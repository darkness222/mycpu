#include "RpcServer.h"
#include "../cpu/CPU.h"
#include "../cpu/Decoder.h"
#include "../memory/Memory.h"
#include "../bus/Bus.h"
#include "../devices/Device.h"
#include "../elf/ElfLoader.h"
#include "../assembler/Assembler.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

namespace {

std::string extractHttpBody(const std::string& request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return request;
    return request.substr(pos + 4);
}

std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                default: out += n; break;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string extractJsonStringField(const std::string& body, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t key_pos = body.find(pattern);
    if (key_pos == std::string::npos) return "";

    size_t colon_pos = body.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) return "";

    size_t quote_start = body.find('"', colon_pos);
    if (quote_start == std::string::npos) return "";

    std::string raw;
    bool escaped = false;
    for (size_t i = quote_start + 1; i < body.size(); ++i) {
        char c = body[i];
        if (escaped) {
            raw += '\\';
            raw += c;
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return jsonUnescape(raw);
        }
        raw += c;
    }
    return "";
}

}

namespace mycpu {

RpcServer::RpcServer(uint16 port) : port_(port), running_(false) {}

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::setSimulator(std::shared_ptr<Simulator> simulator) {
    simulator_ = simulator;
}

void RpcServer::start() {
    if (running_) return;

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
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

    while (running_) {
        sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        std::string raw_request;
        
        // 先接收请求头
        while (true) {
            char temp_buffer[4096];
            int bytes_received = recv(client_socket, temp_buffer, sizeof(temp_buffer) - 1, 0);
            if (bytes_received <= 0) break;
            temp_buffer[bytes_received] = '\0';
            raw_request += temp_buffer;
            
            // 检查是否收到完整的 HTTP 头（包含空行）
            size_t header_end = raw_request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                break;
            }
            if (raw_request.length() > 65536) break; // 防止无限循环
        }
        
        // 解析 Content-Length 获取 body 大小
        size_t content_length = 0;
        size_t body_start_pos = raw_request.find("\r\n\r\n");
        if (body_start_pos != std::string::npos) {
            std::string headers = raw_request.substr(0, body_start_pos);
            size_t cl_pos = headers.find("Content-Length:");
            if (cl_pos != std::string::npos) {
                size_t cl_start = cl_pos + 15;
                size_t cl_end = headers.find("\r\n", cl_start);
                if (cl_end != std::string::npos) {
                    std::string cl_str = headers.substr(cl_start, cl_end - cl_start);
                    content_length = std::stoul(cl_str);
                }
            }
            
            // 计算已接收的 body 长度
            size_t body_start_idx = body_start_pos + 4;
            size_t received_body_len = raw_request.length() - body_start_idx;
            
            // 如果 body 不完整，继续接收
            while (received_body_len < content_length) {
                char temp_buffer[4096];
                int bytes_received = recv(client_socket, temp_buffer, sizeof(temp_buffer) - 1, 0);
                if (bytes_received <= 0) break;
                temp_buffer[bytes_received] = '\0';
                raw_request += temp_buffer;
                received_body_len += bytes_received;
            }
        }

        if (raw_request.empty()) continue;
        
        // 解析 HTTP 请求
        std::string method, path, body;
        size_t first_line_end = raw_request.find("\r\n");
        if (first_line_end != std::string::npos) {
            std::string first_line = raw_request.substr(0, first_line_end);
            size_t space1 = first_line.find(' ');
            size_t space2 = first_line.find(' ', space1 + 1);
            if (space1 != std::string::npos && space2 != std::string::npos) {
                method = first_line.substr(0, space1);
                path = first_line.substr(space1 + 1, space2 - space1 - 1);
            }
        }
        
        // 提取 POST body
        size_t body_start = raw_request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body = raw_request.substr(body_start + 4);
            // 移除可能的尾部空白字符
            while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == '\0')) {
                body.pop_back();
            }
        }
        
        std::cout << "Received request: " << method << " " << path << " (body size: " << body.length() << ")" << std::endl;
        
        std::string response;
        std::string http_response;
        
        // 根据路径路由请求
        if (path == "/load_elf" && method == "POST") {
            handleLoadElf(body, response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (path == "/get_state" || path == "/api/get_state") {
            handleGetState(response);
            std::cout << "  handleGetState completed, response size: " << response.length() << std::endl;
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (path == "/step" || path == "/api/step") {
            handleStep(response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (path == "/step_instruction" || path == "/api/step_instruction") {
            handleStepInstruction(response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (path == "/reset" || path == "/api/reset") {
            handleReset(response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (path == "/assemble" && method == "POST") {
            handleAssemble(body, response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if ((path == "/get_instructions" || path == "/api/get_instructions") && method == "GET") {
            handleGetInstructions(response);
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                           std::to_string(response.length()) + "\r\n\r\n" + response;
        } else if (method == "OPTIONS") {
            // 处理 CORS 预检请求
            http_response = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
        } else {
            // 兼容旧的字符串请求格式
            handleRequest(raw_request, response);
            if (response.find("{") == 0) {
                // JSON 响应，添加 HTTP 头
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + 
                               std::to_string(response.length()) + "\r\n\r\n" + response;
            } else {
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

void RpcServer::stop() {
    running_ = false;
}

void RpcServer::handleRequest(const std::string& request, std::string& response) {
    if (request.find("get_state") != std::string::npos) {
        handleGetState(response);
    } else if (request.find("step") != std::string::npos) {
        handleStep(response);
    } else if (request.find("step_instruction") != std::string::npos) {
        handleStepInstruction(response);
    } else if (request.find("reset") != std::string::npos) {
        handleReset(response);
    } else if (request.find("load_program") != std::string::npos) {
        handleLoadProgram(request, response);
    } else if (request.find("load_elf") != std::string::npos) {
        handleLoadElf(request, response);
    } else if (request.find("load_binary") != std::string::npos) {
        handleLoadBinary(request, response);
    } else {
        response = "{\"error\": \"Unknown command\"}";
    }
}

void RpcServer::handleGetState(std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    response = simulator_->toJson();
}

void RpcServer::handleStep(std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    simulator_->step();
    response = simulator_->toJson();
}

void RpcServer::handleStepInstruction(std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    simulator_->stepInstruction();
    response = simulator_->toJson();
}

void RpcServer::handleReset(std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    simulator_->reset();
    response = simulator_->toJson();
}

void RpcServer::handleLoadProgram(const std::string& request, std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    response = "{\"status\": \"Program loaded\"}";
}

void RpcServer::handleAssemble(const std::string& request, std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }

    std::string source = extractJsonStringField(request, "source");

    if (source.empty()) {
        response = "{\"error\": \"No source code in request\"}";
        return;
    }

    std::cout << "Assembling source (" << source.length() << " chars)..." << std::endl;
    std::cout << "  First 100 chars: " << source.substr(0, 100) << std::endl;

    Assembler assembler;
    auto result = assembler.assemble(source);

    if (!result.success) {
        std::ostringstream oss;
        for (size_t i = 0; i < result.errors.size(); ++i) {
            if (i > 0) oss << "\\n";
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
    if (!result.labels.empty()) {
        oss << ", \"labels\":[";
        for (size_t i = 0; i < result.labels.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << escapeJson(result.labels[i]) << "\"";
        }
        oss << "]";
    }
    oss << "}";
    response = oss.str();
}

void RpcServer::handleLoadElf(const std::string& request, std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }

    // 解析 JSON body: {"data": "base64..."}
    std::string base64_data;
    
    // 尝试从 JSON 中提取 data 字段
    size_t data_pos = request.find("\"data\"");
    if (data_pos != std::string::npos) {
        size_t colon_pos = request.find(":", data_pos);
        if (colon_pos != std::string::npos) {
            size_t quote_start = request.find("\"", colon_pos);
            if (quote_start != std::string::npos) {
                size_t quote_end = request.find("\"", quote_start + 1);
                if (quote_end != std::string::npos) {
                    base64_data = request.substr(quote_start + 1, quote_end - quote_start - 1);
                }
            }
        }
    }
    
    // 如果 JSON 解析失败，尝试旧的格式 "data=..."
    if (base64_data.empty()) {
        size_t pos = request.find("data=");
        if (pos != std::string::npos) {
            base64_data = request.substr(pos + 5);
            // 移除可能的 URL 编码或换行符
            size_t end = base64_data.find_first_of("\r\n\"}");
            if (end != std::string::npos) {
                base64_data = base64_data.substr(0, end);
            }
        }
    }
    
    if (base64_data.empty()) {
        std::cout << "  ERROR: No ELF data found in request" << std::endl;
        response = "{\"error\": \"No ELF data in request\"}";
        return;
    }
    
    std::cout << "  Extracted base64 data, length: " << base64_data.length() << " chars" << std::endl;
    
    // 简单的 base64 解码
    std::vector<uint8_t> elf_data;
    const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (size_t i = 0; i < base64_data.size(); i += 4) {
        int v = 0;
        int padding = 0;
        for (int j = 0; j < 4 && i + j < base64_data.size(); ++j) {
            char c = base64_data[i + j];
            if (c == '=') {
                padding++;
                continue;
            }
            const char* p = std::strchr(base64_chars, c);
            if (p) {
                v = (v << 6) | (p - base64_chars);
            }
        }
        v <<= (6 * padding);
        elf_data.push_back((v >> 16) & 0xFF);
        if (padding < 2) elf_data.push_back((v >> 8) & 0xFF);
        if (padding < 1) elf_data.push_back(v & 0xFF);
    }

    std::cout << "  Base64 decoded, binary size: " << elf_data.size() << " bytes" << std::endl;
    if (elf_data.size() >= 4) {
        std::cout << "  First 4 bytes (ELF magic): 0x";
        for (int i = 0; i < 4 && i < (int)elf_data.size(); i++) {
            printf("%02X ", elf_data[i]);
        }
        std::cout << std::endl;
    }

    std::cout << "  Attempting to load ELF (size: " << elf_data.size() << " bytes)..." << std::endl;
    if (simulator_->loadElf(elf_data)) {
        response = "{\"status\": \"ELF loaded successfully\"}";
        std::cout << "  ELF loaded successfully" << std::endl;
    } else {
        // 获取 CPU 的 trace 来获取详细错误信息
        std::string error_msg = "Failed to load ELF";
        response = "{\"error\": \"" + escapeJson(error_msg) + "\"}";
        std::cout << "  ELF load failed" << std::endl;
    }
}

void RpcServer::handleLoadBinary(const std::string& request, std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }
    response = "{\"status\": \"Binary loaded\"}";
}

void RpcServer::handleGetInstructions(std::string& response) {
    if (!simulator_) {
        response = "{\"error\": \"No simulator\"}";
        return;
    }

    Decoder dec;
    const auto& instructions = simulator_->getLoadedInstructions();
    std::ostringstream oss;
    oss << "{\"instructions\":[";
    bool first = true;
    for (size_t i = 0; i < instructions.size(); ++i) {
        uint32 word = instructions[i];
        uint32 addr = i * 4;
        // 保留 word==0 的槽位，使 JSON 数组下标与 PC/4 一致（否则前端「完成」标记会错位）
        Instruction instr = dec.decode(word, addr);
        std::string text = dec.disassemble(instr);

        if (!first) oss << ",";
        first = false;
        oss << "{\"address\":" << addr
            << ",\"raw\":" << word
            << ",\"text\":\"" << escapeJson(text) << "\"";
        if (instr.rd != 0) {
            oss << ",\"rd\":" << (int)instr.rd;
        }
        oss << "}";
    }
    oss << "]}";
    response = oss.str();
}

std::string RpcServer::escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

Simulator::Simulator() {
    memory_ = std::make_shared<Memory>();
    bus_ = std::make_shared<Bus>();
    cpu_ = std::make_shared<CPU>();

    cpu_->setMemory(memory_);
    cpu_->setBus(bus_);

    auto uart = std::make_shared<UARTDevice>();
    auto timer = std::make_shared<TimerDevice>();
    auto interrupt_controller = std::make_shared<InterruptControllerDevice>();

    bus_->connectDevice(constants::UART_BASE, uart);
    bus_->connectDevice(constants::TIMER_BASE, timer);
    bus_->connectDevice(constants::INTERRUPT_BASE, interrupt_controller);

    reset();
}

void Simulator::reset() {
    memory_->reset();
    bus_->reset();
    cpu_->reset();

    if (suppress_reload_on_reset_) {
        return;
    }

    switch (loaded_program_kind_) {
        case LoadedProgramKind::ASSEMBLY:
            if (!loaded_instructions_.empty()) {
                memory_->identityMapRange(constants::TEXT_START, constants::MEMORY_END);
                memory_->identityMapRange(constants::MMIO_BASE, constants::MMIO_BASE + 0xFFF);
                memory_->enablePaging(true);
                cpu_->loadProgram(loaded_instructions_, loaded_start_address_);
            }
            break;
        case LoadedProgramKind::BINARY:
            if (!loaded_instructions_.empty()) {
                cpu_->loadBinary(loaded_instructions_, loaded_start_address_);
            }
            break;
        case LoadedProgramKind::ELF:
            if (!loaded_elf_.empty()) {
                cpu_->loadElf(loaded_elf_);
            }
            break;
        case LoadedProgramKind::NONE:
        default:
            break;
    }
}

void Simulator::step() {
    cpu_->step();
}

void Simulator::stepInstruction() {
    const uint64 start_count = cpu_->getStats().instruction_count;
    for (int i = 0; i < 32; ++i) {
        cpu_->step();
        if (cpu_->getState() == CpuState::HALTED) break;
        if (cpu_->getStats().instruction_count > start_count) break;
    }
}

void Simulator::loadProgram(const std::vector<uint32>& program, uint32 start_address) {
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

void Simulator::loadBinary(const std::vector<uint32>& program, uint32 start_address) {
    loaded_program_kind_ = LoadedProgramKind::BINARY;
    loaded_instructions_ = program;
    loaded_start_address_ = start_address;
    loaded_elf_.clear();
    suppress_reload_on_reset_ = true;
    reset();
    suppress_reload_on_reset_ = false;
    cpu_->loadBinary(program, start_address);
}

bool Simulator::loadElf(const std::vector<uint8>& elf_data) {
    loaded_program_kind_ = LoadedProgramKind::ELF;
    loaded_elf_ = elf_data;
    loaded_instructions_.clear();
    loaded_start_address_ = 0;
    suppress_reload_on_reset_ = true;
    reset();
    suppress_reload_on_reset_ = false;
    return cpu_->loadElf(elf_data);
}

SimulatorState Simulator::getState() const {
    return cpu_->getSimulatorState();
}

std::string Simulator::toJson() const {
    return cpu_->toJson();
}

} // namespace mycpu
