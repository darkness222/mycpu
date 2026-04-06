#include <iostream>
#include <sstream>
#include <thread>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <fstream>
#include <atomic>
#include <cstdlib>
#include <array>
#include <cstdint>
#include "RpcServer.h"
#include "../cpu/CPU.h"
#include "../cpu/CpuCore.h"
#include "../cpu/PipelinedCPU.h"
#include "../cpu/Decoder.h"
#include "../memory/Memory.h"
#include "../bus/Bus.h"
#include "../devices/Device.h"
#include "../elf/ElfLoader.h"
#include "../assembler/Assembler.h"

#ifdef _WIN32
#include <direct.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <limits.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace
{
    std::atomic<uint64_t> g_game_step_counter{0};

    bool fileExists(const std::string &path)
    {
        std::ifstream file(path);
        return file.good();
    }

    std::string joinPath(const std::string &base, const std::string &relative)
    {
        if (base.empty())
        {
            return relative;
        }

        const char last = base.back();
        if (last == '\\' || last == '/')
        {
            return base + relative;
        }
        return base + "\\" + relative;
    }

    std::string parentDirectory(const std::string &path)
    {
        const size_t pos = path.find_last_of("\\/");
        if (pos == std::string::npos)
        {
            return "";
        }
        return path.substr(0, pos);
    }

    std::string getCurrentDirectoryString()
    {
        char buffer[4096] = {0};
#ifdef _WIN32
        if (_getcwd(buffer, sizeof(buffer)) != nullptr)
#else
        if (getcwd(buffer, sizeof(buffer)) != nullptr)
#endif
        {
            return buffer;
        }
        return "";
    }

    std::string getExecutableDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH] = {0};
        DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return parentDirectory(std::string(buffer, length));
        }
#endif
        return "";
    }

    std::string findProjectFile(const std::string &relative_path)
    {
        std::vector<std::string> candidates = {
            relative_path
        };

        const std::string current_dir = getCurrentDirectoryString();
        const std::string current_parent = parentDirectory(current_dir);
        const std::string current_grandparent = parentDirectory(current_parent);
        const std::string exe_dir = getExecutableDirectory();
        const std::string exe_parent = parentDirectory(exe_dir);

        if (!current_dir.empty())
            candidates.push_back(joinPath(current_dir, relative_path));
        if (!current_parent.empty())
            candidates.push_back(joinPath(current_parent, relative_path));
        if (!current_grandparent.empty())
            candidates.push_back(joinPath(current_grandparent, relative_path));
        if (!exe_dir.empty())
            candidates.push_back(joinPath(exe_dir, relative_path));
        if (!exe_parent.empty())
            candidates.push_back(joinPath(exe_parent, relative_path));

        for (const auto &candidate : candidates)
        {
            if (fileExists(candidate))
            {
                return candidate;
            }
        }

        return "";
    }

    std::string readFile(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string jsonUnescape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size())
            {
                char n = s[++i];
                switch (n)
                {
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '"':
                    out += '"';
                    break;
                default:
                    out += n;
                    break;
                }
            }
            else
            {
                out += c;
            }
        }
        return out;
    }

    std::string extractJsonStringField(const std::string &body, const std::string &key)
    {
        std::string pattern = "\"" + key + "\"";
        size_t key_pos = body.find(pattern);
        if (key_pos == std::string::npos)
            return "";

        size_t colon_pos = body.find(':', key_pos + pattern.size());
        if (colon_pos == std::string::npos)
            return "";

        size_t quote_start = body.find('"', colon_pos);
        if (quote_start == std::string::npos)
            return "";

        std::string raw;
        bool escaped = false;
        for (size_t i = quote_start + 1; i < body.size(); ++i)
        {
            char c = body[i];
            if (escaped)
            {
                raw += '\\';
                raw += c;
                escaped = false;
                continue;
            }
            if (c == '\\')
            {
                escaped = true;
                continue;
            }
            if (c == '"')
            {
                return jsonUnescape(raw);
            }
            raw += c;
        }
        return "";
    }

    std::string extractHeaderValue(const std::string &request, const std::string &header_name)
    {
        const std::string needle = header_name + ":";
        size_t pos = request.find(needle);
        if (pos == std::string::npos)
        {
            return "";
        }
        pos += needle.size();
        while (pos < request.size() && (request[pos] == ' ' || request[pos] == '\t'))
        {
            ++pos;
        }
        size_t end = request.find("\r\n", pos);
        std::string value = request.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        {
            value.pop_back();
        }
        return value;
    }

    uint32_t leftRotate(uint32_t value, size_t bits)
    {
        return (value << bits) | (value >> (32 - bits));
    }

    std::array<uint8_t, 20> sha1Bytes(const std::string &input)
    {
        std::vector<uint8_t> data(input.begin(), input.end());
        const uint64_t bit_length = static_cast<uint64_t>(data.size()) * 8;
        data.push_back(0x80);
        while ((data.size() % 64) != 56)
        {
            data.push_back(0x00);
        }
        for (int i = 7; i >= 0; --i)
        {
            data.push_back(static_cast<uint8_t>((bit_length >> (i * 8)) & 0xFF));
        }

        uint32_t h0 = 0x67452301;
        uint32_t h1 = 0xEFCDAB89;
        uint32_t h2 = 0x98BADCFE;
        uint32_t h3 = 0x10325476;
        uint32_t h4 = 0xC3D2E1F0;

        for (size_t chunk = 0; chunk < data.size(); chunk += 64)
        {
            uint32_t w[80] = {};
            for (size_t i = 0; i < 16; ++i)
            {
                const size_t offset = chunk + i * 4;
                w[i] = (static_cast<uint32_t>(data[offset]) << 24) |
                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
                       static_cast<uint32_t>(data[offset + 3]);
            }
            for (size_t i = 16; i < 80; ++i)
            {
                w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            uint32_t a = h0;
            uint32_t b = h1;
            uint32_t c = h2;
            uint32_t d = h3;
            uint32_t e = h4;

            for (size_t i = 0; i < 80; ++i)
            {
                uint32_t f = 0;
                uint32_t k = 0;
                if (i < 20)
                {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                }
                else if (i < 40)
                {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                }
                else if (i < 60)
                {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                }
                else
                {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }

                const uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = leftRotate(b, 30);
                b = a;
                a = temp;
            }

            h0 += a;
            h1 += b;
            h2 += c;
            h3 += d;
            h4 += e;
        }

        return {
            static_cast<uint8_t>((h0 >> 24) & 0xFF), static_cast<uint8_t>((h0 >> 16) & 0xFF), static_cast<uint8_t>((h0 >> 8) & 0xFF), static_cast<uint8_t>(h0 & 0xFF),
            static_cast<uint8_t>((h1 >> 24) & 0xFF), static_cast<uint8_t>((h1 >> 16) & 0xFF), static_cast<uint8_t>((h1 >> 8) & 0xFF), static_cast<uint8_t>(h1 & 0xFF),
            static_cast<uint8_t>((h2 >> 24) & 0xFF), static_cast<uint8_t>((h2 >> 16) & 0xFF), static_cast<uint8_t>((h2 >> 8) & 0xFF), static_cast<uint8_t>(h2 & 0xFF),
            static_cast<uint8_t>((h3 >> 24) & 0xFF), static_cast<uint8_t>((h3 >> 16) & 0xFF), static_cast<uint8_t>((h3 >> 8) & 0xFF), static_cast<uint8_t>(h3 & 0xFF),
            static_cast<uint8_t>((h4 >> 24) & 0xFF), static_cast<uint8_t>((h4 >> 16) & 0xFF), static_cast<uint8_t>((h4 >> 8) & 0xFF), static_cast<uint8_t>(h4 & 0xFF)
        };
    }

    std::string base64Encode(const uint8_t *data, size_t len)
    {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3)
        {
            const uint32_t octet_a = i < len ? data[i] : 0;
            const uint32_t octet_b = (i + 1) < len ? data[i + 1] : 0;
            const uint32_t octet_c = (i + 2) < len ? data[i + 2] : 0;
            const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
            out.push_back(table[(triple >> 18) & 0x3F]);
            out.push_back(table[(triple >> 12) & 0x3F]);
            out.push_back((i + 1) < len ? table[(triple >> 6) & 0x3F] : '=');
            out.push_back((i + 2) < len ? table[triple & 0x3F] : '=');
        }
        return out;
    }

    std::string buildWebSocketAccept(const std::string &key)
    {
        const auto digest = sha1Bytes(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        return base64Encode(digest.data(), digest.size());
    }

}

namespace mycpu
{

    RpcServer::RpcServer(uint16 port) : port_(port), running_(false) {}

    RpcServer::~RpcServer()
    {
        stop();
    }

    void RpcServer::setSimulator(std::shared_ptr<Simulator> simulator)
    {
        simulator_ = simulator;
    }

    void RpcServer::start()
    {
        if (running_)
            return;

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET)
        {
            std::cerr << "Failed to create socket" << std::endl;
            return;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_socket, (sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
        {
            std::cerr << "Bind failed on port " << port_;
#ifdef _WIN32
            std::cerr << " (WSA error " << WSAGetLastError() << ")";
#endif
            std::cerr << std::endl;
#ifdef _WIN32
            closesocket(server_socket);
#else
            close(server_socket);
#endif
            return;
        }

        listen(server_socket, 5);
        std::cout << "RPC Server listening on port " << port_ << std::endl;

        running_ = true;

        while (running_)
        {
            sockaddr_in client_addr;
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            SOCKET client_socket = accept(server_socket, (sockaddr *)&client_addr, &client_len);

            if (client_socket == INVALID_SOCKET)
            {
                continue;
            }

            std::string raw_request;

            // 先接收请求头
            while (true)
            {
                char temp_buffer[4096];
                int bytes_received = recv(client_socket, temp_buffer, sizeof(temp_buffer) - 1, 0);
                if (bytes_received <= 0)
                    break;
                temp_buffer[bytes_received] = '\0';
                raw_request += temp_buffer;

                // 检查是否收到完整的 HTTP 头（包含空行）
                size_t header_end = raw_request.find("\r\n\r\n");
                if (header_end != std::string::npos)
                {
                    break;
                }
                if (raw_request.length() > 65536)
                    break; // 防止无限循环
            }

            // 解析 Content-Length 获取 body 大小
            size_t content_length = 0;
            size_t body_start_pos = raw_request.find("\r\n\r\n");
            if (body_start_pos != std::string::npos)
            {
                std::string headers = raw_request.substr(0, body_start_pos);
                size_t cl_pos = headers.find("Content-Length:");
                if (cl_pos != std::string::npos)
                {
                    size_t cl_start = cl_pos + 15;
                    size_t cl_end = headers.find("\r\n", cl_start);
                    if (cl_end != std::string::npos)
                    {
                        std::string cl_str = headers.substr(cl_start, cl_end - cl_start);
                        content_length = std::stoul(cl_str);
                    }
                }

                // 计算已接收的 body 长度
                size_t body_start_idx = body_start_pos + 4;
                size_t received_body_len = raw_request.length() - body_start_idx;

                // 如果 body 不完整，继续接收
                while (received_body_len < content_length)
                {
                    char temp_buffer[4096];
                    int bytes_received = recv(client_socket, temp_buffer, sizeof(temp_buffer) - 1, 0);
                    if (bytes_received <= 0)
                        break;
                    temp_buffer[bytes_received] = '\0';
                    raw_request += temp_buffer;
                    received_body_len += bytes_received;
                }
            }

            if (raw_request.empty())
                continue;

            // 解析 HTTP 请求
            std::string method, path, body;
            size_t first_line_end = raw_request.find("\r\n");
            if (first_line_end != std::string::npos)
            {
                std::string first_line = raw_request.substr(0, first_line_end);
                size_t space1 = first_line.find(' ');
                size_t space2 = first_line.find(' ', space1 + 1);
                if (space1 != std::string::npos && space2 != std::string::npos)
                {
                    method = first_line.substr(0, space1);
                    path = first_line.substr(space1 + 1, space2 - space1 - 1);
                }
            }

            // 提取 POST body
            size_t body_start = raw_request.find("\r\n\r\n");
            if (body_start != std::string::npos)
            {
                body = raw_request.substr(body_start + 4);
                // 移除可能的尾部空白字符
                while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == '\0'))
                {
                    body.pop_back();
                }
            }

            std::cout << "Received request: " << method << " " << path << " (body size: " << body.length() << ")" << std::endl;

            const std::string upgrade = extractHeaderValue(raw_request, "Upgrade");
            if (path == "/ws" && !upgrade.empty() && (upgrade == "websocket" || upgrade == "WebSocket"))
            {
                if (handleWebSocketSession(static_cast<std::intptr_t>(client_socket), path, raw_request))
                {
                    continue;
                }
            }

            std::string response;
            std::string http_response;

            // 根据路径路由请求
            if (path == "/load_elf" && method == "POST")
            {
                handleLoadElf(body, response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/get_state" || path == "/api/get_state")
            {
                handleGetState(response);
                std::cout << "  handleGetState completed, response size: " << response.length() << std::endl;
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/step" || path == "/api/step")
            {
                handleStep(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/step_instruction" || path == "/api/step_instruction")
            {
                handleStepInstruction(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/reset" || path == "/api/reset")
            {
                handleReset(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/set_mode" && method == "POST")
            {
                handleSetMode(body, response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if ((path == "/get_mode" || path == "/api/get_mode") && method == "GET")
            {
                handleGetMode(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/assemble" && method == "POST")
            {
                handleAssemble(body, response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if ((path == "/get_instructions" || path == "/api/get_instructions") && method == "GET")
            {
                handleGetInstructions(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/game/init" && method == "POST")
            {
                handleGameInit(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (path == "/game/step" && method == "POST")
            {
                handleGameStep(body, response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if ((path == "/game/get_state" || path == "/api/game/get_state") && method == "GET")
            {
                handleGameGetState(response);
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                std::to_string(response.length()) + "\r\n\r\n" + response;
            }
            else if (method == "OPTIONS")
            {
                // 处理 CORS 预检请求
                http_response = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
            }
            else
            {
                // 兼容旧的字符串请求格式
                handleRequest(raw_request, response);
                if (response.find("{") == 0)
                {
                    // JSON 响应，添加 HTTP 头
                    http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " +
                                    std::to_string(response.length()) + "\r\n\r\n" + response;
                }
                else
                {
                    http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + response;
                }
            }

            std::cout << "  Sending response (" << http_response.length() << " bytes)..." << std::endl;
            int sent = send(client_socket, http_response.c_str(), static_cast<int>(http_response.length()), 0);
            std::cout << "  send() returned: " << sent << std::endl;

#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
        }

#ifdef _WIN32
        closesocket(server_socket);
        WSACleanup();
#else
        close(server_socket);
#endif
    }

    void RpcServer::stop()
    {
        running_ = false;
    }

    void RpcServer::handleRequest(const std::string &request, std::string &response)
    {
        if (request.find("get_state") != std::string::npos)
        {
            handleGetState(response);
        }
        else if (request.find("step") != std::string::npos)
        {
            handleStep(response);
        }
        else if (request.find("step_instruction") != std::string::npos)
        {
            handleStepInstruction(response);
        }
        else if (request.find("reset") != std::string::npos)
        {
            handleReset(response);
        }
        else if (request.find("set_mode") != std::string::npos)
        {
            handleSetMode(request, response);
        }
        else if (request.find("get_mode") != std::string::npos)
        {
            handleGetMode(response);
        }
        else if (request.find("load_program") != std::string::npos)
        {
            handleLoadProgram(request, response);
        }
        else if (request.find("load_elf") != std::string::npos)
        {
            handleLoadElf(request, response);
        }
        else if (request.find("load_binary") != std::string::npos)
        {
            handleLoadBinary(request, response);
        }
        else
        {
            response = "{\"error\": \"Unknown command\"}";
        }
    }

    void RpcServer::handleGetState(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        response = simulator_->toJson();
    }

    void RpcServer::handleStep(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        simulator_->step();
        response = simulator_->toJson();
    }

    void RpcServer::handleStepInstruction(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        simulator_->stepInstruction();
        response = simulator_->toJson();
    }

    void RpcServer::handleReset(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        simulator_->reset();
        response = simulator_->toJson();
    }

    void RpcServer::handleSetMode(const std::string &request, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        std::string mode_text = extractJsonStringField(request, "mode");
        if (mode_text.empty())
        {
            response = "{\"error\": \"Missing mode\"}";
            return;
        }

        const bool want_pipeline = mode_text == "PIPELINED" || mode_text == "pipelined" || mode_text == "pipeline";
        const SimulationMode mode = want_pipeline ? SimulationMode::PIPELINED : SimulationMode::MULTI_CYCLE;
        simulator_->setMode(mode);
        response = simulator_->toJson();
    }

    void RpcServer::handleGetMode(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        response = simulator_->toJson();
    }

    void RpcServer::handleLoadProgram(const std::string &, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        response = "{\"status\": \"Program loaded\"}";
    }

    void RpcServer::handleAssemble(const std::string &request, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        std::string source = extractJsonStringField(request, "source");

        if (source.empty())
        {
            response = "{\"error\": \"No source code in request\"}";
            return;
        }

        std::cout << "Assembling source (" << source.length() << " chars)..." << std::endl;
        std::cout << "  First 100 chars: " << source.substr(0, 100) << std::endl;

        Assembler assembler;
        auto result = assembler.assemble(source);

        if (!result.success)
        {
            std::ostringstream oss;
            for (size_t i = 0; i < result.errors.size(); ++i)
            {
                if (i > 0)
                    oss << "\\n";
                oss << result.errors[i];
            }
            response = "{\"error\": \"" + escapeJson(oss.str()) + "\"}";
            std::cout << "  FAILED: " << oss.str() << std::endl;
            std::cout << "  Response: " << response << std::endl;
            return;
        }

        simulator_->loadProgram(result.instructions, 0);
        std::cout << "  SUCCESS: " << result.instructions.size() << " instructions" << std::endl;

        std::ostringstream oss;
        oss << "{\"status\": \"ok\", \"count\":" << result.instructions.size();
        if (!result.labels.empty())
        {
            oss << ", \"labels\":[";
            for (size_t i = 0; i < result.labels.size(); ++i)
            {
                if (i > 0)
                    oss << ",";
                oss << "\"" << escapeJson(result.labels[i]) << "\"";
            }
            oss << "]";
        }
        oss << "}";
        response = oss.str();
    }

    void RpcServer::handleLoadElf(const std::string &request, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        // 解析 JSON body: {"data": "base64..."}
        std::string base64_data;

        // 尝试从 JSON 中提取 data 字段
        size_t data_pos = request.find("\"data\"");
        if (data_pos != std::string::npos)
        {
            size_t colon_pos = request.find(":", data_pos);
            if (colon_pos != std::string::npos)
            {
                size_t quote_start = request.find("\"", colon_pos);
                if (quote_start != std::string::npos)
                {
                    size_t quote_end = request.find("\"", quote_start + 1);
                    if (quote_end != std::string::npos)
                    {
                        base64_data = request.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }

        // 如果 JSON 解析失败，尝试旧的格式 "data=..."
        if (base64_data.empty())
        {
            size_t pos = request.find("data=");
            if (pos != std::string::npos)
            {
                base64_data = request.substr(pos + 5);
                // 移除可能的 URL 编码或换行符
                size_t end = base64_data.find_first_of("\r\n\"}");
                if (end != std::string::npos)
                {
                    base64_data = base64_data.substr(0, end);
                }
            }
        }

        if (base64_data.empty())
        {
            std::cout << "  ERROR: No ELF data found in request" << std::endl;
            response = "{\"error\": \"No ELF data in request\"}";
            return;
        }

        std::cout << "  Extracted base64 data, length: " << base64_data.length() << " chars" << std::endl;

        // 简单的 base64 解码
        std::vector<uint8_t> elf_data;
        const char *base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        for (size_t i = 0; i < base64_data.size(); i += 4)
        {
            int v = 0;
            int padding = 0;
            for (int j = 0; j < 4 && i + j < base64_data.size(); ++j)
            {
                char c = base64_data[i + j];
                if (c == '=')
                {
                    padding++;
                    continue;
                }
                const char *p = std::strchr(base64_chars, c);
                if (p)
                {
                    v = (v << 6) | (p - base64_chars);
                }
            }
            v <<= (6 * padding);
            elf_data.push_back((v >> 16) & 0xFF);
            if (padding < 2)
                elf_data.push_back((v >> 8) & 0xFF);
            if (padding < 1)
                elf_data.push_back(v & 0xFF);
        }

        std::cout << "  Base64 decoded, binary size: " << elf_data.size() << " bytes" << std::endl;
        if (elf_data.size() >= 4)
        {
            std::cout << "  First 4 bytes (ELF magic): 0x";
            for (int i = 0; i < 4 && i < (int)elf_data.size(); i++)
            {
                printf("%02X ", elf_data[i]);
            }
            std::cout << std::endl;
        }

        std::cout << "  Attempting to load ELF (size: " << elf_data.size() << " bytes)..." << std::endl;
        if (simulator_->loadElf(elf_data))
        {
            response = "{\"status\": \"ELF loaded successfully\"}";
            std::cout << "  ELF loaded successfully" << std::endl;
        }
        else
        {
            // 获取 CPU 的 trace 来获取详细错误信息
            std::string error_msg = "Failed to load ELF";
            response = "{\"error\": \"" + escapeJson(error_msg) + "\"}";
            std::cout << "  ELF load failed" << std::endl;
        }
    }

    void RpcServer::handleLoadBinary(const std::string &, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }
        response = "{\"status\": \"Binary loaded\"}";
    }

    void RpcServer::handleGetInstructions(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        Decoder dec;
        const auto &instructions = simulator_->getLoadedInstructions();
        std::ostringstream oss;
        oss << "{\"instructions\":[";
        bool first = true;
        for (size_t i = 0; i < instructions.size(); ++i)
        {
            uint32 word = instructions[i];
            uint32 addr = i * 4;
            // 保留 word==0 的槽位，使 JSON 数组下标与 PC/4 一致（否则前端「完成」标记会错位）
            Instruction instr = dec.decode(word, addr);
            std::string text = dec.disassemble(instr);

            if (!first)
                oss << ",";
            first = false;
            oss << "{\"address\":" << addr
                << ",\"raw\":" << word
                << ",\"text\":\"" << escapeJson(text) << "\"";
            if (instr.rd != 0)
            {
                oss << ",\"rd\":" << (int)instr.rd;
            }
            oss << "}";
        }
        oss << "]}";
        response = oss.str();
    }

    std::string RpcServer::escapeJson(const std::string &str)
    {
        std::string result;
        for (char c : str)
        {
            switch (c)
            {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
            }
        }
        return result;
    }

    bool RpcServer::handleWebSocketSession(std::intptr_t socket_value, const std::string &, const std::string &raw_request)
    {
        SOCKET client_socket = static_cast<SOCKET>(socket_value);
        const std::string key = extractHeaderValue(raw_request, "Sec-WebSocket-Key");
        if (key.empty())
        {
            return false;
        }

        const std::string accept = buildWebSocketAccept(key);
        const std::string handshake =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        send(client_socket, handshake.c_str(), static_cast<int>(handshake.size()), 0);

        while (running_)
        {
            uint8_t header[2] = {};
            int received = recv(client_socket, reinterpret_cast<char *>(header), 2, MSG_WAITALL);
            if (received != 2)
            {
                break;
            }

            const uint8_t opcode = header[0] & 0x0F;
            const bool masked = (header[1] & 0x80) != 0;
            uint64_t payload_len = header[1] & 0x7F;
            if (payload_len == 126)
            {
                uint8_t ext[2] = {};
                if (recv(client_socket, reinterpret_cast<char *>(ext), 2, MSG_WAITALL) != 2) break;
                payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
            }
            else if (payload_len == 127)
            {
                uint8_t ext[8] = {};
                if (recv(client_socket, reinterpret_cast<char *>(ext), 8, MSG_WAITALL) != 8) break;
                payload_len = 0;
                for (int i = 0; i < 8; ++i)
                {
                    payload_len = (payload_len << 8) | ext[i];
                }
            }

            uint8_t mask[4] = {};
            if (masked && recv(client_socket, reinterpret_cast<char *>(mask), 4, MSG_WAITALL) != 4)
            {
                break;
            }

            std::string payload(payload_len, '\0');
            if (payload_len > 0 && recv(client_socket, &payload[0], static_cast<int>(payload_len), MSG_WAITALL) != static_cast<int>(payload_len))
            {
                break;
            }

            if (masked)
            {
                for (size_t i = 0; i < payload.size(); ++i)
                {
                    payload[i] ^= mask[i % 4];
                }
            }

            if (opcode == 0x8)
            {
                break;
            }
            if (opcode == 0x9)
            {
                std::string pong;
                pong.push_back(static_cast<char>(0x8A));
                pong.push_back(static_cast<char>(payload.size()));
                pong += payload;
                send(client_socket, pong.c_str(), static_cast<int>(pong.size()), 0);
                continue;
            }
            if (opcode != 0x1)
            {
                continue;
            }

            std::string response = "{\"error\":\"Unknown message type\"}";
            const std::string type = extractJsonStringField(payload, "type");
            if (type == "game_init")
            {
                handleGameInit(response);
            }
            else if (type == "game_step")
            {
                response = buildGameStateJson(payload, true);
            }
            else if (type == "get_state")
            {
                response = buildGameStateJson(payload, false);
            }

            std::string frame;
            frame.push_back(static_cast<char>(0x81));
            if (response.size() < 126)
            {
                frame.push_back(static_cast<char>(response.size()));
            }
            else
            {
                frame.push_back(126);
                frame.push_back(static_cast<char>((response.size() >> 8) & 0xFF));
                frame.push_back(static_cast<char>(response.size() & 0xFF));
            }
            frame += response;
            send(client_socket, frame.c_str(), static_cast<int>(frame.size()), 0);
        }

#ifdef _WIN32
        closesocket(client_socket);
#else
        close(client_socket);
#endif
        return true;
    }

    std::string RpcServer::buildGameStateJson(const std::string &request, bool include_full_state)
    {
        std::string response;
        if (include_full_state)
        {
            handleGameStep(request, response);
        }
        else
        {
            handleGameGetState(response);
        }
        return response;
    }

    void RpcServer::handleGameInit(std::string &response)
    {
        std::cout << "[GAME] handleGameInit called (using virtual CPU)" << std::endl;

        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            std::cout << "[GAME] ERROR: No simulator" << std::endl;
            return;
        }

        // Keep gameplay deterministic. The game program is authored and verified
        // against the multi-cycle core; inheriting the last UI-selected mode can
        // cause pipeline-specific state divergence.
        simulator_->setMode(SimulationMode::MULTI_CYCLE);
        simulator_->reset();

        std::string asm_path = findProjectFile("game/dino_game.s");
        std::cout << "[GAME] Reading assembly file: " << (asm_path.empty() ? "game/dino_game.s (not found)" : asm_path) << std::endl;
        std::string asm_source = asm_path.empty() ? "" : readFile(asm_path);

        if (asm_source.empty())
        {
            response = "{\"error\": \"Failed to read dino_game.s. Check working directory or file path.\"}";
            std::cout << "[GAME] ERROR: Failed to read assembly file" << std::endl;
            return;
        }
        std::cout << "[GAME] Assembly file loaded, size: " << asm_source.length() << " chars" << std::endl;

        Assembler assembler;
        auto result = assembler.assemble(asm_source);

        if (!result.success)
        {
            std::ostringstream oss;
            for (size_t i = 0; i < result.errors.size(); ++i)
            {
                if (i > 0)
                    oss << "\\n";
                oss << result.errors[i];
            }
            response = "{\"error\": \"" + escapeJson(oss.str()) + "\"}";
            return;
        }

        simulator_->loadProgram(result.instructions, 0);

        response = "{\"status\": \"ok\"}";
    }

    void RpcServer::handleGameStep(const std::string &request, std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        bool jump_request = false;
        size_t jump_pos = request.find("\"jump\"");
        if (jump_pos != std::string::npos)
        {
            size_t colon_pos = request.find(":", jump_pos);
            if (colon_pos != std::string::npos)
            {
                size_t value_pos = colon_pos + 1;
                while (value_pos < request.length() && (request[value_pos] == ' ' || request[value_pos] == '\t' || request[value_pos] == '\r' || request[value_pos] == '\n'))
                {
                    value_pos++;
                }
                if (value_pos < request.length() && request[value_pos] == 't')
                {
                    jump_request = true;
                }
            }
        }

        if (jump_request)
        {
            std::cout << "[GAME] Jump: YES" << std::endl;
        }

        auto memory = simulator_->getMemory();
        if (!memory)
        {
            response = "{\"error\": \"No memory\"}";
            return;
        }

        auto state = simulator_->getState();
        if (state.state == CpuState::HALTED)
        {
            std::cout << "[GAME] CPU halted, restarting from PC=0" << std::endl;
            simulator_->resetCpuOnly();
        }

        uint32 control_word = 0;
        if (jump_request)
        {
            control_word |= 0x1;
        }
        memory->writeWord(0x2000, control_word);

        const int MAX_INSTRUCTIONS = 1000;
        int executed_instructions = 0;
        for (int i = 0; i < MAX_INSTRUCTIONS; ++i)
        {
            auto exec_state = simulator_->getState();
            if (exec_state.state == CpuState::HALTED)
            {
                break;
            }
            simulator_->stepInstruction();
            executed_instructions++;
        }

        uint32 dino_y = memory->readWord(0x2004);
        uint32 dino_vy = memory->readWord(0x2008);
        uint32 score = memory->readWord(0x2010);
        uint32 control_word_out = memory->readWord(0x2000);
        uint32 obstacle_count = memory->readWord(0x2014);
        const bool enable_verbose_log = false;
        if (enable_verbose_log)
        {

        // Debug: 输出每帧执行信息和内存区域快照（0x2000 - 0x2030）
        std::cout << "[GAME] Executed instructions this step: " << executed_instructions
                  << ", dino_y=" << static_cast<int32_t>(dino_y)
                  << ", dino_vy=" << static_cast<int32_t>(dino_vy)
                  << ", score=" << score
                  << ", control_word=0x" << std::hex << control_word_out << std::dec
                  << std::endl;
        for (uint32 addr = 0x2000; addr <= 0x2030; addr += 4)
        {
            uint32 v = memory->readWord(addr);
            char buf[128];
            snprintf(buf, sizeof(buf), "[GAME] MEM[0x%08X] = 0x%08X (%d)", addr, v, static_cast<int32_t>(v));
            std::cout << buf << std::endl;
        }

        }

        bool game_over = (control_word_out & 0x2) != 0;
        const uint64_t step_id = ++g_game_step_counter;
        if (jump_request || game_over || (step_id % 25 == 0))
        {
            std::cout << "[GAME] step=" << step_id
                      << " executed=" << executed_instructions
                      << " dino_y=" << static_cast<int32_t>(dino_y)
                      << " dino_vy=" << static_cast<int32_t>(dino_vy)
                      << " score=" << score
                      << " obstacles=" << obstacle_count
                      << " game_over=" << (game_over ? "true" : "false")
                      << std::endl;
        }

        std::vector<std::pair<int, int>> obstacles;
        for (uint32 i = 0; i < obstacle_count && i < 30; ++i)
        {
            uint32 addr = 0x2020 + i * 8;
            uint32 obs_x = memory->readWord(addr);
            uint32 obs_type = memory->readWord(addr + 4);
            obstacles.push_back({(int32_t)obs_x, (int)obs_type});
        }

        std::ostringstream oss;
        oss << "{";
        oss << "\"dino_y\":" << (int32_t)dino_y << ",";
        oss << "\"dino_vy\":" << (int32_t)dino_vy << ",";
        oss << "\"score\":" << score << ",";
        oss << "\"executed_instructions\":" << executed_instructions << ",";
        oss << "\"max_instructions_per_step\":" << MAX_INSTRUCTIONS << ",";
        oss << "\"game_over\":" << (game_over ? "true" : "false") << ",";
        oss << "\"obstacles\":[";
        for (size_t i = 0; i < obstacles.size(); i++)
        {
            if (i > 0)
                oss << ",";
            oss << "{\"x\":" << obstacles[i].first << ",\"type\":" << obstacles[i].second << "}";
        }
        oss << "]";
        oss << "}";
        response = oss.str();
    }

    void RpcServer::handleGameGetState(std::string &response)
    {
        if (!simulator_)
        {
            response = "{\"error\": \"No simulator\"}";
            return;
        }

        auto memory = simulator_->getMemory();
        if (!memory)
        {
            response = "{\"error\": \"No memory\"}";
            return;
        }

        uint32 dino_y = memory->readWord(0x2004);
        uint32 score = memory->readWord(0x2010);
        uint32 control_word = memory->readWord(0x2000);
        bool game_over = (control_word & 0x2) != 0;

        std::ostringstream oss;
        oss << "{";
        oss << "\"dino_y\":" << (int32_t)dino_y << ",";
        oss << "\"score\":" << score << ",";
        oss << "\"game_over\":" << (game_over ? "true" : "false");
        oss << "}";
        response = oss.str();
    }

    Simulator::Simulator()
    {
        memory_ = std::make_shared<Memory>();
        bus_ = std::make_shared<Bus>();
        multicycle_cpu_ = std::make_shared<CPU>();
        pipelined_cpu_ = std::make_shared<PipelinedCPU>();
        cpu_ = multicycle_cpu_;

        multicycle_cpu_->setMemory(memory_);
        multicycle_cpu_->setBus(bus_);
        pipelined_cpu_->setMemory(memory_);
        pipelined_cpu_->setBus(bus_);

        auto uart = std::make_shared<UARTDevice>();
        auto timer = std::make_shared<TimerDevice>();
        auto interrupt_controller = std::make_shared<InterruptControllerDevice>();

        bus_->connectDevice(constants::UART_BASE, uart);
        bus_->connectDevice(constants::TIMER_BASE, timer);
        bus_->connectDevice(constants::INTERRUPT_BASE, interrupt_controller);

        reset();
    }

    void Simulator::reset()
    {
        memory_->reset();
        bus_->reset();
        cpu_->reset();

        if (suppress_reload_on_reset_)
        {
            return;
        }

        switch (loaded_program_kind_)
        {
        case LoadedProgramKind::ASSEMBLY:
            if (!loaded_instructions_.empty())
            {
                memory_->identityMapRange(constants::TEXT_START, constants::MEMORY_END);
                memory_->identityMapRange(constants::MMIO_BASE, constants::MMIO_BASE + 0xFFF);
                memory_->enablePaging(true);
                cpu_->loadProgram(loaded_instructions_, loaded_start_address_);
            }
            break;
        case LoadedProgramKind::BINARY:
            if (!loaded_instructions_.empty())
            {
                cpu_->loadBinary(loaded_instructions_, loaded_start_address_);
            }
            break;
        case LoadedProgramKind::ELF:
            if (!loaded_elf_.empty())
            {
                cpu_->loadElf(loaded_elf_);
            }
            break;
        case LoadedProgramKind::NONE:
        default:
            break;
        }
    }

    void Simulator::resetCpuOnly()
    {
        cpu_->reset();
        if (loaded_program_kind_ == LoadedProgramKind::ASSEMBLY && !loaded_instructions_.empty())
        {
            cpu_->loadProgram(loaded_instructions_, loaded_start_address_);
        }
    }

    void Simulator::step()
    {
        cpu_->step();
    }

    void Simulator::stepInstruction()
    {
        const uint64 start_count = cpu_->getStats().instruction_count;
        for (int i = 0; i < 32; ++i)
        {
            cpu_->step();
            if (cpu_->getState() == CpuState::HALTED)
                break;
            if (cpu_->getStats().instruction_count > start_count)
                break;
        }
    }

    bool Simulator::setMode(SimulationMode mode)
    {
        if (mode_ == mode)
        {
            return true;
        }

        mode_ = mode;
        cpu_ = (mode_ == SimulationMode::PIPELINED)
                   ? std::static_pointer_cast<CpuCore>(pipelined_cpu_)
                   : std::static_pointer_cast<CpuCore>(multicycle_cpu_);
        reset();
        return true;
    }

    void Simulator::loadProgram(const std::vector<uint32> &program, uint32 start_address)
    {
        loaded_program_kind_ = LoadedProgramKind::ASSEMBLY;
        loaded_instructions_ = program;
        loaded_start_address_ = start_address;
        loaded_elf_.clear();
        suppress_reload_on_reset_ = true;
        reset();
        suppress_reload_on_reset_ = false;
        memory_->identityMapRange(constants::TEXT_START, constants::MEMORY_END);
        memory_->identityMapRange(constants::MMIO_BASE, constants::MMIO_BASE + 0xFFF);
        memory_->enablePaging(true);
        cpu_->loadProgram(program, start_address);
    }

    void Simulator::loadBinary(const std::vector<uint32> &program, uint32 start_address)
    {
        loaded_program_kind_ = LoadedProgramKind::BINARY;
        loaded_instructions_ = program;
        loaded_start_address_ = start_address;
        loaded_elf_.clear();
        suppress_reload_on_reset_ = true;
        reset();
        suppress_reload_on_reset_ = false;
        cpu_->loadBinary(program, start_address);
    }

    bool Simulator::loadElf(const std::vector<uint8> &elf_data)
    {
        loaded_program_kind_ = LoadedProgramKind::ELF;
        loaded_elf_ = elf_data;
        loaded_instructions_.clear();
        loaded_start_address_ = 0;
        suppress_reload_on_reset_ = true;
        reset();
        suppress_reload_on_reset_ = false;
        return cpu_->loadElf(elf_data);
    }

    SimulatorState Simulator::getState() const
    {
        SimulatorState state = cpu_->getSimulatorState();
        state.mode = mode_;
        state.mode_name = mode_ == SimulationMode::PIPELINED ? "Pipelined" : "Multi-cycle";
        if (mode_ == SimulationMode::PIPELINED)
        {
            state.true_pipeline = cpu_->supportsTrueOverlapPipeline();
            if (!state.true_pipeline && state.mode_note.empty())
            {
                state.mode_note = "Pipeline mode scaffold is wired end to end, but the true overlapped core is still under construction.";
            }
        }
        return state;
    }

    std::string Simulator::toJson() const
    {
        SimulatorState state = getState();
        return state.toJson(memory_.get());
    }

} // namespace mycpu
