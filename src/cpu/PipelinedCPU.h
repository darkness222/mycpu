#pragma once

#include "CpuCore.h"
#include "RegisterFile.h"
#include "Decoder.h"
#include "CsrFile.h"
#include "TrapHandler.h"
#include "../memory/Memory.h"
#include <memory>
#include <string>
#include <vector>

namespace mycpu {

class Bus;

class PipelinedCPU : public CpuCore {
public:
    PipelinedCPU();
    ~PipelinedCPU() override = default;

    void reset() override;
    void step() override;
    void run(uint64 cycles) override;
    void loadProgram(const std::vector<uint32>& program, uint32 start_address = 0) override;
    bool loadElf(const std::vector<uint8>& elf_data) override;
    bool loadBinary(const std::vector<uint32>& program, uint32 start_address = 0x00000000) override;

    void setMemory(std::shared_ptr<Memory> memory) override;
    void setBus(std::shared_ptr<Bus> bus) override;

    SimulationMode getSimulationMode() const override { return SimulationMode::PIPELINED; }
    std::string getCoreName() const override { return "Pipelined CPU"; }
    bool supportsTrueOverlapPipeline() const override { return true; }

    CsrFile& getCsr() override { return csr_; }
    TrapHandler& getTrapHandler() override { return trap_handler_; }
    uint32 getPC() const override { return pc_; }
    CpuState getState() const override { return state_; }
    PipelineStage getCurrentStage() const override { return current_stage_; }
    const RegisterFile& getRegisterFile() const override { return registers_; }
    const CpuStats& getStats() const override { return stats_; }
    const HazardSignals& getHazardSignals() const override { return hazard_signals_; }
    const PeripheralState& getPeripherals() const override { return peripherals_; }
    const PipelineRegisters& getPipelineRegisters() const override { return pipeline_regs_; }
    const std::vector<std::string>& getTrace() const override { return trace_; }
    bool didTestFinish() const override { return test_finished_; }
    bool didTestPass() const override { return test_passed_; }
    uint32 getTestCode() const override { return test_code_; }
    uint32 getTohostAddress() const override { return tohost_address_; }

    SimulatorState getSimulatorState() const override;
    std::string toJson() const override;

private:
    struct StageControl {
        bool reg_write = false;
        bool mem_read = false;
        bool mem_write = false;
        bool halt = false;
    };

    int32 alu(int32 operand1, int32 operand2, uint8 funct3, bool is_sub = false) const;
    bool instructionWritesRegister(const Instruction& instr) const;
    bool instructionUsesRs1(const Instruction& instr) const;
    bool instructionUsesRs2(const Instruction& instr) const;
    int32 getForwardedOperand(uint8 reg, int32 fallback, const PipelineRegisters& old_regs, bool& forwarded_from_exmem, bool& forwarded_from_memwb) const;
    int32 getMemWbWriteValue(const PipelineRegisters& regs) const;
    StageControl buildControl(const Instruction& instr) const;
    void pushTrace(const std::string& line);
    void trimTrace();
    void syncPeripheralState();
    bool isPipelineEmpty(const PipelineRegisters& regs) const;
    bool resolveMemoryAccess(uint32 addr, MemoryAccessType access, uint8 size, uint32 pc, uint32& translated_addr);
    void handleTrap(const TrapInfo& trap);
    bool checkAndTakeInterrupt();
    void handleMret();
    void handleEcall();
    void handleEbreak();
    int32 executeCsrInstruction(const Instruction& instr, int32 operand1);

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
    bool fetch_stopped_;
    bool fetch_view_valid_;
    uint32 fetch_view_pc_;
    std::string fetch_view_text_;
    bool decode_view_valid_;
    uint32 decode_view_pc_;
    std::string decode_view_text_;
    bool execute_view_valid_;
    uint32 execute_view_pc_;
    std::string execute_view_text_;
    bool memory_view_valid_;
    uint32 memory_view_pc_;
    std::string memory_view_text_;
    bool writeback_view_valid_;
    uint32 writeback_view_pc_;
    std::string writeback_view_text_;
    bool test_finished_;
    bool test_passed_;
    uint32 test_code_;
    uint32 tohost_address_;
};

} // namespace mycpu
