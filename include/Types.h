#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <array>
#include <unordered_map>

namespace mycpu {

// 基本类型别名
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

// 地址类型
using Address = uint32_t;
using Word = uint32_t;
using HalfWord = uint16_t;
using Byte = uint8_t;

// 通用寄存器数量 (RISC-V 标准)
constexpr size_t NUM_REGISTERS = 32;

// 内存大小 (16KB)
constexpr size_t MEMORY_SIZE = 256 * 1024;

// 内存区域定义
enum class MemorySegment : uint8_t {
    TEXT = 0,    // 代码段
    DATA = 1,    // 数据段
    STACK = 2,   // 栈段
    HEAP = 3,    // 堆段
    MMIO = 4,    // 内存映射IO
    OTHER = 5
};

// 执行状态
enum class CpuState : uint8_t {
    RUNNING = 0,
    HALTED = 1,
    WAITING = 2,
    EXCEPTION = 3,
    INTERRUPT = 4
};

// 指令操作码 (简化版 RISC-V I 型)
enum class Opcode : uint8_t {
    LOAD = 0x03,
    FENCE = 0x0F,
    STORE = 0x23,
    BRANCH = 0x63,
    JAL = 0x6F,
    JALR = 0x67,
    LUI = 0x37,
    AUIPC = 0x17,
    OP_IMM = 0x13,
    OP = 0x33,
    SYSTEM = 0x73,
    HALT = 0xFF  // 自定义停机指令
};

// 流水线阶段
enum class PipelineStage : uint8_t {
    FETCH = 0,
    DECODE = 1,
    EXECUTE = 2,
    MEMORY = 3,
    WRITEBACK = 4
};

// 异常类型
enum class ExceptionType : uint8_t {
    NONE = 0,
    ILLEGAL_INSTRUCTION = 1,
    LOAD_ADDRESS_MISALIGNED = 2,
    STORE_ADDRESS_MISALIGNED = 3,
    LOAD_ACCESS_FAULT = 4,
    STORE_ACCESS_FAULT = 5,
    BRANCH_MISALIGNED = 6,
    SYSTEM_CALL = 7,
    BREAKPOINT = 8
};

// 指令结构
struct Instruction {
    Opcode opcode;
    uint32 raw;              // 原始32位编码
    uint32 pc;               // 指令地址

    // 解码字段
    uint8 rd;                // 目标寄存器
    uint8 rs1;               // 源寄存器1
    uint8 rs2;               // 源寄存器2
    uint8 funct3;             // 功能码
    uint8 funct7;             // 扩展功能码
    int32 imm;                // 立即数

    // 指令文本（用于显示）
    std::string mnemonic;
    std::string disassembly;

    Instruction() : opcode(Opcode::SYSTEM), raw(0), pc(0),
                    rd(0), rs1(0), rs2(0), funct3(0), funct7(0), imm(0) {}
};

// 流水线寄存器
struct PipelineRegisters {
    // IF/ID
    uint32 if_id_pc;
    Instruction if_id_instruction;
    bool if_id_valid;

    // ID/EX
    uint32 id_ex_pc;
    Instruction id_ex_instruction;
    int32 id_ex_alu_result;
    int32 id_ex_reg_data1;
    int32 id_ex_reg_data2;
    bool id_ex_valid;

    // EX/MEM
    uint32 ex_mem_pc;
    Instruction ex_mem_instruction;
    int32 ex_mem_alu_result;
    int32 ex_mem_mem_write_data;
    bool ex_mem_mem_read;
    bool ex_mem_mem_write;
    bool ex_mem_valid;

    // MEM/WB
    uint32 mem_wb_pc;
    Instruction mem_wb_instruction;
    int32 mem_wb_alu_result;
    int32 mem_wb_mem_data;
    bool mem_wb_reg_write;
    bool mem_wb_valid;

    PipelineRegisters() : if_id_pc(0), id_ex_pc(0), ex_mem_pc(0), mem_wb_pc(0),
                          if_id_valid(false), id_ex_valid(false),
                          ex_mem_valid(false), mem_wb_valid(false) {}
};

// CPU 统计信息
struct CpuStats {
    uint64 cycle_count;
    uint64 instruction_count;
    uint64 stall_count;
    uint64 flush_count;
    uint64 branch_taken;
    uint64 branch_not_taken;

    CpuStats() : cycle_count(0), instruction_count(0), stall_count(0),
                 flush_count(0), branch_taken(0), branch_not_taken(0) {}
};

// 冒险检测信号
struct HazardSignals {
    bool stall;
    bool flush;
    uint8 forward_from_exmem;   // EX/MEM 向前传递
    uint8 forward_from_memwb;  // MEM/WB 向前传递
    std::string description;

    HazardSignals() : stall(false), flush(false),
                      forward_from_exmem(0), forward_from_memwb(0) {}
};

// 外设状态
struct PeripheralState {
    std::string uart_buffer;
    uint32 timer_value;
    bool timer_interrupt;
    bool uart_interrupt;
    bool external_interrupt;

    PeripheralState() : timer_value(0), timer_interrupt(false),
                         uart_interrupt(false), external_interrupt(false) {}
};

// 模拟器状态（用于前端展示）
struct SimulatorState {
    CpuState state;
    uint32 pc;
    std::vector<int32> registers;
    std::unordered_map<Address, uint32> memory_snapshot;
    PipelineStage current_stage;
    PipelineRegisters pipeline_regs;
    CpuStats stats;
    HazardSignals hazard_signals;
    PeripheralState peripherals;
    std::vector<std::string> execution_trace;

    // 已解码的流水线指令文本
    std::string ifid_text;
    std::string idex_text;
    std::string exmem_text;
    std::string memwb_text;

    SimulatorState();
    std::string toJson(const void* memory_ptr = nullptr) const;
};

// 指令描述
struct InstructionInfo {
    std::string name;
    std::string format;
    std::string description;
    Opcode opcode;
    uint8 funct3;
    uint8 funct7;
};

// 支持的指令列表
const std::vector<InstructionInfo>& getSupportedInstructions();

// 内联实现
inline SimulatorState::SimulatorState() : state(CpuState::HALTED), pc(0), current_stage(PipelineStage::FETCH) {
    registers.resize(32, 0);
}

inline std::string SimulatorState::toJson(const void* memory_ptr) const {
    auto state_to_string = [](CpuState value) -> const char* {
        switch (value) {
            case CpuState::RUNNING: return "RUNNING";
            case CpuState::HALTED: return "HALTED";
            case CpuState::WAITING: return "WAITING";
            case CpuState::EXCEPTION: return "EXCEPTION";
            case CpuState::INTERRUPT: return "INTERRUPT";
            default: return "UNKNOWN";
        }
    };

    std::ostringstream oss;
    oss << "{";
    oss << "\"pc\":" << pc << ",";
    oss << "\"pcInstructionIndex\":" << (pc / 4) << ",";
    oss << "\"cycle\":" << stats.cycle_count << ",";
    oss << "\"state\":\"" << state_to_string(state) << "\",";
    oss << "\"registers\":[";
    for (size_t i = 0; i < registers.size(); ++i) {
        if (i > 0) oss << ",";
        oss << registers[i];
    }
    oss << "]";

    // 内存快照（通过 void* 避免前向声明循环依赖）
    oss << ",\"memory\":{";
    if (memory_ptr) {
        // 将 memory_snapshot 的 unordered_map 序列化为 JSON
        bool first = true;
        for (const auto& kv : memory_snapshot) {
            if (!first) oss << ",";
            oss << "\"" << kv.first << "\":" << kv.second;
            first = false;
        }
    }
    oss << "}";

    // 反斜杠转义工具（内联 lambda，C++14+）
    auto json_esc = [](const std::string& s) -> std::string {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') r += '\\';
            r += c;
        }
        return r;
    };

    // 流水线寄存器（已解码的文本由 CPU 填充）
    oss << ",\"pipelineLatches\":{";
    oss << "\"ifid\":"  << (ifid_text.empty()  ? "null" : "\"" + json_esc(ifid_text)  + "\"");
    oss << ",\"idex\":"  << (idex_text.empty()  ? "null" : "\"" + json_esc(idex_text)  + "\"");
    oss << ",\"exmem\":" << (exmem_text.empty() ? "null" : "\"" + json_esc(exmem_text) + "\"");
    oss << ",\"memwb\":" << (memwb_text.empty() ? "null" : "\"" + json_esc(memwb_text) + "\"");
    oss << "}";

    // 冒险信号
    oss << ",\"flowSignals\":{";
    oss << "\"stall\":" << (hazard_signals.stall ? "true" : "false") << ",";
    oss << "\"flush\":" << (hazard_signals.flush ? "true" : "false") << ",";
    oss << "\"forwarding\":[";
    bool first_fwd = true;
    if (hazard_signals.forward_from_exmem != 0) {
        oss << "\"EX/MEM->ID/EX (x" << (int)hazard_signals.forward_from_exmem << ")\"";
        first_fwd = false;
    }
    if (hazard_signals.forward_from_memwb != 0) {
        if (!first_fwd) oss << ",";
        oss << "\"MEM/WB->ID/EX (x" << (int)hazard_signals.forward_from_memwb << ")\"";
    }
    oss << "],\"notes\":[]";
    oss << "}";

    // Bubble
    oss << ",\"bubble\":{\"active\":false,\"stage\":\"\",\"reason\":\"\"}";

    // 统计
    oss << ",\"stats\":{\"cycles\":" << stats.cycle_count
        << ",\"instructions\":" << stats.instruction_count
        << ",\"stalls\":" << stats.stall_count << "}";

    // 当前阶段
    oss << ",\"stageIndex\":" << (int)current_stage;
    oss << ",\"stage\":\"" << (current_stage == PipelineStage::FETCH ? "IF" :
                               current_stage == PipelineStage::DECODE ? "ID" :
                               current_stage == PipelineStage::EXECUTE ? "EX" :
                               current_stage == PipelineStage::MEMORY ? "MEM" : "WB") << "\"";

    // 外设状态
    oss << ",\"peripherals\":{";
    oss << "\"uart_buffer\":\"" << json_esc(peripherals.uart_buffer) << "\"";
    oss << ",\"timer_value\":" << peripherals.timer_value;
    oss << ",\"timer_interrupt\":" << (peripherals.timer_interrupt ? "true" : "false");
    oss << ",\"uart_interrupt\":" << (peripherals.uart_interrupt ? "true" : "false");
    oss << ",\"external_interrupt\":" << (peripherals.external_interrupt ? "true" : "false");
    oss << "}";

    // trace
    oss << ",\"trace\":[";
    for (size_t i = 0; i < execution_trace.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_esc(execution_trace[i]) << "\"";
    }
    oss << "]";

    oss << "}";
    return oss.str();
}

inline const std::vector<InstructionInfo>& getSupportedInstructions() {
    static std::vector<InstructionInfo> instructions = {
        {"add", "rd, rs1, rs2", "加法", Opcode::OP, 0, 0},
        {"sub", "rd, rs1, rs2", "减法", Opcode::OP, 0, 0x20},
        {"addi", "rd, rs1, imm", "立即数加法", Opcode::OP_IMM, 0, 0},
        {"and", "rd, rs1, rs2", "按位与", Opcode::OP, 7, 0},
        {"or", "rd, rs1, rs2", "按位或", Opcode::OP, 6, 0},
        {"xor", "rd, rs1, rs2", "按位异或", Opcode::OP, 4, 0},
        {"lw", "rd, offset(rs1)", "加载字", Opcode::LOAD, 2, 0},
        {"sw", "rs2, offset(rs1)", "存储字", Opcode::STORE, 2, 0},
        {"beq", "rs1, rs2, offset", "相等分支", Opcode::BRANCH, 0, 0},
        {"bne", "rs1, rs2, offset", "不等分支", Opcode::BRANCH, 1, 0},
        {"lui", "rd, imm", "加载高位", Opcode::LUI, 0, 0},
        {"auipc", "rd, imm", "PC加立即数", Opcode::AUIPC, 0, 0},
        {"jal", "rd, offset", "跳转并链接", Opcode::JAL, 0, 0},
        {"jalr", "rd, rs1, offset", "间接跳转", Opcode::JALR, 0, 0},
    };
    return instructions;
}

} // namespace mycpu
