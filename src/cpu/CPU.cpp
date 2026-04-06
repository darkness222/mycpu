#include "CPU.h"
#include "../memory/Memory.h"
#include "../bus/Bus.h"
#include "../include/Constants.h"
#include "../elf/ElfLoader.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cstdio>

namespace mycpu {

CPU::CPU()
    : pc_(0)
    , state_(CpuState::HALTED)
    , current_stage_(PipelineStage::FETCH)
    , exec_mode_(ExecMode::ASSEMBLY)
    , fetch_view_valid_(false)
    , fetch_view_pc_(0)
    , branch_taken_(false)
    , branch_target_(0)
    , test_finished_(false)
    , test_passed_(false)
    , test_code_(0)
    , tohost_address_(0) {
    registers_.reset();
    trap_handler_.setCsr(&csr_);
}

CPU::~CPU() {}

void CPU::reset() {
    trap_handler_.setCsr(&csr_);
    pc_ = constants::RESET_VECTOR;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;
    registers_.reset();
    stats_ = CpuStats();
    hazard_signals_ = HazardSignals();
    trace_.clear();
    trace_.push_back("[CPU] reset complete, ready to execute");

    // Reset CSR state
    csr_.setPrivilegeMode(PrivilegeMode::MACHINE);
    csr_.write(CSR::MSTATUS, (static_cast<uint32>(PrivilegeMode::MACHINE) << 11));
    csr_.write(CSR::MTVEC, constants::TRAP_VECTOR);
    csr_.write(CSR::MIE, (1u << 3) | (1u << 7) | (1u << 11));
    csr_.enableInterrupts();
    peripherals_ = PeripheralState();

    pipeline_regs_ = PipelineRegisters();
    fetch_instruction_ = 0;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    branch_taken_ = false;
    branch_target_ = 0;
    test_finished_ = false;
    test_passed_ = false;
    test_code_ = 0;
    tohost_address_ = 0;
}

bool CPU::loadElf(const std::vector<uint8>& elf_data) {
    std::cout << "[CPU] loadElf called, data size: " << elf_data.size() << " bytes" << std::endl;
    
    ElfLoader loader;
    if (!loader.loadFromBytes(elf_data)) {
        std::ostringstream oss;
        oss << "Failed to load ELF: invalid format";
        trace_.push_back(oss.str());
        std::cout << "[CPU] ELF format validation failed" << std::endl;
        return false;
    }
    
    std::cout << "[CPU] ELF format valid, entry point: 0x" << std::hex << loader.getEntryPoint() << std::dec << std::endl;
    std::cout << "[CPU] Machine: 0x" << std::hex << loader.getMachine() << ", Type: " << loader.getFileType() << std::dec << std::endl;

    auto result = loader.loadToMemory(memory_, bus_);
    if (!result.success) {
        trace_.push_back("Failed to load ELF to memory: " + result.error_message);
        std::cout << "[CPU] Load to memory failed: " << result.error_message << std::endl;
        return false;
    }
    
    exec_mode_ = ExecMode::ELF;
    tohost_address_ = loader.getSectionAddress(".tohost");
    if (tohost_address_ == 0) {
        tohost_address_ = constants::RISCV_TEST_TOHOST_FALLBACK;
    }

    // Initialize stack pointer for ELF programs.
    registers_.write(constants::REG_SP, constants::DEFAULT_STACK_POINTER);

    std::ostringstream oss;
    oss << "ELF loaded: entry=0x" << std::hex << result.entry_point
        << ", segments=" << result.segment_count
        << ", size=0x" << result.load_size;
    trace_.push_back(oss.str());

    return true;
}

bool CPU::loadBinary(const std::vector<uint32>& program, uint32 start_address) {
    if (!memory_) return false;

    for (size_t i = 0; i < program.size(); ++i) {
        memory_->writeWord(start_address + i * 4, program[i]);
    }

    pc_ = start_address;
    state_ = CpuState::RUNNING;
    exec_mode_ = ExecMode::BINARY;
    registers_.write(constants::REG_SP, constants::DEFAULT_STACK_POINTER);

    std::ostringstream oss;
    oss << "Binary loaded to 0x" << std::hex << start_address
        << ", " << std::dec << program.size() << " instructions";
    trace_.push_back(oss.str());

    return true;
}

void CPU::loadProgram(const std::vector<uint32>& program, uint32 start_address) {
    if (!memory_) return;

    for (size_t i = 0; i < program.size(); ++i) {
        memory_->writeWord(start_address + i * 4, program[i]);
    }

    pc_ = start_address;
    state_ = CpuState::RUNNING;
    current_stage_ = PipelineStage::FETCH;

    std::ostringstream oss;
    oss << "[CPU] program loaded at 0x" << std::hex << start_address
        << " (" << std::dec << program.size() << " instructions)";
    trace_.push_back(oss.str());
}

void CPU::step() {
    if (state_ == CpuState::HALTED) {
        return;
    }

    stats_.cycle_count++;
    csr_.incrementCycle();
    if (bus_) {
        bus_->tick();
    }
    syncPeripheralState();

    // ===== 濮ｅ繐鎳嗛張鐔割梾閺屻儰鑵戦弬?=====
    if (state_ == CpuState::RUNNING) {
        if (checkAndTakeInterrupt()) {
            return;
        }
    }

    switch (current_stage_) {
        case PipelineStage::FETCH:
            fetch();
            break;
        case PipelineStage::DECODE:
            decode();
            break;
        case PipelineStage::EXECUTE:
            execute();
            break;
        case PipelineStage::MEMORY:
            memoryAccess();
            break;
        case PipelineStage::WRITEBACK:
            writeback();
            break;
    }

    detectHazards();

    if (state_ == CpuState::HALTED) {
        return;
    }

    current_stage_ = static_cast<PipelineStage>((static_cast<int>(current_stage_) + 1) % 5);
}

void CPU::run(uint64 cycles) {
    for (uint64 i = 0; i < cycles && state_ != CpuState::HALTED; ++i) {
        step();
    }
}

void CPU::fetch() {
    if (!memory_) return;
    fetch_view_valid_ = false;
    fetch_view_pc_ = 0;
    fetch_view_text_.clear();
    if (exec_mode_ == ExecMode::ELF && pc_ < constants::RISCV_TEST_BASE) {
        std::ostringstream warn;
        warn << "[WARN] PC dropped below official ELF base: 0x" << std::hex << pc_;
        trace_.push_back(warn.str());
        pc_ += constants::RISCV_TEST_BASE;
    }

    // Handle pending flush from the previous cycle before fetching a new instruction.
    if (hazard_signals_.flush) {
        pipeline_regs_.if_id_valid = false;
        hazard_signals_.flush = false;
        stats_.flush_count++;
    }

    // ===== 婢跺嫮鎮?Stall =====
    if (hazard_signals_.stall) {
        hazard_signals_.stall = false;
        stats_.stall_count++;
        return;
    }

    // Fetch the next instruction. The execute stage may still redirect PC later
    // if a branch or trap changes control flow.
    if ((pc_ & 0x3) != 0) {
        TrapInfo trap = trap_handler_.handleInstructionAddressMisaligned(pc_, pc_);
        handleTrap(trap);
        return;
    }

    uint32 translated_pc = 0;
    if (!resolveMemoryAccess(pc_, MemoryAccessType::FETCH, 4, pc_, translated_pc)) {
        return;
    }
    bool fetch_from_mmio = translated_pc >= constants::MMIO_BASE &&
                           translated_pc < (constants::MMIO_BASE + 0x1000);
    if (fetch_from_mmio) {
        TrapInfo trap = trap_handler_.handleInstructionAccessFault(pc_);
        handleTrap(trap);
        return;
    }

    fetch_instruction_ = memory_->readWord(translated_pc);
    Instruction instr = decoder_.decode(fetch_instruction_, pc_);
    fetch_view_valid_ = true;
    fetch_view_pc_ = pc_;
    fetch_view_text_ = decoder_.disassemble(instr);

    pipeline_regs_.if_id_pc = pc_;
    pipeline_regs_.if_id_instruction = instr;
    pipeline_regs_.if_id_valid = true;

    pc_ += 4;

    std::ostringstream oss;
    oss << "[IF] PC=0x" << std::hex << pipeline_regs_.if_id_pc << " fetch: " << instr.disassembly;
    trace_.push_back(oss.str());
}

void CPU::decode() {
    if (!pipeline_regs_.if_id_valid) {
        pipeline_regs_.id_ex_valid = false;
        return;
    }

    Instruction instr = pipeline_regs_.if_id_instruction;

    int32 reg_data1 = registers_.read(instr.rs1);
    int32 reg_data2 = registers_.read(instr.rs2);

    pipeline_regs_.id_ex_pc = pipeline_regs_.if_id_pc;
    pipeline_regs_.id_ex_instruction = instr;
    pipeline_regs_.id_ex_reg_data1 = reg_data1;
    pipeline_regs_.id_ex_reg_data2 = reg_data2;
    pipeline_regs_.id_ex_valid = true;
    pipeline_regs_.if_id_valid = false;

    std::ostringstream oss;
    oss << "[ID] decode: " << instr.disassembly
        << " | rs1=x" << (int)instr.rs1 << "=" << reg_data1
        << " rs2=x" << (int)instr.rs2 << "=" << reg_data2;
    trace_.push_back(oss.str());
}

void CPU::execute() {
    if (!pipeline_regs_.id_ex_valid) {
        pipeline_regs_.ex_mem_valid = false;
        return;
    }

    Instruction instr = pipeline_regs_.id_ex_instruction;
    int32 operand1 = pipeline_regs_.id_ex_reg_data1;
    int32 operand2 = pipeline_regs_.id_ex_reg_data2;
    int32 alu_result = 0;
    bool mem_read = false;
    bool mem_write = false;
    bool reg_write = false;
    int32 write_value = 0;
    uint8 write_reg = 0;

    switch (instr.opcode) {
        case Opcode::LUI:
            alu_result = instr.imm;
            write_reg = instr.rd;
            write_value = alu_result;
            reg_write = true;
            break;

        case Opcode::AUIPC:
            alu_result = pipeline_regs_.id_ex_pc + instr.imm;
            write_reg = instr.rd;
            write_value = alu_result;
            reg_write = true;
            break;

        case Opcode::JAL:
            alu_result = pipeline_regs_.id_ex_pc + 4;
            write_reg = instr.rd;
            write_value = alu_result;
            reg_write = true;
            pc_ = pipeline_regs_.id_ex_pc + instr.imm;
            branch_taken_ = true;
            break;

        case Opcode::JALR:
            alu_result = (operand1 + instr.imm) & ~1;
            write_reg = instr.rd;
            write_value = alu_result;
            reg_write = true;
            pc_ = alu_result;
            branch_taken_ = true;
            break;

        case Opcode::OP_IMM:
            if (instr.funct3 == 0x5) {
                uint32 shamt = static_cast<uint32>(instr.imm) & 0x1F;
                bool arithmetic_shift = (instr.funct7 == 0x20);
                alu_result = arithmetic_shift
                    ? (operand1 >> shamt)
                    : static_cast<int32>(static_cast<uint32>(operand1) >> shamt);
            } else {
                alu_result = alu(operand1, instr.imm, instr.funct3);
            }
            write_reg = instr.rd;
            write_value = alu_result;
            reg_write = true;
            break;

        case Opcode::OP:
            if (instr.funct7 == 0x01) {
                int32_t op1 = static_cast<int32_t>(operand1);
                int32_t op2 = static_cast<int32_t>(operand2);
                uint32_t uop1 = static_cast<uint32_t>(operand1);
                uint32_t uop2 = static_cast<uint32_t>(operand2);
                int32_t mresult = 0;
                switch (instr.funct3) {
                    case 0x0: { // mul (low 32 bits signed*signed)
                        int64_t prod = static_cast<int64_t>(op1) * static_cast<int64_t>(op2);
                        mresult = static_cast<int32_t>(static_cast<uint32_t>(prod & 0xFFFFFFFF));
                        break;
                    }
                    case 0x1: { // mulh (high 32 bits signed*signed)
                        int64_t prod = static_cast<int64_t>(op1) * static_cast<int64_t>(op2);
                        mresult = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(prod) >> 32));
                        break;
                    }
                    case 0x2: { // mulhsu (high 32 bits signed*unsigned)
                        int64_t s = static_cast<int64_t>(op1);
                        uint64_t u = static_cast<uint64_t>(uop2);
                        uint64_t prod = static_cast<uint64_t>(s < 0 ? static_cast<uint64_t>(static_cast<int64_t>(s)) : static_cast<uint64_t>(s)) * u;
                        mresult = static_cast<int32_t>(static_cast<uint32_t>(prod >> 32));
                        break;
                    }
                    case 0x3: { // mulhu (high 32 bits unsigned*unsigned)
                        uint64_t prod = static_cast<uint64_t>(uop1) * static_cast<uint64_t>(uop2);
                        mresult = static_cast<int32_t>(static_cast<uint32_t>(prod >> 32));
                        break;
                    }
                    case 0x4: { // div (signed)
                        if (op2 == 0) mresult = -1;
                        else if (op1 == INT32_MIN && op2 == -1) mresult = op1;
                        else mresult = static_cast<int32_t>(op1 / op2);
                        break;
                    }
                    case 0x5: { // divu (unsigned)
                        if (uop2 == 0) mresult = static_cast<int32_t>(-1);
                        else mresult = static_cast<int32_t>(uop1 / uop2);
                        break;
                    }
                    case 0x6: { // rem (signed)
                        if (op2 == 0) mresult = op1;
                        else if (op1 == INT32_MIN && op2 == -1) mresult = 0;
                        else mresult = static_cast<int32_t>(op1 % op2);
                        break;
                    }
                    case 0x7: { // remu (unsigned)
                        if (uop2 == 0) mresult = static_cast<int32_t>(uop1);
                        else mresult = static_cast<int32_t>(uop1 % uop2);
                        break;
                    }
                    default:
                        mresult = 0;
                        break;
                }
                alu_result = mresult;
                write_reg = instr.rd;
                write_value = alu_result;
                reg_write = true;
            } else {
                alu_result = alu(operand1, operand2, instr.funct3, instr.funct7 == 0x20);
                write_reg = instr.rd;
                write_value = alu_result;
                reg_write = true;
            }
            break;

        case Opcode::LOAD:
            alu_result = operand1 + instr.imm;
            write_reg = instr.rd;
            reg_write = true;
            mem_read = true;
            break;

        case Opcode::STORE:
            alu_result = operand1 + instr.imm;
            mem_write = true;
            pipeline_regs_.ex_mem_mem_write_data = operand2;
            break;

        case Opcode::BRANCH:
            {
                bool taken = false;
                switch (instr.funct3) {
                    case constants::Funct3::BEQ: taken = (operand1 == operand2); break;
                    case constants::Funct3::BNE: taken = (operand1 != operand2); break;
                    case constants::Funct3::BLT: taken = (operand1 < operand2); break;
                    case constants::Funct3::BGE: taken = (operand1 >= operand2); break;
                    case constants::Funct3::BLTU: taken = ((uint32)operand1 < (uint32)operand2); break;
                    case constants::Funct3::BGEU: taken = ((uint32)operand1 >= (uint32)operand2); break;
                }
                branch_taken_ = taken;
                branch_target_ = pipeline_regs_.id_ex_pc + instr.imm;
                if (taken) {
                    pc_ = branch_target_;
                    stats_.branch_taken++;
                } else {
                    stats_.branch_not_taken++;
                }
                hazard_signals_.flush = true;
            }
            break;

        case Opcode::SYSTEM:
            {
                if (instr.funct3 == 0) {
                    if (instr.raw == 0x00100073) {
                        handleEbreak();
                    } else if (instr.raw == 0x00000073) {
                        handleEcallSyscall();
                    } else if (instr.raw == 0x10200073) {
                    } else if (instr.raw == 0x30200073) {
                        handleMret();
                    } else {
                        TrapInfo trap = trap_handler_.handleIllegalInstruction(instr.raw, instr.pc);
                        handleTrap(trap);
                    }
                } else {
                    executeCsrInstruction(instr);
                }
            }
            break;

        case Opcode::FENCE:
            break;

        case Opcode::HALT:
            state_ = CpuState::HALTED;
            trace_.push_back("[EX] HALT executed, processor stopped");
            break;

        default:
            break;
    }

    pipeline_regs_.ex_mem_pc = pipeline_regs_.id_ex_pc;
    pipeline_regs_.ex_mem_instruction = instr;
    pipeline_regs_.ex_mem_alu_result = alu_result;
    pipeline_regs_.ex_mem_mem_read = mem_read;
    pipeline_regs_.ex_mem_mem_write = mem_write;
    pipeline_regs_.ex_mem_valid = true;
    pipeline_regs_.id_ex_valid = false;

    pipeline_regs_.mem_wb_alu_result = write_value;
    pipeline_regs_.mem_wb_reg_write = reg_write;
    if (reg_write) {
        pipeline_regs_.mem_wb_instruction.rd = write_reg;
    }

    std::ostringstream oss;
    oss << "[EX] execute: " << instr.disassembly << " => ALU result=" << alu_result;
    trace_.push_back(oss.str());
}

void CPU::memoryAccess() {
    if (!pipeline_regs_.ex_mem_valid) {
        pipeline_regs_.mem_wb_valid = false;
        return;
    }

    Instruction instr = pipeline_regs_.ex_mem_instruction;
    int32 alu_result = pipeline_regs_.ex_mem_alu_result;
    int32 mem_data = 0;

    if (!memory_) {
        pipeline_regs_.mem_wb_valid = false;
        return;
    }

    uint32 translated_addr = 0;
    uint8 access_size = 1;
    if (instr.funct3 == constants::Funct3::LH || instr.funct3 == constants::Funct3::LHU ||
        instr.funct3 == constants::Funct3::SH) {
        access_size = 2;
    } else if (instr.funct3 == constants::Funct3::LW || instr.funct3 == constants::Funct3::SW) {
        access_size = 4;
    }

    if ((pipeline_regs_.ex_mem_mem_read || pipeline_regs_.ex_mem_mem_write) &&
        !resolveMemoryAccess(static_cast<uint32>(alu_result),
                             pipeline_regs_.ex_mem_mem_write ? MemoryAccessType::STORE : MemoryAccessType::LOAD,
                             access_size,
                             pipeline_regs_.ex_mem_pc,
                             translated_addr)) {
        pipeline_regs_.ex_mem_valid = false;
        pipeline_regs_.mem_wb_valid = false;
        return;
    }

    if (pipeline_regs_.ex_mem_mem_read) {
        switch (instr.funct3) {
            case constants::Funct3::LB:
                mem_data = (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000))
                    ? static_cast<int32>(static_cast<int8>(bus_->read(translated_addr, 1)))
                    : static_cast<int32>(static_cast<int8>(memory_->readByte(translated_addr)));
                break;
            case constants::Funct3::LH:
                mem_data = (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000))
                    ? static_cast<int32>(static_cast<int16>(bus_->read(translated_addr, 2)))
                    : static_cast<int32>(static_cast<int16>(memory_->readHalfWord(translated_addr)));
                break;
            case constants::Funct3::LW:
                mem_data = (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000))
                    ? static_cast<int32>(bus_->read(translated_addr, 4))
                    : static_cast<int32>(memory_->readWord(translated_addr));
                break;
            case constants::Funct3::LBU:
                mem_data = (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000))
                    ? static_cast<int32>(bus_->read(translated_addr, 1))
                    : static_cast<int32>(memory_->readByte(translated_addr));
                break;
            case constants::Funct3::LHU:
                mem_data = (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000))
                    ? static_cast<int32>(bus_->read(translated_addr, 2))
                    : static_cast<int32>(memory_->readHalfWord(translated_addr));
                break;
        }
        pipeline_regs_.mem_wb_mem_data = mem_data;
    }

    if (pipeline_regs_.ex_mem_mem_write) {
        int32 write_data = pipeline_regs_.ex_mem_mem_write_data;
        switch (instr.funct3) {
            case constants::Funct3::SB:
                if (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000)) bus_->write(translated_addr, static_cast<uint32>(write_data & 0xFF), 1);
                else memory_->writeByte(translated_addr, static_cast<uint8>(write_data & 0xFF));
                break;
            case constants::Funct3::SH:
                if (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000)) bus_->write(translated_addr, static_cast<uint32>(write_data & 0xFFFF), 2);
                else memory_->writeHalfWord(translated_addr, static_cast<uint16>(write_data & 0xFFFF));
                break;
            case constants::Funct3::SW:
                if (translated_addr >= constants::MMIO_BASE && translated_addr < (constants::MMIO_BASE + 0x1000)) bus_->write(translated_addr, static_cast<uint32>(write_data), 4);
                else memory_->writeWord(translated_addr, static_cast<uint32>(write_data));
                break;
        }

        if (exec_mode_ == ExecMode::ELF &&
            tohost_address_ != 0 &&
            static_cast<uint32>(alu_result) == tohost_address_ &&
            static_cast<uint32>(write_data) != 0) {
            test_finished_ = true;
            test_code_ = static_cast<uint32>(write_data);
            test_passed_ = (test_code_ == 1);
            state_ = CpuState::HALTED;

            std::ostringstream status;
            status << "[TEST] tohost=0x" << std::hex << test_code_
                   << (test_passed_ ? " PASS" : " FAIL");
            trace_.push_back(status.str());
        }
    }

    if (hazard_signals_.flush) {
        hazard_signals_.flush = false;
    }

    pipeline_regs_.mem_wb_pc = pipeline_regs_.ex_mem_pc;
    pipeline_regs_.mem_wb_instruction = instr;
    pipeline_regs_.mem_wb_valid = true;
    pipeline_regs_.ex_mem_valid = false;

    std::ostringstream oss;
    oss << "[MEM] access: " << instr.disassembly;
    if (pipeline_regs_.ex_mem_mem_read) {
        oss << " => load=" << mem_data;
    }
    if (pipeline_regs_.ex_mem_mem_write) {
        oss << " => store committed";
    }
    trace_.push_back(oss.str());
}

void CPU::writeback() {
    if (!pipeline_regs_.mem_wb_valid) {
        return;
    }

    Instruction instr = pipeline_regs_.mem_wb_instruction;

    // Commit register writes in writeback.
    if (pipeline_regs_.mem_wb_reg_write && instr.rd != 0) {
        int32 write_value = pipeline_regs_.mem_wb_alu_result;
        if (instr.opcode == Opcode::LOAD) {
            write_value = pipeline_regs_.mem_wb_mem_data;
        } else if (instr.opcode == Opcode::JALR) {
            write_value = pipeline_regs_.mem_wb_pc + 4;
        }
        registers_.write(instr.rd, write_value);

        std::ostringstream oss;
        oss << "[WB] writeback: x" << (int)instr.rd << " = " << write_value;
        trace_.push_back(oss.str());
    }

    // The sequential teaching core advances PC during fetch. Control-flow changes
    // are still handled by later stages through flush / trap redirection.

    stats_.instruction_count++;
    pipeline_regs_.mem_wb_valid = false;

    if (state_ == CpuState::HALTED) {
        std::ostringstream oss;
        oss << "[CPU] halted after cycles=" << stats_.cycle_count
            << ", instructions=" << stats_.instruction_count;
        trace_.push_back(oss.str());
    }
}

void CPU::detectHazards() {
    hazard_signals_.stall = false;
    hazard_signals_.flush = false;
    hazard_signals_.forward_from_exmem = 0;
    hazard_signals_.forward_from_memwb = 0;

    if (!pipeline_regs_.id_ex_valid || !pipeline_regs_.if_id_valid) {
        return;
    }

    Instruction id_instr = pipeline_regs_.if_id_instruction;
    Instruction ex_instr = pipeline_regs_.id_ex_instruction;

    if (ex_instr.opcode == Opcode::LOAD &&
        (id_instr.rs1 == ex_instr.rd || id_instr.rs2 == ex_instr.rd)) {
        hazard_signals_.stall = true;
        hazard_signals_.description = "Load-use hazard detected, pipeline stalled";
    }
}

bool CPU::checkAndTakeInterrupt() {
    if (!trap_handler_.hasPendingInterrupt()) {
        return false;
    }

    TrapInfo trap = trap_handler_.getPendingInterrupt(pc_);
    trap_handler_.clearInterrupt(trap.cause);
    handleTrap(trap);
    state_ = CpuState::INTERRUPT;
    return true;
}

void CPU::handleTrap(const TrapInfo& trap) {
    std::ostringstream oss;
    if (trap.is_interrupt) {
        oss << "[TRAP] Interrupt cause=0x" << std::hex << trap.cause
            << " epc=0x" << trap.epc;
    } else {
        oss << "[TRAP] Exception cause=0x" << std::hex << trap.cause
            << " epc=0x" << trap.epc;
    }
    trace_.push_back(oss.str());

    trap_handler_.enterTrap(trap);

    uint32 trap_pc = trap_handler_.calculateTrapVector(trap.cause);
    pc_ = trap_pc;
    branch_taken_ = true;
    branch_target_ = trap_pc;

    // flush IF/ID
    pipeline_regs_.if_id_valid = false;
    stats_.flush_count++;
}

void CPU::handleEcallSyscall() {
    if (exec_mode_ == ExecMode::ELF) {
        TrapInfo trap = trap_handler_.handleEcall(pc_);
        handleTrap(trap);
        trace_.push_back("[SYSTEM] ecall - delegated to official trap vector");
        return;
    }

    TrapInfo trap = trap_handler_.handleEcall(pc_);
    handleTrap(trap);
    trace_.push_back("[SYSTEM] ecall - entering trap handler");

    uint32 a7 = registers_.read(constants::REG_A7);
    uint32 a0 = registers_.read(constants::REG_A0);

    switch (a7) {
        case 0: // SBI_PUTCHAR
            {
                char c = static_cast<char>(a0 & 0xFF);
                std::ostringstream out;
                out << "[SBI] putchar: " << c;
                trace_.push_back(out.str());
            }
            break;
        case 1: // SBI_SHUTDOWN
            trace_.push_back("[SBI] shutdown request");
            state_ = CpuState::HALTED;
            break;
        default:
            {
                std::ostringstream out;
                out << "[SBI] unknown call: a7=0x" << std::hex << a7;
                trace_.push_back(out.str());
            }
            break;
    }

    // In M mode, ecall raises an exception and enters the trap handler.
    // U/S mode ecall handling can be extended here in the future.
}

void CPU::handleEbreak() {
    TrapInfo trap = trap_handler_.handleBreakpoint(pc_);
    handleTrap(trap);
    trace_.push_back("[SYSTEM] ebreak - entering trap handler");
}

void CPU::handleMret() {
    uint32 epc = csr_.getMepc();
    csr_.exitTrap();
    pc_ = epc;
    branch_taken_ = true;
    branch_target_ = epc;
    state_ = CpuState::RUNNING;
    {
        std::ostringstream oss;
        oss << "[SYSTEM] mret - returning from trap to 0x" << std::hex << epc;
        trace_.push_back(oss.str());
    }

    // flush pipeline
    pipeline_regs_.if_id_valid = false;
    pipeline_regs_.id_ex_valid = false;
}

void CPU::syncPeripheralState() {
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

bool CPU::resolveMemoryAccess(uint32 addr, MemoryAccessType access, uint8 size,
                              uint32 pc, uint32& translated_addr) {
    if (!memory_) {
        return false;
    }

    if ((size == 2 && (addr & 0x1)) || (size == 4 && (addr & 0x3))) {
        if (access == MemoryAccessType::FETCH) {
            TrapInfo trap = trap_handler_.handleInstructionAddressMisaligned(pc, addr);
            handleTrap(trap);
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

void CPU::executeCsrInstruction(const Instruction& instr) {
    uint16 csr_addr = static_cast<uint16>(instr.imm & 0xFFF);
    uint32 old_value = csr_.read(csr_addr);
    uint32 write_value = registers_.read(instr.rs1);

    uint32 new_value = old_value;
    switch (instr.funct3) {
        case 0x1: // csrrw
            new_value = write_value;
            break;
        case 0x2: // csrrs
            new_value = old_value | write_value;
            break;
        case 0x3: // csrrc
            new_value = old_value & ~write_value;
            break;
        case 0x5: // csrrwi
            new_value = instr.rs1;
            break;
        case 0x6: // csrrsi
            new_value = old_value | instr.rs1;
            break;
        case 0x7: // csrrci
            new_value = old_value & ~instr.rs1;
            break;
        default:
            // 闂堢偞纭堕幐鍥︽姢
            TrapInfo trap = trap_handler_.handleIllegalInstruction(instr.raw, instr.pc);
            handleTrap(trap);
            return;
    }

    csr_.write(csr_addr, new_value);

    if (instr.rd != 0) {
        registers_.write(instr.rd, old_value);
    }

    std::ostringstream oss;
    oss << "[CSR] " << std::hex << csr_addr
        << " old=0x" << old_value
        << " new=0x" << new_value;
    trace_.push_back(oss.str());
}

int32 CPU::alu(int32 operand1, int32 operand2, uint8 funct3, bool is_sub) {
    switch (funct3) {
        case 0x0: return is_sub ? (operand1 - operand2) : (operand1 + operand2);
        case 0x1: return operand1 << (operand2 & 0x1F);
        case 0x2: return (operand1 < operand2) ? 1 : 0;
        case 0x3: return ((uint32)operand1 < (uint32)operand2) ? 1 : 0;
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

SimulatorState CPU::getSimulatorState() const {
    SimulatorState state;
    const auto register_snapshot = registers_.getAllRegisters();
    state.state = state_;
    state.mode = SimulationMode::MULTI_CYCLE;
    state.mode_name = "Multi-cycle";
    state.true_pipeline = false;
    state.mode_note = "Sequential 5-stage teaching model. Instructions advance stage by stage, but they are not overlapped yet.";
    state.pc = pc_;
    state.registers.assign(register_snapshot.begin(), register_snapshot.end());

    state.current_stage = current_stage_;
    state.pipeline_regs = pipeline_regs_;
    state.stats = stats_;
    state.hazard_signals = hazard_signals_;
    state.peripherals = peripherals_;
    state.execution_trace = trace_;
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

    if (trace_.size() > 20) {
        state.execution_trace = std::vector<std::string>(
            trace_.end() - 20, trace_.end());
    }

    state.ifid_text  = pipeline_regs_.if_id_valid  ? decoder_.disassemble(pipeline_regs_.if_id_instruction)  : "";
    state.idex_text  = pipeline_regs_.id_ex_valid  ? decoder_.disassemble(pipeline_regs_.id_ex_instruction)  : "";
    state.exmem_text = pipeline_regs_.ex_mem_valid ? decoder_.disassemble(pipeline_regs_.ex_mem_instruction) : "";
    state.memwb_text = pipeline_regs_.mem_wb_valid ? decoder_.disassemble(pipeline_regs_.mem_wb_instruction) : "";
    state.fetch_text = fetch_view_text_;
    state.ifid_pc = pipeline_regs_.if_id_pc;
    state.idex_pc = pipeline_regs_.id_ex_pc;
    state.exmem_pc = pipeline_regs_.ex_mem_pc;
    state.memwb_pc = pipeline_regs_.mem_wb_pc;
    state.fetch_pc = fetch_view_pc_;
    state.ifid_valid = pipeline_regs_.if_id_valid;
    state.idex_valid = pipeline_regs_.id_ex_valid;
    state.exmem_valid = pipeline_regs_.ex_mem_valid;
    state.memwb_valid = pipeline_regs_.mem_wb_valid;
    state.fetch_valid = fetch_view_valid_;

    if (memory_) {
        state.memory_snapshot = memory_->getMemorySnapshot();
        state.mmu.paging_enabled = memory_->isPagingEnabled();
        state.mmu.mapped_pages = static_cast<uint32>(memory_->getMappedPageCount());
        for (const auto& mapping : memory_->getPageTableSnapshot()) {
            std::ostringstream page_desc;
            page_desc << "VPN 0x" << std::hex << mapping.first
                      << " -> PPN 0x" << mapping.second.physical_page
                      << " [" << (mapping.second.readable ? 'R' : '-')
                      << (mapping.second.writable ? 'W' : '-')
                      << (mapping.second.executable ? 'X' : '-') << "]";
            state.mmu.page_mappings.push_back(page_desc.str());
            if (state.mmu.page_mappings.size() >= 8) {
                break;
            }
        }
    }

    return state;
}

std::string CPU::toJson() const {
    SimulatorState st = getSimulatorState();
    return st.toJson(memory_.get());
}

} // namespace mycpu
