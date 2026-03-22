#pragma once

#include "../include/Types.h"
#include "RegisterFile.h"
#include "Decoder.h"
#include "CsrFile.h"
#include "TrapHandler.h"
#include <memory>
#include <vector>
#include <string>

namespace mycpu {

class Memory;
class Bus;
class Device;

// ===== CPU 执行模式 =====
enum class ExecMode : uint8_t {
    ASSEMBLY = 0,  // 教学汇编模式 (li/add/halt)
    BINARY = 1,    // 真实 RISC-V 二进制 (机器码)
    ELF = 2        // ELF 文件加载模式
};

class CPU {
public:
    CPU();
    ~CPU();

    void reset();
    void step();
    void run(uint64 cycles);
    void loadProgram(const std::vector<uint32>& program, uint32 start_address = 0);

    // ===== ELF 加载接口 =====
    bool loadElf(const std::vector<uint8>& elf_data);
    bool loadBinary(const std::vector<uint32>& program, uint32 start_address = 0x00000000);

    // ===== 执行模式 =====
    void setExecMode(ExecMode mode) { exec_mode_ = mode; }
    ExecMode getExecMode() const { return exec_mode_; }
    bool didTestFinish() const { return test_finished_; }
    bool didTestPass() const { return test_passed_; }
    uint32 getTestCode() const { return test_code_; }
    uint32 getTohostAddress() const { return tohost_address_; }

    // ===== CSR / Trap 接口 =====
    CsrFile& getCsr() { return csr_; }
    TrapHandler& getTrapHandler() { return trap_handler_; }

    // 状态访问
    uint32 getPC() const { return pc_; }
    CpuState getState() const { return state_; }
    PipelineStage getCurrentStage() const { return current_stage_; }
    const RegisterFile& getRegisterFile() const { return registers_; }
    const CpuStats& getStats() const { return stats_; }
    const HazardSignals& getHazardSignals() const { return hazard_signals_; }
    const PeripheralState& getPeripherals() const { return peripherals_; }
    const PipelineRegisters& getPipelineRegisters() const { return pipeline_regs_; }
    const std::vector<std::string>& getTrace() const { return trace_; }

    void setMemory(std::shared_ptr<Memory> memory) { memory_ = memory; trap_handler_.setMemory(memory); }
    void setBus(std::shared_ptr<Bus> bus) { bus_ = bus; trap_handler_.setBus(bus); }

    SimulatorState getSimulatorState() const;
    std::string toJson() const;

private:
    void fetch();
    void decode();
    void execute();
    void memoryAccess();
    void writeback();

    void detectHazards();
    void handleForwarding();
    void handleStall();
    void handleFlush();

    // ===== Trap 相关 =====
    bool checkAndTakeInterrupt();
    void handleTrap(const TrapInfo& trap);
    void handleEcallSyscall();
    void handleEbreak();
    void handleMret();

    // ===== CSR 指令执行 =====
    void executeCsrInstruction(const Instruction& instr);

    int32 alu(int32 operand1, int32 operand2, uint8 funct3, bool is_sub = false);

    uint32 pc_;
    CpuState state_;
    PipelineStage current_stage_;
    ExecMode exec_mode_;

    RegisterFile registers_;
    Decoder decoder_;
    CsrFile csr_;
    TrapHandler trap_handler_;

    PipelineRegisters pipeline_regs_;

    CpuStats stats_;
    HazardSignals hazard_signals_;
    PeripheralState peripherals_;

    std::shared_ptr<Memory> memory_;
    std::shared_ptr<Bus> bus_;

    std::vector<std::string> trace_;

    uint32 fetch_instruction_;
    bool branch_taken_;
    uint32 branch_target_;
    bool test_finished_;
    bool test_passed_;
    uint32 test_code_;
    uint32 tohost_address_;
};

} // namespace mycpu
