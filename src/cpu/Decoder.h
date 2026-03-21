#pragma once

#include "../include/Types.h"
#include <string>

namespace mycpu {

class Decoder {
public:
    Decoder() = default;

    Instruction decode(uint32 instruction, uint32 pc);
    std::string disassemble(const Instruction& instr) const;

    static std::string stageToString(PipelineStage stage);
    static std::string stateToString(CpuState state);
    static std::string opcodeToString(Opcode op);

private:
    int32 decodeImmediate(uint32 instruction, Opcode opcode);

    Opcode getOpcode(uint32 instruction) {
        return static_cast<Opcode>(instruction & 0x7F);
    }

    uint8 getRd(uint32 instruction) {
        return (instruction >> 7) & 0x1F;
    }

    uint8 getFunct3(uint32 instruction) {
        return (instruction >> 12) & 0x7;
    }

    uint8 getRs1(uint32 instruction) {
        return (instruction >> 15) & 0x1F;
    }

    uint8 getRs2(uint32 instruction) {
        return (instruction >> 20) & 0x1F;
    }

    uint8 getFunct7(uint32 instruction) {
        return (instruction >> 25) & 0x7F;
    }
};

} // namespace mycpu
