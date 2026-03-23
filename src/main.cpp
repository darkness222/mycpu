#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "assembler/Assembler.h"
#include "bus/Bus.h"
#include "cpu/CPU.h"
#include "devices/Device.h"
#include "elf/ElfLoader.h"
#include "memory/Memory.h"
#include "rpc/RpcServer.h"

using namespace mycpu;

namespace {

constexpr uint16 kDefaultServerPort = 18080;

void printUsage() {
    std::cout << "Usage:\n"
              << "  myCPU\n"
              << "  myCPU --server [--port N]\n"
              << "  myCPU --elf <file> [--max-cycles N]\n";
}

std::vector<uint8> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8> data(static_cast<size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(data.data()), size);
    }
    return data;
}

void printTraceTail(const CPU& cpu, size_t max_lines = 40) {
    const auto& trace = cpu.getTrace();
    const size_t start = (trace.size() > max_lines) ? (trace.size() - max_lines) : 0;
    std::cout << "Trace tail:" << std::endl;
    for (size_t i = start; i < trace.size(); ++i) {
        std::cout << "  " << trace[i] << std::endl;
    }
}

void printDebugState(CPU& cpu) {
    std::cout << "PC: 0x" << std::hex << cpu.getPC() << std::dec << std::endl;
    std::cout << "gp(x3): " << cpu.getRegisterFile().read(3) << std::endl;
    std::cout << "a0(x10): " << cpu.getRegisterFile().read(10) << std::endl;
    std::cout << "a7(x17): " << cpu.getRegisterFile().read(17) << std::endl;
    std::cout << "mstatus: 0x" << std::hex << cpu.getCsr().getMstatus() << std::endl;
    std::cout << "mtvec: 0x" << cpu.getCsr().getMtvec() << std::endl;
    std::cout << "mepc: 0x" << cpu.getCsr().getMepc() << std::endl;
    std::cout << "mcause: 0x" << cpu.getCsr().getMcause() << std::endl;
    std::cout << "mtval: 0x" << cpu.getCsr().getMtval() << std::dec << std::endl;
}

void printSpecialTrace(const CPU& cpu) {
    const auto& trace = cpu.getTrace();
    std::cout << "Special trace:" << std::endl;
    for (const auto& line : trace) {
        if (line.find("[WARN]") != std::string::npos ||
            line.find("[CSR]") != std::string::npos ||
            line.find("mret") != std::string::npos ||
            line.find("[TRAP]") != std::string::npos) {
            std::cout << "  " << line << std::endl;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::cout << "====================================" << std::endl;
    std::cout << "  myCPU RISC-V Simulator v1.0" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << std::endl;

    bool server_mode = false;
    bool elf_mode = false;
    std::string elf_path;
    uint64 max_cycles = 200000;
    uint16 server_port = kDefaultServerPort;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server" || arg == "-s") {
            server_mode = true;
        } else if (arg == "--elf" && i + 1 < argc) {
            elf_mode = true;
            elf_path = argv[++i];
        } else if (arg == "--max-cycles" && i + 1 < argc) {
            max_cycles = std::stoull(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            uint32 parsed_port = static_cast<uint32>(std::stoul(argv[++i]));
            if (parsed_port > 65535) {
                std::cerr << "Invalid port: " << parsed_port << std::endl;
                return 1;
            }
            server_port = static_cast<uint16>(parsed_port);
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    auto memory = std::make_shared<Memory>();
    auto bus = std::make_shared<Bus>();
    auto cpu = std::make_shared<CPU>();

    cpu->setMemory(memory);
    cpu->setBus(bus);

    auto uart = std::make_shared<UARTDevice>();
    auto timer = std::make_shared<TimerDevice>();
    auto interrupt_controller = std::make_shared<InterruptControllerDevice>();

    bus->connectDevice(constants::UART_BASE, uart);
    bus->connectDevice(constants::TIMER_BASE, timer);
    bus->connectDevice(constants::INTERRUPT_BASE, interrupt_controller);

    cpu->reset();

    if (server_mode) {
        auto simulator = std::make_shared<Simulator>();
        RpcServer server(server_port);
        server.setSimulator(simulator);
        server.start();

        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 0;
    }

    if (elf_mode) {
        auto elf_data = readBinaryFile(elf_path);
        if (elf_data.empty()) {
            std::cerr << "Failed to read ELF file: " << elf_path << std::endl;
            return 1;
        }

        if (!cpu->loadElf(elf_data)) {
            std::cerr << "Failed to load ELF file: " << elf_path << std::endl;
            return 1;
        }

        cpu->run(max_cycles);

        std::cout << "ELF: " << elf_path << std::endl;
        std::cout << "Cycles: " << cpu->getStats().cycle_count << std::endl;
        std::cout << "Instructions: " << cpu->getStats().instruction_count << std::endl;
        std::cout << "tohost: 0x" << std::hex << cpu->getTohostAddress() << std::dec << std::endl;

        if (cpu->didTestFinish()) {
            std::cout << (cpu->didTestPass() ? "PASS" : "FAIL")
                      << " (tohost=0x" << std::hex << cpu->getTestCode() << std::dec << ")"
                      << std::endl;
            return cpu->didTestPass() ? 0 : 2;
        }

        if (cpu->getState() == CpuState::HALTED) {
            std::cout << "HALTED without official PASS/FAIL signal" << std::endl;
            printDebugState(*cpu);
            printSpecialTrace(*cpu);
            printTraceTail(*cpu);
            return 3;
        }

        std::cout << "TIMEOUT after " << max_cycles << " cycles" << std::endl;
        printDebugState(*cpu);
        printSpecialTrace(*cpu);
        printTraceTail(*cpu);
        return 4;
    }

    std::vector<uint32> demo_program = {
        0x00500513,
        0x00700593,
        0x00B585B3,
        0x00000013,
        0x00000013,
        0x000000FF
    };

    cpu->loadProgram(demo_program, 0x00000000);
    cpu->run(10);

    std::cout << "Demo finished after " << cpu->getStats().cycle_count << " cycles" << std::endl;
    return 0;
}
