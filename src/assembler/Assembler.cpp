#include "Assembler.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>

namespace mycpu {

std::string toHex(uint32 value);

std::string Assembler::last_error_;

Assembler::Assembler() {}

Assembler::AssembledProgram Assembler::assemble(const std::string& source) {
    AssembledProgram result;
    result.success = true;
    labels_.clear();

    std::vector<ParsedLine> parsed_lines;
    std::unordered_map<std::string, uint32> label_positions;
    std::vector<std::string> lines;
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    uint32 pc = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        ParsedLine parsed = parseLine(lines[i], static_cast<int>(i + 1));

        if (!parsed.label.empty()) {
            if (label_positions.find(parsed.label) != label_positions.end()) {
                std::ostringstream oss;
                oss << "Line " << parsed.line_number << ": duplicate label '" << parsed.label << "'";
                result.errors.push_back(oss.str());
                result.success = false;
            }
            label_positions[parsed.label] = pc;
        }

        if (!parsed.mnemonic.empty()) {
            // 婵″倹鐏夐弰?li 閹稿洣鎶ら敍灞藉讲閼虫垝绱扮悮顐㈢潔瀵偓娑撹桨琚遍弶鈩冨瘹娴犮倧绱檒ui + addi閿?
            if (parsed.mnemonic == "li") {
                // 鐟欙絾鐎界粩瀣祮閺佸府绱濋懟銉ㄧТ閸?12-bit 閺堝顑侀崣鐤瘱閸ヨ揪绱濋崚娆忓窗閻劋琚遍弶鈩冨瘹娴?
                int32 imm = 0;
                if (parsed.operands.size() >= 2) {
                    imm = parseImmediate(parsed.operands[1]);
                }
                if (imm >= -2048 && imm <= 2047) {
                    pc += 4;
                } else {
                    pc += 8;
                }
            } else {
                pc += 4;
            }
        }
    }

    uint32 instr_pc = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        ParsedLine parsed = parseLine(lines[i], static_cast<int>(i + 1));

        if (!parsed.mnemonic.empty()) {
            try {
                // Special-case: expand "li" into lui+addi when immediate doesn't fit 12-bit
                if (parsed.mnemonic == "li") {
                    uint8 rd = parseRegister(parsed.operands[0]);
                    int32 imm = parseImmediate(parsed.operands[1]);
                    // If imm fits into signed 12-bit, emit a single addi
                    if (imm >= -2048 && imm <= 2047) {
                        uint32 enc = encodeIType("addi", rd, 0, imm);
                        result.instructions.push_back(enc);
                        instr_pc += 4;
                    } else {
                        // Compute upper 20 bits for LUI with correct sign adjustment
                        int32 hi = (imm + (1 << 11)) >> 12; // round to nearest for correct lower sign
                        int32 lo = imm - (hi << 12);
                        uint32 enc_lui = encodeUType("lui", rd, hi << 12);
                        uint32 enc_addi = encodeIType("addi", rd, rd, lo);
                        result.instructions.push_back(enc_lui);
                        result.instructions.push_back(enc_addi);
                        instr_pc += 8;
                    }
                } else {
                    uint32 encoded = encodeInstruction(parsed, label_positions, instr_pc);
                    result.instructions.push_back(encoded);
                    instr_pc += 4;
                }
            } catch (const std::exception& e) {
                std::ostringstream oss;
                oss << "Line " << parsed.line_number << ": encode error - " << e.what();
                result.errors.push_back(oss.str());
                result.success = false;
            }
        }
    }

    for (const auto& kv : label_positions) {
        result.labels.push_back(kv.first + ": 0x" + toHex(kv.second));
    }

    if (!result.success) {
        result.success = false;
    }

    return result;
}

Assembler::ParsedLine Assembler::parseLine(const std::string& raw_line, int line_number) {
    ParsedLine result;
    result.line_number = line_number;

    std::string line = raw_line;
    // 缁夊娅?Windows 鐞涘苯鐔惃?\r
    while (!line.empty() && line.back() == '\r') line.pop_back();
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);

    size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
    }
    comment_pos = line.find(';');
    if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
    }

    line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    while (!line.empty() && std::isspace(line.back())) {
        line.pop_back();
    }

    if (line.empty()) {
        return result;
    }

    size_t label_pos = line.find(':');
    if (label_pos != std::string::npos) {
        result.label = line.substr(0, label_pos);
        line = line.substr(label_pos + 1);
        while (!line.empty() && std::isspace(line[0])) {
            line.erase(line.begin(), line.begin() + 1);
        }
    }

    if (line.empty()) {
        return result;
    }

    std::istringstream iss(line);
    std::string mnemonic;
    iss >> mnemonic;
    result.mnemonic = mnemonic;

    std::string operand_str;
    if (std::getline(iss, operand_str)) {
        std::replace(operand_str.begin(), operand_str.end(), '(', ' ');
        std::replace(operand_str.begin(), operand_str.end(), ')', ' ');
        std::replace(operand_str.begin(), operand_str.end(), ',', ' ');

        std::istringstream op_stream(operand_str);
        std::string operand;
        while (op_stream >> operand) {
            result.operands.push_back(operand);
        }
    }

    return result;
}

int32 Assembler::resolveBranchOffset(const std::string& target, uint32 instr_pc,
                                     const std::unordered_map<std::string, uint32>& labels) {
    auto it = labels.find(target);
    if (it != labels.end()) {
        return static_cast<int32>(it->second) - static_cast<int32>(instr_pc);
    }
    return parseImmediate(target);
}

uint32 Assembler::encodeInstruction(const ParsedLine& parsed,
                                     const std::unordered_map<std::string, uint32>& labels,
                                     uint32 instr_pc) {
    const std::string& mnemonic = parsed.mnemonic;

    if (mnemonic == "add" || mnemonic == "sub" || mnemonic == "sll" || mnemonic == "slt" ||
        mnemonic == "sltu" || mnemonic == "xor" || mnemonic == "srl" || mnemonic == "sra" ||
        mnemonic == "or" || mnemonic == "and") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint8 rs1 = parseRegister(parsed.operands[1]);
        uint8 rs2 = parseRegister(parsed.operands[2]);
        return encodeRType(mnemonic, rd, rs1, rs2);
    }

    // R-type M-extension
    if (mnemonic == "mul" || mnemonic == "mulh" || mnemonic == "mulhsu" || mnemonic == "mulhu" ||
        mnemonic == "div" || mnemonic == "divu" || mnemonic == "rem" || mnemonic == "remu") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint8 rs1 = parseRegister(parsed.operands[1]);
        uint8 rs2 = parseRegister(parsed.operands[2]);
        return encodeRType(mnemonic, rd, rs1, rs2);
    }

    if (mnemonic == "addi" || mnemonic == "slli" || mnemonic == "slti" || mnemonic == "sltiu" ||
        mnemonic == "xori" || mnemonic == "srli" || mnemonic == "srai" || mnemonic == "ori" ||
        mnemonic == "andi") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint8 rs1 = parseRegister(parsed.operands[1]);
        int32 imm = parseImmediate(parsed.operands[2]);
        return encodeIType(mnemonic, rd, rs1, imm);
    }

    if (mnemonic == "lb" || mnemonic == "lh" || mnemonic == "lw" || mnemonic == "lbu" || mnemonic == "lhu") {
        uint8 rd = parseRegister(parsed.operands[0]);
        int32 imm;
        uint8 rs1;
        if (parsed.operands.size() == 2) {
            imm = parseImmediate(parsed.operands[1]);
            rs1 = 0;
        } else {
            imm = parseImmediate(parsed.operands[1]);
            rs1 = parseRegister(parsed.operands[2]);
        }
        return encodeIType(mnemonic, rd, rs1, imm);
    }

    if (mnemonic == "sb" || mnemonic == "sh" || mnemonic == "sw") {
        uint8 rs2 = parseRegister(parsed.operands[0]);
        int32 imm;
        uint8 rs1;
        if (parsed.operands.size() == 2) {
            imm = parseImmediate(parsed.operands[1]);
            rs1 = 0;
        } else {
            imm = parseImmediate(parsed.operands[1]);
            rs1 = parseRegister(parsed.operands[2]);
        }
        return encodeSType(mnemonic, rs1, rs2, imm);
    }

    if (mnemonic == "beq" || mnemonic == "bne" || mnemonic == "blt" || mnemonic == "bge" ||
        mnemonic == "bltu" || mnemonic == "bgeu") {
        uint8 rs1 = parseRegister(parsed.operands[0]);
        uint8 rs2 = parseRegister(parsed.operands[1]);
        int32 imm = resolveBranchOffset(parsed.operands[2], instr_pc, labels);
        return encodeBType(mnemonic, rs1, rs2, imm);
    }

    if (mnemonic == "lui" || mnemonic == "auipc") {
        uint8 rd = parseRegister(parsed.operands[0]);
        int32 imm = parseImmediate(parsed.operands[1]);
        return encodeUType(mnemonic, rd, imm);
    }

    if (mnemonic == "jal") {
        uint8 rd = parseRegister(parsed.operands[0]);
        int32 imm = resolveBranchOffset(parsed.operands[1], instr_pc, labels);
        return encodeJType(mnemonic, rd, imm);
    }

    if (mnemonic == "jalr") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint8 rs1 = parseRegister(parsed.operands[1]);
        int32 imm = parseImmediate(parsed.operands[2]);
        return encodeIType(mnemonic, rd, rs1, imm);
    }

    if (mnemonic == "ecall") {
        return 0x00000073;
    }

    if (mnemonic == "ebreak") {
        return 0x00100073;
    }

    if (mnemonic == "mret") {
        return 0x30200073;
    }

    if (mnemonic == "csrrw" || mnemonic == "csrrs" || mnemonic == "csrrc") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint16 csr = parseCsrAddress(parsed.operands[1]);
        uint8 rs1 = parseRegister(parsed.operands[2]);
        return encodeSystemType(mnemonic, rd, rs1, csr);
    }

    if (mnemonic == "csrrwi" || mnemonic == "csrrsi" || mnemonic == "csrrci") {
        uint8 rd = parseRegister(parsed.operands[0]);
        uint16 csr = parseCsrAddress(parsed.operands[1]);
        uint8 zimm = static_cast<uint8>(parseImmediate(parsed.operands[2]) & 0x1F);
        return encodeSystemType(mnemonic, rd, zimm, csr);
    }

    if (mnemonic == "li") {
        uint8 rd = parseRegister(parsed.operands[0]);
        int32 imm = parseImmediate(parsed.operands[1]);
        return encodeIType("addi", rd, 0, imm);
    }

    if (mnemonic == "nop") {
        return 0x00000013;
    }

    if (mnemonic == "halt") {
        return 0x000000FF;
    }

    throw std::runtime_error("Unknown instruction: " + mnemonic);
}

uint32 Assembler::encodeRType(const std::string& mnemonic, uint8 rd, uint8 rs1, uint8 rs2) {
    uint8 funct3 = 0;
    uint8 funct7 = 0;

    if (mnemonic == "add") { funct3 = 0; funct7 = 0; }
    else if (mnemonic == "sub") { funct3 = 0; funct7 = 0x20; }
    else if (mnemonic == "sll") { funct3 = 1; funct7 = 0; }
    else if (mnemonic == "slt") { funct3 = 2; funct7 = 0; }
    else if (mnemonic == "sltu") { funct3 = 3; funct7 = 0; }
    else if (mnemonic == "xor") { funct3 = 4; funct7 = 0; }
    else if (mnemonic == "srl") { funct3 = 5; funct7 = 0; }
    else if (mnemonic == "sra") { funct3 = 5; funct7 = 0x20; }
    else if (mnemonic == "or") { funct3 = 6; funct7 = 0; }
    else if (mnemonic == "and") { funct3 = 7; funct7 = 0; }

    // M-extension operations use funct7 = 0x01
    else if (mnemonic == "mul") { funct3 = 0; funct7 = 0x01; }
    else if (mnemonic == "mulh") { funct3 = 1; funct7 = 0x01; }
    else if (mnemonic == "mulhsu") { funct3 = 2; funct7 = 0x01; }
    else if (mnemonic == "mulhu") { funct3 = 3; funct7 = 0x01; }
    else if (mnemonic == "div") { funct3 = 4; funct7 = 0x01; }
    else if (mnemonic == "divu") { funct3 = 5; funct7 = 0x01; }
    else if (mnemonic == "rem") { funct3 = 6; funct7 = 0x01; }
    else if (mnemonic == "remu") { funct3 = 7; funct7 = 0x01; }

    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0x33;
}

uint32 Assembler::encodeIType(const std::string& mnemonic, uint8 rd, uint8 rs1, int32 imm) {
    uint8 funct3 = 0;
    uint32 opcode = 0x13;

    if (mnemonic == "addi") { funct3 = 0; }
    else if (mnemonic == "slli") { funct3 = 1; imm &= 0x1F; }
    else if (mnemonic == "slti") { funct3 = 2; }
    else if (mnemonic == "sltiu") { funct3 = 3; }
    else if (mnemonic == "xori") { funct3 = 4; }
    else if (mnemonic == "srli" || mnemonic == "srai") {
        funct3 = 5;
        if (mnemonic == "srai") {
            imm |= 0x400;
        }
    }
    else if (mnemonic == "ori") { funct3 = 6; }
    else if (mnemonic == "andi") { funct3 = 7; }
    else if (mnemonic == "lb") { funct3 = 0; opcode = 0x03; }
    else if (mnemonic == "lh") { funct3 = 1; opcode = 0x03; }
    else if (mnemonic == "lw") { funct3 = 2; opcode = 0x03; }
    else if (mnemonic == "lbu") { funct3 = 4; opcode = 0x03; }
    else if (mnemonic == "lhu") { funct3 = 5; opcode = 0x03; }
    else if (mnemonic == "jalr") { funct3 = 0; opcode = 0x67; }

    uint32 imm_encoded = static_cast<uint32>(imm) & 0xFFF;
    return (imm_encoded << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

uint32 Assembler::encodeSType(const std::string& mnemonic, uint8 rs1, uint8 rs2, int32 imm) {
    uint8 funct3 = 0;

    if (mnemonic == "sb") { funct3 = 0; }
    else if (mnemonic == "sh") { funct3 = 1; }
    else if (mnemonic == "sw") { funct3 = 2; }

    uint32 imm_encoded = static_cast<uint32>(imm) & 0xFFF;
    uint32 imm_high = (imm_encoded >> 5) & 0x7F;
    uint32 imm_low = imm_encoded & 0x1F;

    return (imm_high << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_low << 7) | 0x23;
}

uint32 Assembler::encodeBType(const std::string& mnemonic, uint8 rs1, uint8 rs2, int32 imm) {
    uint8 funct3 = 0;

    if (mnemonic == "beq") { funct3 = 0; }
    else if (mnemonic == "bne") { funct3 = 1; }
    else if (mnemonic == "blt") { funct3 = 4; }
    else if (mnemonic == "bge") { funct3 = 5; }
    else if (mnemonic == "bltu") { funct3 = 6; }
    else if (mnemonic == "bgeu") { funct3 = 7; }

    uint32 imm_encoded = static_cast<uint32>(imm) & 0x1FFF;
    uint32 bit11 = (imm_encoded >> 11) & 0x1;
    uint32 bits1_4 = (imm_encoded >> 1) & 0xF;
    uint32 bits5_10 = (imm_encoded >> 5) & 0x3F;
    uint32 bit12 = (imm_encoded >> 12) & 0x1;

    return ((bit12 & 0x1) << 31) | ((bits5_10 & 0x3F) << 25) | (rs2 << 20) | (rs1 << 15) |
           ((funct3 & 0x7) << 12) | ((bits1_4 & 0xF) << 8) | ((bit11 & 0x1) << 7) | 0x63;
}

uint32 Assembler::encodeUType(const std::string& mnemonic, uint8 rd, int32 imm) {
    uint32 opcode = (mnemonic == "lui") ? 0x37 : 0x17;
    return ((static_cast<uint32>(imm) >> 12) << 12) | (rd << 7) | opcode;
}

uint32 Assembler::encodeJType(const std::string& mnemonic, uint8 rd, int32 imm) {
    (void)mnemonic;
    uint32 imm_encoded = static_cast<uint32>(imm) & 0x1FFFFF;
    uint32 bit19_12 = (imm_encoded >> 12) & 0xFF;
    uint32 bit11 = (imm_encoded >> 11) & 0x1;
    uint32 bit10_1 = (imm_encoded >> 1) & 0x3FF;

    return ((imm_encoded >> 20) & 0x1) << 31 | (bit10_1 << 21) | (bit11 << 20) |
           (bit19_12 << 12) | (rd << 7) | 0x6F;
}

uint32 Assembler::encodeSystemType(const std::string& mnemonic, uint8 rd, uint8 rs1, uint16 csr) {
    uint8 funct3 = 0;
    if (mnemonic == "csrrw") funct3 = 0x1;
    else if (mnemonic == "csrrs") funct3 = 0x2;
    else if (mnemonic == "csrrc") funct3 = 0x3;
    else if (mnemonic == "csrrwi") funct3 = 0x5;
    else if (mnemonic == "csrrsi") funct3 = 0x6;
    else if (mnemonic == "csrrci") funct3 = 0x7;
    return (static_cast<uint32>(csr) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0x73;
}

uint8 Assembler::parseRegister(const std::string& reg) {
    if (reg.empty()) {
        throw std::runtime_error("Register operand is empty");
    }

    std::string r = reg;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);

    if (r[0] == 'x' || r[0] == 'r') {
        int num = std::stoi(r.substr(1));
        if (num < 0 || num > 31) {
            throw std::runtime_error("Invalid register index: " + r);
        }
        return static_cast<uint8>(num);
    }

    static const std::unordered_map<std::string, uint8> alias_map = {
        {"zero", 0}, {"ra", 1}, {"sp", 2}, {"gp", 3}, {"tp", 4},
        {"t0", 5}, {"t1", 6}, {"t2", 7},
        {"s0", 8}, {"s1", 9},
        {"a0", 10}, {"a1", 11}, {"a2", 12}, {"a3", 13}, {"a4", 14}, {"a5", 15},
        {"a6", 16}, {"a7", 17},
        {"s2", 18}, {"s3", 19}, {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23},
        {"s8", 24}, {"s9", 25}, {"s10", 26}, {"s11", 27},
        {"t3", 28}, {"t4", 29}, {"t5", 30}, {"t6", 31}
    };

    auto it = alias_map.find(r);
    if (it != alias_map.end()) {
        return it->second;
    }

    throw std::runtime_error("Unknown register name: " + reg);
}

uint16 Assembler::parseCsrAddress(const std::string& csr_str) {
    std::string s = csr_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    static const std::unordered_map<std::string, uint16> csr_map = {
        {"mstatus", 0x300}, {"mie", 0x304}, {"mtvec", 0x305}, {"mepc", 0x341},
        {"mcause", 0x342}, {"mtval", 0x343}, {"mip", 0x344}, {"cycle", 0xC00},
        {"instret", 0xC02}, {"mcycle", 0xB00}, {"minstret", 0xB02}
    };
    auto it = csr_map.find(s);
    if (it != csr_map.end()) {
        return it->second;
    }
    return static_cast<uint16>(parseImmediate(csr_str) & 0xFFF);
}

int32 Assembler::parseImmediate(const std::string& imm_str) {
    if (imm_str.empty()) {
        return 0;
    }

    std::string s = imm_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s.find("0x") == 0) {
        return static_cast<int32>(std::stoll(s.substr(2), nullptr, 16));
    }

    if (s.find("0b") == 0) {
        return static_cast<int32>(std::stoll(s.substr(2), nullptr, 2));
    }

    return static_cast<int32>(std::stoll(s));
}

std::string toHex(uint32 value) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << value;
    return oss.str();
}

} // namespace mycpu
