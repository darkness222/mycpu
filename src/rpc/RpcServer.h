#pragma once

#include "../include/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace mycpu {

// 前向声明
class Simulator;
class Memory;
class Bus;
class CPU;

class RpcServer {
public:
    RpcServer(uint16 port = 18080);
    ~RpcServer();

    void setSimulator(std::shared_ptr<Simulator> simulator);
    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    void handleRequest(const std::string& request, std::string& response);
    void handleGetState(std::string& response);
    void handleStep(std::string& response);
    void handleStepInstruction(std::string& response);
    void handleReset(std::string& response);
    void handleLoadProgram(const std::string& request, std::string& response);
    void handleAssemble(const std::string& request, std::string& response);
    void handleGetInstructions(std::string& response);
    void handleLoadElf(const std::string& request, std::string& response);
    void handleLoadBinary(const std::string& request, std::string& response);

    std::string escapeJson(const std::string& str);

    uint16 port_;
    bool running_;
    std::shared_ptr<Simulator> simulator_;
};

class Simulator {
public:
    Simulator();

    void reset();
    void step();
    void stepInstruction();
    void loadProgram(const std::vector<uint32>& program, uint32 start_address = 0);
    void loadBinary(const std::vector<uint32>& program, uint32 start_address = 0);
    bool loadElf(const std::vector<uint8>& elf_data);

    SimulatorState getState() const;
    std::vector<uint32> getLoadedInstructions() const { return loaded_instructions_; }
    std::string toJson() const;

private:
    enum class LoadedProgramKind {
        NONE,
        ASSEMBLY,
        BINARY,
        ELF
    };

    std::shared_ptr<Memory> memory_;
    std::shared_ptr<Bus> bus_;
    std::shared_ptr<CPU> cpu_;
    std::vector<uint32> loaded_instructions_;
    std::vector<uint8> loaded_elf_;
    uint32 loaded_start_address_ = 0;
    LoadedProgramKind loaded_program_kind_ = LoadedProgramKind::NONE;
    bool suppress_reload_on_reset_ = false;
};

} // namespace mycpu
