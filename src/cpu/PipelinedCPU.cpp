#include "PipelinedCPU.h"
#include "../bus/Bus.h"
#include "../include/Constants.h"
#include "../elf/ElfLoader.h"
#include <algorithm>
#include <sstream>

namespace mycpu {

namespace {
bool isMmioAddress(uint32 addr) {
    return addr >= constants::MMIO_BASE && addr < (constants::MMIO_BASE + 0x1000);
}
}

PipelinedCPU::PipelinedCPU()
    : pc_(0),
      state_(CpuState::HALTED),
      current_stage_(PipelineStage::FETCH),
      exec_mode_(ExecMode::ASSEMBLY),
      fetch_stopped_(false),
      fetch_view_valid_(false),
      fetch_view_pc_(0),
      decode_view_valid_(false),
      decode_view_pc_(0),
      execute_view_valid_(false),
      execute_view_pc_(0),
      memory_view_valid_(false),
      memory_view_pc_(0),
      writeback_view_valid_(false),
      writeback_view_pc_(0),
      test_finished_(false),
      test_passed_(false),
      test_code_(0),
      tohost_address_(0) {
    registers_.reset();
    trap_handler_.setCsr(&csr_);
}

void PipelinedCPU::reset() {
    trap_handler_.setCsr(&csr_);
    pc_ = constants::RESET_VECTOR;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;
    exec_mode_ = ExecMode::ASSEMBLY;
    registers_.reset();
    pipeline_regs_ = PipelineRegisters();
    stats_ = CpuStats();
    hazard_signals_ = HazardSignals();
    peripherals_ = PeripheralState();
    trace_.clear();
    fetch_stopped_ = false;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    decode_view_valid_ = false;
    decode_view_pc_ = 0;
    decode_view_text_.clear();
    execute_view_valid_ = false;
    execute_view_pc_ = 0;
    execute_view_text_.clear();
    memory_view_valid_ = false;
    memory_view_pc_ = 0;
    memory_view_text_.clear();
    writeback_view_valid_ = false;
    writeback_view_pc_ = 0;
    writeback_view_text_.clear();
    test_finished_ = false;
    test_passed_ = false;
    test_code_ = 0;
    tohost_address_ = 0;

    csr_.setPrivilegeMode(PrivilegeMode::MACHINE);
    csr_.write(CSR::MSTATUS, (static_cast<uint32>(PrivilegeMode::MACHINE) << 11));
    csr_.write(CSR::MTVEC, constants::TRAP_VECTOR);
    csr_.write(CSR::MIE, (1u << 3) | (1u << 7) | (1u << 11));
    csr_.enableInterrupts();

    pushTrace("[PIPE] reset - ready to execute");
}

void PipelinedCPU::setMemory(std::shared_ptr<Memory> memory) {
    memory_ = memory;
    trap_handler_.setMemory(memory);
}

void PipelinedCPU::setBus(std::shared_ptr<Bus> bus) {
    bus_ = bus;
    trap_handler_.setBus(bus);
}

void PipelinedCPU::loadProgram(const std::vector<uint32>& program, uint32 start_address) {
    if (!memory_) {
        return;
    }

    for (size_t i = 0; i < program.size(); ++i) {
        memory_->writeWord(start_address + static_cast<uint32>(i * 4), program[i]);
    }

    pc_ = start_address;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;
    exec_mode_ = ExecMode::ASSEMBLY;
    fetch_stopped_ = false;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    decode_view_valid_ = false;
    decode_view_pc_ = 0;
    decode_view_text_.clear();
    execute_view_valid_ = false;
    execute_view_pc_ = 0;
    execute_view_text_.clear();
    memory_view_valid_ = false;
    memory_view_pc_ = 0;
    memory_view_text_.clear();
    writeback_view_valid_ = false;
    writeback_view_pc_ = 0;
    writeback_view_text_.clear();
    pipeline_regs_ = PipelineRegisters();

    std::ostringstream oss;
    oss << "[PIPE] program loaded at 0x" << std::hex << start_address
        << " (" << std::dec << program.size() << " instructions)";
    pushTrace(oss.str());
}

bool PipelinedCPU::loadBinary(const std::vector<uint32>& program, uint32 start_address) {
    if (!memory_) {
        return false;
    }

    for (size_t i = 0; i < program.size(); ++i) {
        memory_->writeWord(start_address + static_cast<uint32>(i * 4), program[i]);
    }

    pc_ = start_address;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;
    exec_mode_ = ExecMode::BINARY;
    fetch_stopped_ = false;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    decode_view_valid_ = false;
    decode_view_pc_ = 0;
    decode_view_text_.clear();
    execute_view_valid_ = false;
    execute_view_pc_ = 0;
    execute_view_text_.clear();
    memory_view_valid_ = false;
    memory_view_pc_ = 0;
    memory_view_text_.clear();
    writeback_view_valid_ = false;
    writeback_view_pc_ = 0;
    writeback_view_text_.clear();
    pipeline_regs_ = PipelineRegisters();
    registers_.write(constants::REG_SP, constants::DEFAULT_STACK_POINTER);
    pushTrace("[PIPE] binary loaded");
    return true;
}

bool PipelinedCPU::loadElf(const std::vector<uint8>& elf_data) {
    if (!memory_) {
        return false;
    }

    ElfLoader loader;
    if (!loader.loadFromBytes(elf_data)) {
        pushTrace("[PIPE] failed to parse ELF");
        return false;
    }

    auto result = loader.loadToMemory(memory_, bus_);
    if (!result.success) {
        pushTrace("[PIPE] failed to load ELF segments");
        return false;
    }

    pc_ = result.entry_point;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;
    exec_mode_ = ExecMode::ELF;
    fetch_stopped_ = false;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    decode_view_valid_ = false;
    decode_view_pc_ = 0;
    decode_view_text_.clear();
    execute_view_valid_ = false;
    execute_view_pc_ = 0;
    execute_view_text_.clear();
    memory_view_valid_ = false;
    memory_view_pc_ = 0;
    memory_view_text_.clear();
    writeback_view_valid_ = false;
    writeback_view_pc_ = 0;
    writeback_view_text_.clear();
    pipeline_regs_ = PipelineRegisters();
    tohost_address_ = loader.getSectionAddress(".tohost");
    if (tohost_address_ == 0) {
        tohost_address_ = constants::RISCV_TEST_TOHOST_FALLBACK;
    }
    registers_.write(constants::REG_SP, constants::DEFAULT_STACK_POINTER);

    std::ostringstream oss;
    oss << "[PIPE] ELF loaded: entry=0x" << std::hex << result.entry_point;
    pushTrace(oss.str());
    return true;
}

void PipelinedCPU::run(uint64 cycles) {
    for (uint64 i = 0; i < cycles && state_ != CpuState::HALTED; ++i) {
        step();
    }
}

PipelinedCPU::StageControl PipelinedCPU::buildControl(const Instruction& instr) const {
    StageControl control;
    switch (instr.opcode) {
        case Opcode::LUI:
        case Opcode::AUIPC:
        case Opcode::OP_IMM:
        case Opcode::OP:
        case Opcode::JAL:
        case Opcode::JALR:
            control.reg_write = true;
            break;
        case Opcode::LOAD:
            control.reg_write = true;
            control.mem_read = true;
            break;
        case Opcode::STORE:
            control.mem_write = true;
            break;
        case Opcode::SYSTEM:
            if (instr.funct3 != 0 && instr.rd != 0) {
                control.reg_write = true;
            }
            break;
        case Opcode::HALT:
            control.halt = true;
            break;
        default:
            break;
    }
    return control;
}

bool PipelinedCPU::instructionWritesRegister(const Instruction& instr) const {
    return buildControl(instr).reg_write && instr.rd != 0;
}

bool PipelinedCPU::instructionUsesRs1(const Instruction& instr) const {
    switch (instr.opcode) {
        case Opcode::LUI:
        case Opcode::AUIPC:
        case Opcode::JAL:
        case Opcode::HALT:
            return false;
        default:
            return true;
    }
}

bool PipelinedCPU::instructionUsesRs2(const Instruction& instr) const {
    switch (instr.opcode) {
        case Opcode::OP:
        case Opcode::STORE:
        case Opcode::BRANCH:
            return true;
        default:
            return false;
    }
}

int32 PipelinedCPU::getMemWbWriteValue(const PipelineRegisters& regs) const {
    if (!regs.mem_wb_valid) {
        return 0;
    }
    return regs.mem_wb_instruction.opcode == Opcode::LOAD
        ? regs.mem_wb_mem_data
        : regs.mem_wb_alu_result;
}

int32 PipelinedCPU::getForwardedOperand(uint8 reg, int32 fallback, const PipelineRegisters& old_regs,
                                        bool& forwarded_from_exmem, bool& forwarded_from_memwb) const {
    forwarded_from_exmem = false;
    forwarded_from_memwb = false;

    if (reg == 0) {
        return 0;
    }

    if (old_regs.ex_mem_valid &&
        old_regs.ex_mem_instruction.rd == reg &&
        instructionWritesRegister(old_regs.ex_mem_instruction) &&
        old_regs.ex_mem_instruction.opcode != Opcode::LOAD) {
        forwarded_from_exmem = true;
        return old_regs.ex_mem_alu_result;
    }

    if (old_regs.mem_wb_valid &&
        old_regs.mem_wb_instruction.rd == reg &&
        old_regs.mem_wb_reg_write) {
        forwarded_from_memwb = true;
        return getMemWbWriteValue(old_regs);
    }

    return fallback;
}

int32 PipelinedCPU::alu(int32 operand1, int32 operand2, uint8 funct3, bool is_sub) const {
    switch (funct3) {
        case 0x0: return is_sub ? (operand1 - operand2) : (operand1 + operand2);
        case 0x1: return operand1 << (operand2 & 0x1F);
        case 0x2: return (operand1 < operand2) ? 1 : 0;
        case 0x3: return (static_cast<uint32>(operand1) < static_cast<uint32>(operand2)) ? 1 : 0;
        case 0x4: return operand1 ^ operand2;
        case 0x5:
            return is_sub
                ? (operand1 >> (operand2 & 0x1F))
                : static_cast<int32>(static_cast<uint32>(operand1) >> (operand2 & 0x1F));
        case 0x6: return operand1 | operand2;
        case 0x7: return operand1 & operand2;
        default: return 0;
    }
}

void PipelinedCPU::step() {
    if (state_ == CpuState::HALTED) {
        return;
    }

    if (!memory_) {
        state_ = CpuState::HALTED;
        pushTrace("[PIPE] halt - memory not connected");
        return;
    }

    if (state_ == CpuState::RUNNING && checkAndTakeInterrupt()) {
        return;
    }

    stats_.cycle_count++;
    csr_.incrementCycle();
    if (bus_) {
        bus_->tick();
    }
    syncPeripheralState();

    const PipelineRegisters old_regs = pipeline_regs_;
    PipelineRegisters next_regs = old_regs;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    decode_view_valid_ = old_regs.if_id_valid;
    decode_view_pc_ = old_regs.if_id_pc;
    decode_view_text_ = old_regs.if_id_valid ? decoder_.disassemble(old_regs.if_id_instruction) : "";
    execute_view_valid_ = old_regs.id_ex_valid;
    execute_view_pc_ = old_regs.id_ex_pc;
    execute_view_text_ = old_regs.id_ex_valid ? decoder_.disassemble(old_regs.id_ex_instruction) : "";
    memory_view_valid_ = old_regs.ex_mem_valid;
    memory_view_pc_ = old_regs.ex_mem_pc;
    memory_view_text_ = old_regs.ex_mem_valid ? decoder_.disassemble(old_regs.ex_mem_instruction) : "";
    writeback_view_valid_ = old_regs.mem_wb_valid;
    writeback_view_pc_ = old_regs.mem_wb_pc;
    writeback_view_text_ = old_regs.mem_wb_valid ? decoder_.disassemble(old_regs.mem_wb_instruction) : "";

    hazard_signals_ = HazardSignals();
    bool stall_fetch = false;
    bool flush_decode = false;
    bool halt_requested = false;
    bool abort_cycle = false;

    if (old_regs.mem_wb_valid) {
        const Instruction& instr = old_regs.mem_wb_instruction;
        if (old_regs.mem_wb_reg_write && instr.rd != 0) {
            const int32 value = getMemWbWriteValue(old_regs);
            registers_.write(instr.rd, value);
            std::ostringstream oss;
            oss << "[WB] x" << static_cast<int>(instr.rd) << " <- " << value;
            pushTrace(oss.str());
        }
        stats_.instruction_count++;
        csr_.incrementInstret();
    }
    next_regs.mem_wb_valid = false;

    if (old_regs.ex_mem_valid) {
        const Instruction& instr = old_regs.ex_mem_instruction;
        const StageControl control = buildControl(instr);
        next_regs.mem_wb_pc = old_regs.ex_mem_pc;
        next_regs.mem_wb_instruction = instr;
        next_regs.mem_wb_alu_result = old_regs.ex_mem_alu_result;
        next_regs.mem_wb_mem_data = 0;
        next_regs.mem_wb_reg_write = control.reg_write;
        next_regs.mem_wb_valid = true;

        const uint32 addr = static_cast<uint32>(old_regs.ex_mem_alu_result);
        if (control.mem_read) {
            uint32 translated_addr = 0;
            uint8 access_size = 4;
            if (instr.funct3 == constants::Funct3::LB || instr.funct3 == constants::Funct3::LBU) access_size = 1;
            else if (instr.funct3 == constants::Funct3::LH || instr.funct3 == constants::Funct3::LHU) access_size = 2;
            if (!resolveMemoryAccess(addr, MemoryAccessType::LOAD, access_size, old_regs.ex_mem_pc, translated_addr)) {
                return;
            }
            switch (instr.funct3) {
                case constants::Funct3::LB:
                    next_regs.mem_wb_mem_data = (isMmioAddress(translated_addr) && bus_)
                        ? static_cast<int32>(static_cast<int8>(bus_->read(translated_addr, 1)))
                        : static_cast<int32>(static_cast<int8>(memory_->readByte(translated_addr)));
                    break;
                case constants::Funct3::LH:
                    next_regs.mem_wb_mem_data = (isMmioAddress(translated_addr) && bus_)
                        ? static_cast<int32>(static_cast<int16>(bus_->read(translated_addr, 2)))
                        : static_cast<int32>(static_cast<int16>(memory_->readHalfWord(translated_addr)));
                    break;
                case constants::Funct3::LHU:
                    next_regs.mem_wb_mem_data = (isMmioAddress(translated_addr) && bus_)
                        ? static_cast<int32>(bus_->read(translated_addr, 2))
                        : static_cast<int32>(memory_->readHalfWord(translated_addr));
                    break;
                case constants::Funct3::LBU:
                    next_regs.mem_wb_mem_data = (isMmioAddress(translated_addr) && bus_)
                        ? static_cast<int32>(bus_->read(translated_addr, 1))
                        : static_cast<int32>(memory_->readByte(translated_addr));
                    break;
                case constants::Funct3::LW:
                default:
                    next_regs.mem_wb_mem_data = (isMmioAddress(translated_addr) && bus_)
                        ? static_cast<int32>(bus_->read(translated_addr, 4))
                        : static_cast<int32>(memory_->readWord(translated_addr));
                    break;
            }
        }

        if (control.mem_write) {
            const int32 write_data = old_regs.ex_mem_mem_write_data;
            uint32 translated_addr = 0;
            uint8 access_size = 4;
            if (instr.funct3 == constants::Funct3::SB) access_size = 1;
            else if (instr.funct3 == constants::Funct3::SH) access_size = 2;
            if (!resolveMemoryAccess(addr, MemoryAccessType::STORE, access_size, old_regs.ex_mem_pc, translated_addr)) {
                return;
            }
            switch (instr.funct3) {
                case constants::Funct3::SB:
                    if (isMmioAddress(translated_addr) && bus_) bus_->write(translated_addr, static_cast<uint32>(write_data & 0xFF), 1);
                    else memory_->writeByte(translated_addr, static_cast<uint8>(write_data & 0xFF));
                    break;
                case constants::Funct3::SH:
                    if (isMmioAddress(translated_addr) && bus_) bus_->write(translated_addr, static_cast<uint32>(write_data & 0xFFFF), 2);
                    else memory_->writeHalfWord(translated_addr, static_cast<uint16>(write_data & 0xFFFF));
                    break;
                case constants::Funct3::SW:
                default:
                    if (isMmioAddress(translated_addr) && bus_) bus_->write(translated_addr, static_cast<uint32>(write_data), 4);
                    else memory_->writeWord(translated_addr, static_cast<uint32>(write_data));
                    break;
            }

            if (exec_mode_ == ExecMode::ELF &&
                tohost_address_ != 0 &&
                translated_addr == tohost_address_ &&
                static_cast<uint32>(write_data) != 0) {
                test_finished_ = true;
                test_code_ = static_cast<uint32>(write_data);
                test_passed_ = (test_code_ == 1);
                state_ = CpuState::HALTED;
                fetch_stopped_ = true;
                std::ostringstream oss;
                oss << "[TEST] tohost=0x" << std::hex << test_code_
                    << (test_passed_ ? " PASS" : " FAIL");
                pushTrace(oss.str());
            }
        }
    }
    next_regs.ex_mem_valid = false;

    if (old_regs.if_id_valid && old_regs.id_ex_valid) {
        const Instruction& decode_instr = old_regs.if_id_instruction;
        const Instruction& execute_instr = old_regs.id_ex_instruction;
        if (execute_instr.opcode == Opcode::LOAD &&
            execute_instr.rd != 0 &&
            ((instructionUsesRs1(decode_instr) && decode_instr.rs1 == execute_instr.rd) ||
             (instructionUsesRs2(decode_instr) && decode_instr.rs2 == execute_instr.rd))) {
            stall_fetch = true;
            hazard_signals_.stall = true;
            hazard_signals_.description = "Load-use hazard";
            stats_.stall_count++;
        }
    }

    if (old_regs.id_ex_valid) {
        const Instruction& instr = old_regs.id_ex_instruction;
        const StageControl control = buildControl(instr);
        bool exmem_forward_rs1 = false;
        bool memwb_forward_rs1 = false;
        bool exmem_forward_rs2 = false;
        bool memwb_forward_rs2 = false;

        int32 operand1 = getForwardedOperand(instr.rs1, old_regs.id_ex_reg_data1, old_regs, exmem_forward_rs1, memwb_forward_rs1);
        int32 operand2 = getForwardedOperand(instr.rs2, old_regs.id_ex_reg_data2, old_regs, exmem_forward_rs2, memwb_forward_rs2);

        if (exmem_forward_rs1 || exmem_forward_rs2) {
            hazard_signals_.forward_from_exmem = exmem_forward_rs1 ? instr.rs1 : instr.rs2;
        }
        if (memwb_forward_rs1 || memwb_forward_rs2) {
            hazard_signals_.forward_from_memwb = memwb_forward_rs1 ? instr.rs1 : instr.rs2;
        }

        int32 alu_result = 0;
        bool take_branch = false;
        uint32 branch_target = 0;

        switch (instr.opcode) {
            case Opcode::LUI:
                alu_result = instr.imm;
                break;
            case Opcode::AUIPC:
                alu_result = static_cast<int32>(old_regs.id_ex_pc + instr.imm);
                break;
            case Opcode::JAL:
                alu_result = static_cast<int32>(old_regs.id_ex_pc + 4);
                take_branch = true;
                branch_target = old_regs.id_ex_pc + instr.imm;
                break;
            case Opcode::JALR:
                alu_result = static_cast<int32>(old_regs.id_ex_pc + 4);
                take_branch = true;
                branch_target = static_cast<uint32>((operand1 + instr.imm) & ~1);
                break;
            case Opcode::OP_IMM:
                if (instr.funct3 == 0x5) {
                    const uint32 shamt = static_cast<uint32>(instr.imm) & 0x1F;
                    const bool arithmetic_shift = (instr.funct7 == 0x20);
                    alu_result = arithmetic_shift
                        ? (operand1 >> shamt)
                        : static_cast<int32>(static_cast<uint32>(operand1) >> shamt);
                } else {
                    alu_result = alu(operand1, instr.imm, instr.funct3);
                }
                break;
            case Opcode::OP:
                alu_result = alu(operand1, operand2, instr.funct3, instr.funct7 == 0x20);
                break;
            case Opcode::LOAD:
            case Opcode::STORE:
                alu_result = operand1 + instr.imm;
                break;
            case Opcode::BRANCH:
                switch (instr.funct3) {
                    case constants::Funct3::BEQ: take_branch = (operand1 == operand2); break;
                    case constants::Funct3::BNE: take_branch = (operand1 != operand2); break;
                    case constants::Funct3::BLT: take_branch = (operand1 < operand2); break;
                    case constants::Funct3::BGE: take_branch = (operand1 >= operand2); break;
                    case constants::Funct3::BLTU: take_branch = (static_cast<uint32>(operand1) < static_cast<uint32>(operand2)); break;
                    case constants::Funct3::BGEU: take_branch = (static_cast<uint32>(operand1) >= static_cast<uint32>(operand2)); break;
                    default: break;
                }
                branch_target = old_regs.id_ex_pc + instr.imm;
                if (take_branch) {
                    stats_.branch_taken++;
                } else {
                    stats_.branch_not_taken++;
                }
                break;
            case Opcode::FENCE:
                break;
            case Opcode::SYSTEM:
                if (instr.funct3 == 0) {
                    if (instr.raw == 0x30200073) {
                        handleMret();
                        abort_cycle = true;
                    } else if (instr.raw == 0x00000073) {
                        handleTrap(trap_handler_.handleEcall(old_regs.id_ex_pc));
                        abort_cycle = true;
                    } else if (instr.raw == 0x00100073) {
                        handleTrap(trap_handler_.handleBreakpoint(old_regs.id_ex_pc));
                        abort_cycle = true;
                    } else {
                        handleTrap(trap_handler_.handleIllegalInstruction(instr.raw, old_regs.id_ex_pc));
                        return;
                    }
                } else {
                    alu_result = executeCsrInstruction(instr, operand1);
                }
                break;
            case Opcode::HALT:
                halt_requested = true;
                fetch_stopped_ = true;
                flush_decode = true;
                break;
            default:
                break;
        }

        next_regs.ex_mem_pc = old_regs.id_ex_pc;
        next_regs.ex_mem_instruction = instr;
        next_regs.ex_mem_alu_result = alu_result;
        next_regs.ex_mem_mem_write_data = operand2;
        next_regs.ex_mem_mem_read = control.mem_read;
        next_regs.ex_mem_mem_write = control.mem_write;
        next_regs.ex_mem_valid = !control.halt;

        if ((instr.opcode == Opcode::JAL) || (instr.opcode == Opcode::JALR) || (instr.opcode == Opcode::BRANCH && take_branch)) {
            flush_decode = true;
            hazard_signals_.flush = true;
            pc_ = branch_target;
            stats_.flush_count++;
        }

        std::ostringstream oss;
        oss << "[EX] " << instr.disassembly;
        if (take_branch) {
            oss << " -> branch 0x" << std::hex << branch_target;
        } else if (control.mem_read || control.mem_write) {
            oss << " -> addr=0x" << std::hex << static_cast<uint32>(alu_result);
        } else {
            oss << " -> alu=" << std::dec << alu_result;
        }
        pushTrace(oss.str());
    }
    if (abort_cycle) {
        return;
    }
    next_regs.id_ex_valid = false;

    if (!flush_decode && !stall_fetch && old_regs.if_id_valid) {
        const Instruction& instr = old_regs.if_id_instruction;
        next_regs.id_ex_pc = old_regs.if_id_pc;
        next_regs.id_ex_instruction = instr;
        next_regs.id_ex_reg_data1 = registers_.read(instr.rs1);
        next_regs.id_ex_reg_data2 = registers_.read(instr.rs2);
        next_regs.id_ex_alu_result = 0;
        next_regs.id_ex_valid = true;
    }

    if (flush_decode) {
        next_regs.if_id_valid = false;
        next_regs.id_ex_valid = false;
    } else if (stall_fetch) {
        next_regs.if_id_valid = old_regs.if_id_valid;
    } else if (!fetch_stopped_) {
        const uint32 fetch_pc = pc_;
        if (exec_mode_ == ExecMode::ELF && fetch_pc < constants::RISCV_TEST_BASE) {
            pc_ = fetch_pc + constants::RISCV_TEST_BASE;
            pushTrace("[PIPE][WARN] PC dropped below official ELF base");
            pipeline_regs_ = next_regs;
            return;
        }
        uint32 translated_pc = 0;
        if (!resolveMemoryAccess(fetch_pc, MemoryAccessType::FETCH, 4, fetch_pc, translated_pc)) {
            return;
        }
        if (isMmioAddress(translated_pc)) {
            handleTrap(trap_handler_.handleInstructionAccessFault(fetch_pc));
            return;
        }
        const uint32 raw = memory_->readWord(translated_pc);
        const Instruction instr = decoder_.decode(raw, fetch_pc);
        fetch_view_valid_ = true;
        fetch_view_pc_ = fetch_pc;
        fetch_view_text_ = decoder_.disassemble(instr);
        next_regs.if_id_pc = fetch_pc;
        next_regs.if_id_instruction = instr;
        next_regs.if_id_valid = true;
        pc_ = fetch_pc + 4;
    } else {
        next_regs.if_id_valid = false;
    }

    if (halt_requested) {
        next_regs.if_id_valid = false;
        next_regs.id_ex_valid = false;
        fetch_stopped_ = true;
    }

    pipeline_regs_ = next_regs;
    current_stage_ = static_cast<PipelineStage>(stats_.cycle_count % 5);

    if (test_finished_ || (fetch_stopped_ && isPipelineEmpty(pipeline_regs_))) {
        state_ = CpuState::HALTED;
        pushTrace("[PIPE] halt - pipeline drained");
    } else {
        state_ = CpuState::RUNNING;
    }
}

bool PipelinedCPU::isPipelineEmpty(const PipelineRegisters& regs) const {
    return !regs.if_id_valid && !regs.id_ex_valid && !regs.ex_mem_valid && !regs.mem_wb_valid;
}

void PipelinedCPU::syncPeripheralState() {
    if (!bus_) {
        return;
    }

    peripherals_.timer_value = bus_->getTimerValue();
    peripherals_.uart_buffer = bus_->getUartBuffer();
    peripherals_.timer_interrupt = bus_->hasPendingTimerInterrupt();
    peripherals_.software_interrupt = bus_->hasPendingSoftwareInterrupt();
    peripherals_.external_interrupt = bus_->hasPendingExternalInterrupt();
    peripherals_.uart_interrupt = false;
    peripherals_.interrupt_pending_bits = bus_->getInterruptPendingBits();
    peripherals_.interrupt_enabled_bits = bus_->getInterruptEnabledBits();

    if (peripherals_.timer_interrupt) {
        trap_handler_.raiseInterrupt(Cause::MACHINE_TIMER_INTERRUPT);
    } else {
        trap_handler_.clearInterrupt(Cause::MACHINE_TIMER_INTERRUPT);
    }

    if (peripherals_.software_interrupt) {
        trap_handler_.raiseInterrupt(Cause::MACHINE_SOFTWARE_INTERRUPT);
    } else {
        trap_handler_.clearInterrupt(Cause::MACHINE_SOFTWARE_INTERRUPT);
    }

    if (peripherals_.external_interrupt) {
        trap_handler_.raiseInterrupt(Cause::MACHINE_EXTERNAL_INTERRUPT);
    } else {
        trap_handler_.clearInterrupt(Cause::MACHINE_EXTERNAL_INTERRUPT);
    }
}

void PipelinedCPU::pushTrace(const std::string& line) {
    trace_.push_back(line);
    trimTrace();
}

void PipelinedCPU::trimTrace() {
    constexpr size_t kMaxTrace = 200;
    if (trace_.size() > kMaxTrace) {
        trace_.erase(trace_.begin(), trace_.begin() + (trace_.size() - kMaxTrace));
    }
}

bool PipelinedCPU::resolveMemoryAccess(uint32 addr, MemoryAccessType access, uint8 size,
                                       uint32 pc, uint32& translated_addr) {
    if (!memory_) {
        return false;
    }

    if ((size == 2 && (addr & 0x1)) || (size == 4 && (addr & 0x3))) {
        if (access == MemoryAccessType::FETCH) {
            handleTrap(trap_handler_.handleInstructionAddressMisaligned(pc, addr));
            return false;
        }
    }

    bool page_fault = false;
    if (memory_->translateForAccess(addr, access, translated_addr, page_fault)) {
        return true;
    }

    TrapInfo trap;
    if (page_fault) {
        trap = (access == MemoryAccessType::STORE)
            ? trap_handler_.handleStorePageFault(addr, pc)
            : (access == MemoryAccessType::LOAD)
                ? trap_handler_.handleLoadPageFault(addr, pc)
                : trap_handler_.handleInstructionPageFault(addr, pc);
    } else {
        trap = (access == MemoryAccessType::STORE)
            ? trap_handler_.handleStoreAccessFault(addr, pc)
            : (access == MemoryAccessType::LOAD)
                ? trap_handler_.handleLoadAccessFault(addr, pc)
                : trap_handler_.handleInstructionAccessFault(pc);
    }
    handleTrap(trap);
    return false;
}

void PipelinedCPU::handleTrap(const TrapInfo& trap) {
    trap_handler_.enterTrap(trap);
    pc_ = trap_handler_.calculateTrapVector(trap.cause);
    pipeline_regs_ = PipelineRegisters();
    fetch_stopped_ = false;
    state_ = trap.is_interrupt ? CpuState::INTERRUPT : CpuState::EXCEPTION;
    std::ostringstream oss;
    oss << "[TRAP] cause=0x" << std::hex << trap.cause << " epc=0x" << trap.epc;
    pushTrace(oss.str());
}

bool PipelinedCPU::checkAndTakeInterrupt() {
    if (!trap_handler_.hasPendingInterrupt()) {
        return false;
    }
    TrapInfo trap = trap_handler_.getPendingInterrupt(pc_);
    trap_handler_.clearInterrupt(trap.cause);
    handleTrap(trap);
    return true;
}

void PipelinedCPU::handleMret() {
    const uint32 epc = trap_handler_.exitTrap();
    pc_ = epc;
    pipeline_regs_ = PipelineRegisters();
    fetch_stopped_ = false;
    state_ = CpuState::RUNNING;
    pushTrace("[SYSTEM] mret");
}

void PipelinedCPU::handleEcall() {
    handleTrap(trap_handler_.handleEcall(pc_));
    pushTrace("[SYSTEM] ecall");
}

void PipelinedCPU::handleEbreak() {
    handleTrap(trap_handler_.handleBreakpoint(pc_));
    pushTrace("[SYSTEM] ebreak");
}

int32 PipelinedCPU::executeCsrInstruction(const Instruction& instr, int32 operand1) {
    const uint16 csr_addr = static_cast<uint16>(instr.imm & 0xFFF);
    const uint32 old_value = csr_.read(csr_addr);
    uint32 write_value = static_cast<uint32>(operand1);

    if (instr.funct3 >= 0x5) {
        write_value = static_cast<uint32>(instr.rs1);
    }

    uint32 new_value = old_value;
    switch (instr.funct3) {
        case 0x1: new_value = write_value; break;
        case 0x2: new_value = old_value | write_value; break;
        case 0x3: new_value = old_value & ~write_value; break;
        case 0x5: new_value = static_cast<uint32>(instr.rs1); break;
        case 0x6: new_value = old_value | static_cast<uint32>(instr.rs1); break;
        case 0x7: new_value = old_value & ~static_cast<uint32>(instr.rs1); break;
        default:
            handleTrap(trap_handler_.handleIllegalInstruction(instr.raw, instr.pc));
            return 0;
    }

    csr_.write(csr_addr, new_value);
    return static_cast<int32>(old_value);
}

SimulatorState PipelinedCPU::getSimulatorState() const {
    SimulatorState state;
    const auto register_snapshot = registers_.getAllRegisters();
    state.state = state_;
    state.mode = SimulationMode::PIPELINED;
    state.mode_name = "Pipelined";
    state.true_pipeline = true;
    state.mode_note = "True overlapped 5-stage pipeline. IF/ID/EX/MEM/WB advance together each cycle.";
    state.pc = pc_;
    state.registers.assign(register_snapshot.begin(), register_snapshot.end());
    state.current_stage = current_stage_;
    state.pipeline_regs = pipeline_regs_;
    state.stats = stats_;
    state.hazard_signals = hazard_signals_;
    state.peripherals = peripherals_;
    state.execution_trace = trace_.size() > 20
        ? std::vector<std::string>(trace_.end() - 20, trace_.end())
        : trace_;

    state.ifid_text = pipeline_regs_.if_id_valid ? decoder_.disassemble(pipeline_regs_.if_id_instruction) : "";
    state.idex_text = pipeline_regs_.id_ex_valid ? decoder_.disassemble(pipeline_regs_.id_ex_instruction) : "";
    state.exmem_text = pipeline_regs_.ex_mem_valid ? decoder_.disassemble(pipeline_regs_.ex_mem_instruction) : "";
    state.memwb_text = pipeline_regs_.mem_wb_valid ? decoder_.disassemble(pipeline_regs_.mem_wb_instruction) : "";
    state.fetch_text = fetch_view_text_;
    state.decode_text = decode_view_text_;
    state.execute_text = execute_view_text_;
    state.memory_text = memory_view_text_;
    state.writeback_text = writeback_view_text_;
    state.ifid_pc = pipeline_regs_.if_id_pc;
    state.idex_pc = pipeline_regs_.id_ex_pc;
    state.exmem_pc = pipeline_regs_.ex_mem_pc;
    state.memwb_pc = pipeline_regs_.mem_wb_pc;
    state.fetch_pc = fetch_view_pc_;
    state.decode_pc = decode_view_pc_;
    state.execute_pc = execute_view_pc_;
    state.memory_pc = memory_view_pc_;
    state.writeback_pc = writeback_view_pc_;
    state.ifid_valid = pipeline_regs_.if_id_valid;
    state.idex_valid = pipeline_regs_.id_ex_valid;
    state.exmem_valid = pipeline_regs_.ex_mem_valid;
    state.memwb_valid = pipeline_regs_.mem_wb_valid;
    state.fetch_valid = fetch_view_valid_;
    state.decode_valid = decode_view_valid_;
    state.execute_valid = execute_view_valid_;
    state.memory_valid = memory_view_valid_;
    state.writeback_valid = writeback_view_valid_;

    if (memory_) {
        state.memory_snapshot = memory_->getMemorySnapshot();
        state.mmu.paging_enabled = memory_->isPagingEnabled();
        state.mmu.mapped_pages = static_cast<uint32>(memory_->getMappedPageCount());
    }

    state.csr.mstatus = csr_.getMstatus();
    state.csr.mie = csr_.getMie();
    state.csr.mip = csr_.getMip();
    state.csr.mtvec = csr_.getMtvec();
    state.csr.mepc = csr_.getMepc();
    state.csr.mcause = csr_.getMcause();
    state.csr.mtval = csr_.getMtval();
    state.csr.privilege_mode =
        csr_.getPrivilegeMode() == PrivilegeMode::MACHINE ? "M" :
        csr_.getPrivilegeMode() == PrivilegeMode::SUPERVISOR ? "S" : "U";
    return state;
}

std::string PipelinedCPU::toJson() const {
    SimulatorState state = getSimulatorState();
    return state.toJson(memory_.get());
}

} // namespace mycpu
