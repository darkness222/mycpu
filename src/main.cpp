#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#include "rpc/RpcServer.h"
#include "assembler/Assembler.h"
#include "cpu/CPU.h"
#include "memory/Memory.h"
#include "bus/Bus.h"
#include "devices/Device.h"
#include "elf/ElfLoader.h"

using namespace mycpu;

int main(int argc, char* argv[]) {
    std::cout << "====================================" << std::endl;
    std::cout << "  myCPU RISC-V Simulator v1.0" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << std::endl;

    // 检查是否以服务器模式运行
    bool server_mode = false;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--server" || arg == "-s") {
            server_mode = true;
        }
    }

    auto memory = std::make_shared<Memory>();
    auto bus = std::make_shared<Bus>();
    auto cpu = std::make_shared<CPU>();

    cpu->setMemory(memory);
    cpu->setBus(bus);

    auto uart = std::make_shared<UARTDevice>();
    auto timer = std::make_shared<TimerDevice>();

    bus->connectDevice(constants::UART_BASE, uart);
    bus->connectDevice(constants::TIMER_BASE, timer);

    cpu->reset();

    if (server_mode) {
        // 服务器模式：启动 RPC 服务器
        std::cout << "启动 RPC 服务器模式..." << std::endl;
        std::cout << "监听端口: 8080" << std::endl;
        std::cout << "按 Ctrl+C 停止服务器" << std::endl;
        std::cout << std::endl;

        auto simulator = std::make_shared<Simulator>();
        RpcServer server(8080);
        server.setSimulator(simulator);
        server.start();

        // 保持运行
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        // 演示模式：运行示例程序
        std::vector<uint32> demo_program = {
            0x00500513,  // li x10, 5
            0x00700593,  // li x11, 7
            0x00B585B3,  // add x11, x10, x11
            0x00000013,  // nop
            0x00000013,  // nop
            0x000000FF   // halt
        };

        cpu->loadProgram(demo_program, 0x00000000);

        std::cout << "Demo program loaded. Running 10 cycles..." << std::endl;
        std::cout << std::endl;
        std::cout << "Initial PC: 0x" << std::hex << cpu->getPC() << std::dec << std::endl;
        std::cout << std::endl;

        cpu->run(10);

        std::cout << "After 10 cycles:" << std::endl;
        std::cout << "PC: 0x" << std::hex << cpu->getPC() << std::dec << std::endl;
        std::cout << "Cycles: " << cpu->getStats().cycle_count << std::endl;
        std::cout << "Instructions: " << cpu->getStats().instruction_count << std::endl;
        std::cout << "State: " << (cpu->getState() == CpuState::HALTED ? "HALTED" : "RUNNING") << std::endl;
        std::cout << std::endl;

        std::cout << "Execution trace:" << std::endl;
        for (const auto& trace : cpu->getTrace()) {
            std::cout << "  " << trace << std::endl;
        }
    }

    return 0;
}
