#pragma once

#include "../include/Types.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace mycpu {

class Assembler {
public:
    Assembler();

    struct AssembledProgram {
        std::vector<uint32> instructions;
        std::vector<std::string> errors;
        std::vector<std::string> labels;
        bool success;
    };

    AssembledProgram assemble(const std::string& source);

    static std::string getLastError() { return last_error_; }
    static void clearError() { last_error_.clear(); }

private:
    struct ParsedLine {
        std::string mnemonic;
        std::vector<std::string> operands;
        std::string label;
        int line_number;
    };

    ParsedLine parseLine(const std::string& line, int line_number);
    uint32 encodeInstruction(const ParsedLine& parsed, const std::unordered_map<std::string, uint32>& labels,
                             uint32 instr_pc);
    int32 resolveBranchOffset(const std::string& target, uint32 instr_pc,
                              const std::unordered_map<std::string, uint32>& labels);
    uint32 encodeRType(const std::string& mnemonic, uint8 rd, uint8 rs1, uint8 rs2);
    uint32 encodeIType(const std::string& mnemonic, uint8 rd, uint8 rs1, int32 imm);
    uint32 encodeSType(const std::string& mnemonic, uint8 rs1, uint8 rs2, int32 imm);
    uint32 encodeBType(const std::string& mnemonic, uint8 rs1, uint8 rs2, int32 imm);
    uint32 encodeUType(const std::string& mnemonic, uint8 rd, int32 imm);
    uint32 encodeJType(const std::string& mnemonic, uint8 rd, int32 imm);
    uint32 encodeSystemType(const std::string& mnemonic, uint8 rd, uint8 rs1, uint16 csr);

    uint8 parseRegister(const std::string& reg);
    uint16 parseCsrAddress(const std::string& csr_str);
    int32 parseImmediate(const std::string& imm_str);

    static std::string last_error_;
    std::unordered_map<std::string, uint32> labels_;
};

} // namespace mycpu
