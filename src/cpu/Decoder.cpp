#include "Decoder.h"
#include "../include/Constants.h"
#include <sstream>
#include <iomanip>

namespace mycpu {

Instruction Decoder::decode(uint32 instruction, uint32 pc) {
    Instruction instr;
    instr.raw = instruction;
    instr.pc = pc;

    Opcode opcode = getOpcode(instruction);
    instr.opcode = opcode;
    instr.rd = getRd(instruction);
    instr.rs1 = getRs1(instruction);
    instr.rs2 = getRs2(instruction);
    instr.funct3 = getFunct3(instruction);
    instr.funct7 = getFunct7(instruction);

    switch (opcode) {
        case Opcode::OP:
        case Opcode::OP_IMM:
        case Opcode::LOAD:
        case Opcode::STORE:
        case Opcode::BRANCH:
        case Opcode::JALR:
            instr.imm = decodeImmediate(instruction, opcode);
            break;
        case Opcode::LUI:
            instr.imm = static_cast<int32>(instruction & 0xFFFFF000);
            break;
        case Opcode::AUIPC:
            instr.imm = static_cast<int32>(instruction & 0xFFFFF000);
            break;
        case Opcode::JAL:
            {
                int32 imm = 0;
                imm |= ((instruction >> 31) & 0x1) << 20;
                imm |= ((instruction >> 12) & 0xFF) << 12;
                imm |= ((instruction >> 20) & 0x1) << 11;
                imm |= ((instruction >> 21) & 0x3FF) << 1;
                instr.imm = (imm & 0x100000) ? (imm | 0xFFE00000) : imm;
            }
            break;
        case Opcode::HALT:
            instr.mnemonic = "halt";
            break;
        default:
            instr.mnemonic = "unknown";
            break;
    }

    instr.disassembly = disassemble(instr);
    return instr;
}

int32 Decoder::decodeImmediate(uint32 instruction, Opcode opcode) {
    int32 imm = 0;

    switch (opcode) {
        case Opcode::OP_IMM:
        case Opcode::JALR:
        case Opcode::LOAD:
            {
                imm = static_cast<int32>(instruction >> 20);
                if (imm & 0x800) {
                    imm |= 0xFFFFF000;
                }
            }
            break;

        case Opcode::STORE:
            {
                imm = ((instruction >> 25) & 0x7F) << 5;
                imm |= ((instruction >> 7) & 0x1F);
                if (imm & 0x800) {
                    imm |= 0xFFFFF000;
                }
            }
            break;

        case Opcode::BRANCH:
            {
                imm = ((instruction >> 31) & 0x1) << 12;
                imm |= ((instruction >> 7) & 0x1) << 11;
                imm |= ((instruction >> 25) & 0x3F) << 5;
                imm |= ((instruction >> 8) & 0xF) << 1;
                if (imm & 0x1000) {
                    imm |= 0xFFFFE000;
                }
            }
            break;

        default:
            imm = 0;
            break;
    }

    return imm;
}

std::string Decoder::disassemble(const Instruction& instr) const {
    std::ostringstream oss;

    switch (instr.opcode) {
        case Opcode::LUI:
            oss << "lui x" << (int)instr.rd << ", 0x" << std::hex << (instr.imm >> 12);
            break;
        case Opcode::AUIPC:
            oss << "auipc x" << (int)instr.rd << ", 0x" << std::hex << (instr.imm >> 12);
            break;
        case Opcode::JAL:
            oss << "jal x" << (int)instr.rd << ", " << std::dec << instr.imm;
            break;
        case Opcode::JALR:
            oss << "jalr x" << (int)instr.rd << ", x" << (int)instr.rs1
                << ", " << instr.imm;
            break;
        case Opcode::LOAD:
            {
                std::string op;
                switch (instr.funct3) {
                    case constants::Funct3::LB: op = "lb"; break;
                    case constants::Funct3::LH: op = "lh"; break;
                    case constants::Funct3::LW: op = "lw"; break;
                    case constants::Funct3::LBU: op = "lbu"; break;
                    case constants::Funct3::LHU: op = "lhu"; break;
                    default: op = "lw"; break;
                }
                oss << op << " x" << (int)instr.rd << ", " << instr.imm
                    << "(x" << (int)instr.rs1 << ")";
            }
            break;
        case Opcode::STORE:
            {
                std::string op;
                switch (instr.funct3) {
                    case constants::Funct3::SB: op = "sb"; break;
                    case constants::Funct3::SH: op = "sh"; break;
                    case constants::Funct3::SW: op = "sw"; break;
                    default: op = "sw"; break;
                }
                oss << op << " x" << (int)instr.rs2 << ", " << instr.imm
                    << "(x" << (int)instr.rs1 << ")";
            }
            break;
        case Opcode::BRANCH:
            {
                std::string op;
                switch (instr.funct3) {
                    case constants::Funct3::BEQ: op = "beq"; break;
                    case constants::Funct3::BNE: op = "bne"; break;
                    case constants::Funct3::BLT: op = "blt"; break;
                    case constants::Funct3::BGE: op = "bge"; break;
                    case constants::Funct3::BLTU: op = "bltu"; break;
                    case constants::Funct3::BGEU: op = "bgeu"; break;
                    default: op = "beq"; break;
                }
                oss << op << " x" << (int)instr.rs1 << ", x" << (int)instr.rs2
                    << ", " << instr.imm;
            }
            break;
        case Opcode::OP_IMM:
            {
                std::string op;
                switch (instr.funct3) {
                    case constants::Funct3::ADDI: op = "addi"; break;
                    case constants::Funct3::SLLI: op = "slli"; break;
                    case constants::Funct3::SLTI: op = "slti"; break;
                    case constants::Funct3::SLTIU: op = "sltiu"; break;
                    case constants::Funct3::XORI: op = "xori"; break;
                    case constants::Funct3::ORI: op = "ori"; break;
                    case constants::Funct3::ANDI: op = "andi"; break;
                    case 0x5: // SRLI/SRAI share funct3=5, distinguished by funct7[5]
                        op = ((instr.funct7 & 0x20) ? "srai" : "srli");
                        break;
                    default: op = "addi"; break;
                }
                oss << op << " x" << (int)instr.rd << ", x" << (int)instr.rs1
                    << ", " << instr.imm;
            }
            break;
        case Opcode::OP:
            {
                std::string op;
                uint8 funct = (instr.funct7 << 3) | instr.funct3;
                switch (funct) {
                    case 0x00: op = "add"; break;
                    case 0x20: op = "sub"; break;
                    case 0x01: op = "sll"; break;
                    case 0x02: op = "slt"; break;
                    case 0x03: op = "sltu"; break;
                    case 0x04: op = "xor"; break;
                    case 0x05: op = "srl"; break;
                    case 0x25: op = "sra"; break;
                    case 0x06: op = "or"; break;
                    case 0x07: op = "and"; break;
                    default: op = "add"; break;
                }
                oss << op << " x" << (int)instr.rd << ", x" << (int)instr.rs1
                    << ", x" << (int)instr.rs2;
            }
            break;
        case Opcode::SYSTEM:
            {
                if (instr.funct3 == 0) {
                    if (instr.raw == 0x00100073) {
                        oss << "ebreak";
                    } else if (instr.raw == 0x00000073) {
                        oss << "ecall";
                    } else {
                        oss << "system";
                    }
                } else {
                    oss << "csrrw x" << (int)instr.rd << ", csr, x" << (int)instr.rs1;
                }
            }
            break;
        case Opcode::HALT:
            oss << "halt";
            break;
        default:
            oss << ".word 0x" << std::hex << instr.raw;
            break;
    }

    return oss.str();
}

std::string Decoder::stageToString(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::FETCH: return "FETCH";
        case PipelineStage::DECODE: return "DECODE";
        case PipelineStage::EXECUTE: return "EXECUTE";
        case PipelineStage::MEMORY: return "MEMORY";
        case PipelineStage::WRITEBACK: return "WRITEBACK";
        default: return "UNKNOWN";
    }
}

std::string Decoder::stateToString(CpuState state) {
    switch (state) {
        case CpuState::RUNNING: return "RUNNING";
        case CpuState::HALTED: return "HALTED";
        case CpuState::WAITING: return "WAITING";
        case CpuState::EXCEPTION: return "EXCEPTION";
        case CpuState::INTERRUPT: return "INTERRUPT";
        default: return "UNKNOWN";
    }
}

std::string Decoder::opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::LOAD: return "LOAD";
        case Opcode::STORE: return "STORE";
        case Opcode::BRANCH: return "BRANCH";
        case Opcode::JAL: return "JAL";
        case Opcode::JALR: return "JALR";
        case Opcode::LUI: return "LUI";
        case Opcode::AUIPC: return "AUIPC";
        case Opcode::OP_IMM: return "OP_IMM";
        case Opcode::OP: return "OP";
        case Opcode::SYSTEM: return "SYSTEM";
        case Opcode::HALT: return "HALT";
        default: return "UNKNOWN";
    }
}

} // namespace mycpu
