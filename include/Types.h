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

enum class SimulationMode : uint8_t {
    MULTI_CYCLE = 0,
    PIPELINED = 1
};

enum class ExecMode : uint8_t {
    ASSEMBLY = 0,
    BINARY = 1,
    ELF = 2
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
    bool software_interrupt;
    uint32 interrupt_pending_bits;
    uint32 interrupt_enabled_bits;

    PeripheralState() : timer_value(0), timer_interrupt(false),
                         uart_interrupt(false), external_interrupt(false),
                         software_interrupt(false), interrupt_pending_bits(0),
                         interrupt_enabled_bits(0) {}
};

struct MmuState {
    bool paging_enabled;
    uint32 mapped_pages;
    std::vector<std::string> page_mappings;

    MmuState() : paging_enabled(false), mapped_pages(0) {}
};

struct CsrState {
    std::string privilege_mode;
    uint32 mstatus;
    uint32 mie;
    uint32 mip;
    uint32 mtvec;
    uint32 mepc;
    uint32 mcause;
    uint32 mtval;

    CsrState()
        : privilege_mode("M"), mstatus(0), mie(0), mip(0), mtvec(0),
          mepc(0), mcause(0), mtval(0) {}
};

// 模拟器状态（用于前端展示）
struct SimulatorState {
    CpuState state;
    SimulationMode mode;
    uint32 pc;
    std::vector<int32> registers;
    std::unordered_map<Address, uint32> memory_snapshot;
    PipelineStage current_stage;
    PipelineRegisters pipeline_regs;
    CpuStats stats;
    HazardSignals hazard_signals;
    PeripheralState peripherals;
    MmuState mmu;
    CsrState csr;
    std::vector<std::string> execution_trace;
    std::string mode_name;
    bool true_pipeline;
    std::string mode_note;

    // 已解码的流水线指令文本
    std::string ifid_text;
    std::string idex_text;
    std::string exmem_text;
    std::string memwb_text;
    std::string fetch_text;
    std::string decode_text;
    std::string execute_text;
    std::string memory_text;
    std::string writeback_text;
    uint32 ifid_pc;
    uint32 idex_pc;
    uint32 exmem_pc;
    uint32 memwb_pc;
    uint32 fetch_pc;
    uint32 decode_pc;
    uint32 execute_pc;
    uint32 memory_pc;
    uint32 writeback_pc;
    bool ifid_valid;
    bool idex_valid;
    bool exmem_valid;
    bool memwb_valid;
    bool fetch_valid;
    bool decode_valid;
    bool execute_valid;
    bool memory_valid;
    bool writeback_valid;

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
inline SimulatorState::SimulatorState()
    : state(CpuState::HALTED),
      mode(SimulationMode::MULTI_CYCLE),
      pc(0),
      current_stage(PipelineStage::FETCH),
      mode_name("Multi-cycle"),
      true_pipeline(false),
      ifid_pc(0),
      idex_pc(0),
      exmem_pc(0),
      memwb_pc(0),
      fetch_pc(0),
      decode_pc(0),
      execute_pc(0),
      memory_pc(0),
      writeback_pc(0),
      ifid_valid(false),
      idex_valid(false),
      exmem_valid(false),
      memwb_valid(false),
      fetch_valid(false),
      decode_valid(false),
      execute_valid(false),
      memory_valid(false),
      writeback_valid(false) {
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

    auto json_esc = [](const std::string& s) -> std::string {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') r += '\\';
            r += c;
        }
        return r;
    };

    std::ostringstream oss;
    oss << "{";
    oss << "\"pc\":" << pc << ",";
    oss << "\"pcInstructionIndex\":" << (pc / 4) << ",";
    oss << "\"cycle\":" << stats.cycle_count << ",";
    oss << "\"mode\":\"" << (mode == SimulationMode::PIPELINED ? "PIPELINED" : "MULTI_CYCLE") << "\",";
    oss << "\"modeName\":\"" << json_esc(mode_name) << "\",";
    oss << "\"truePipeline\":" << (true_pipeline ? "true" : "false") << ",";
    oss << "\"modeNote\":\"" << json_esc(mode_note) << "\",";
    oss << "\"state\":\"" << state_to_string(state) << "\",";
    oss << "\"registers\":[";
    for (size_t i = 0; i < registers.size(); ++i) {
        if (i > 0) oss << ",";
        oss << registers[i];
    }
    oss << "]";

    oss << ",\"memory\":{";
    if (memory_ptr) {
        bool first = true;
        for (const auto& kv : memory_snapshot) {
            if (!first) oss << ",";
            oss << "\"" << kv.first << "\":" << kv.second;
            first = false;
        }
    }
    oss << "}";

    oss << ",\"pipelineLatches\":{";
    oss << "\"fetch\":{"
        << "\"valid\":" << (fetch_valid ? "true" : "false")
        << ",\"pc\":" << fetch_pc
        << ",\"text\":" << (fetch_text.empty() ? "null" : "\"" + json_esc(fetch_text) + "\"")
        << "},";
    oss << "\"ifid\":{"
        << "\"valid\":" << (ifid_valid ? "true" : "false")
        << ",\"pc\":" << ifid_pc
        << ",\"text\":" << (ifid_text.empty() ? "null" : "\"" + json_esc(ifid_text) + "\"")
        << "}";
    oss << ",\"idex\":{"
        << "\"valid\":" << (idex_valid ? "true" : "false")
        << ",\"pc\":" << idex_pc
        << ",\"text\":" << (idex_text.empty() ? "null" : "\"" + json_esc(idex_text) + "\"")
        << "}";
    oss << ",\"exmem\":{"
        << "\"valid\":" << (exmem_valid ? "true" : "false")
        << ",\"pc\":" << exmem_pc
        << ",\"text\":" << (exmem_text.empty() ? "null" : "\"" + json_esc(exmem_text) + "\"")
        << "}";
    oss << ",\"memwb\":{"
        << "\"valid\":" << (memwb_valid ? "true" : "false")
        << ",\"pc\":" << memwb_pc
        << ",\"text\":" << (memwb_text.empty() ? "null" : "\"" + json_esc(memwb_text) + "\"")
        << "}";
    oss << "}";

    oss << ",\"stageViews\":{";
    oss << "\"if\":{"
        << "\"valid\":" << (fetch_valid ? "true" : "false")
        << ",\"pc\":" << fetch_pc
        << ",\"text\":" << (fetch_text.empty() ? "null" : "\"" + json_esc(fetch_text) + "\"")
        << "}";
    oss << ",\"id\":{"
        << "\"valid\":" << (decode_valid ? "true" : "false")
        << ",\"pc\":" << decode_pc
        << ",\"text\":" << (decode_text.empty() ? "null" : "\"" + json_esc(decode_text) + "\"")
        << "}";
    oss << ",\"ex\":{"
        << "\"valid\":" << (execute_valid ? "true" : "false")
        << ",\"pc\":" << execute_pc
        << ",\"text\":" << (execute_text.empty() ? "null" : "\"" + json_esc(execute_text) + "\"")
        << "}";
    oss << ",\"mem\":{"
        << "\"valid\":" << (memory_valid ? "true" : "false")
        << ",\"pc\":" << memory_pc
        << ",\"text\":" << (memory_text.empty() ? "null" : "\"" + json_esc(memory_text) + "\"")
        << "}";
    oss << ",\"wb\":{"
        << "\"valid\":" << (writeback_valid ? "true" : "false")
        << ",\"pc\":" << writeback_pc
        << ",\"text\":" << (writeback_text.empty() ? "null" : "\"" + json_esc(writeback_text) + "\"")
        << "}";
    oss << "}";

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
    oss << "],\"notes\":[";
    if (!hazard_signals.description.empty()) {
        oss << "\"" << json_esc(hazard_signals.description) << "\"";
    }
    oss << "]";
    oss << "}";

    oss << ",\"bubble\":{\"active\":false,\"stage\":\"\",\"reason\":\"\"}";

    oss << ",\"stats\":{\"cycles\":" << stats.cycle_count
        << ",\"instructions\":" << stats.instruction_count
        << ",\"stalls\":" << stats.stall_count
        << ",\"flushes\":" << stats.flush_count
        << ",\"branchTaken\":" << stats.branch_taken
        << ",\"branchNotTaken\":" << stats.branch_not_taken << "}";

    oss << ",\"stageIndex\":" << (int)current_stage;
    oss << ",\"stage\":\"" << (current_stage == PipelineStage::FETCH ? "IF" :
                               current_stage == PipelineStage::DECODE ? "ID" :
                               current_stage == PipelineStage::EXECUTE ? "EX" :
                               current_stage == PipelineStage::MEMORY ? "MEM" : "WB") << "\"";

    oss << ",\"peripherals\":{";
    oss << "\"uart_buffer\":\"" << json_esc(peripherals.uart_buffer) << "\"";
    oss << ",\"timer_value\":" << peripherals.timer_value;
    oss << ",\"timer_interrupt\":" << (peripherals.timer_interrupt ? "true" : "false");
    oss << ",\"uart_interrupt\":" << (peripherals.uart_interrupt ? "true" : "false");
    oss << ",\"external_interrupt\":" << (peripherals.external_interrupt ? "true" : "false");
    oss << ",\"software_interrupt\":" << (peripherals.software_interrupt ? "true" : "false");
    oss << ",\"interrupt_pending_bits\":" << peripherals.interrupt_pending_bits;
    oss << ",\"interrupt_enabled_bits\":" << peripherals.interrupt_enabled_bits;
    oss << "}";

    oss << ",\"mmu\":{";
    oss << "\"paging_enabled\":" << (mmu.paging_enabled ? "true" : "false");
    oss << ",\"mapped_pages\":" << mmu.mapped_pages;
    oss << ",\"page_mappings\":[";
    for (size_t i = 0; i < mmu.page_mappings.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_esc(mmu.page_mappings[i]) << "\"";
    }
    oss << "]}";

    oss << ",\"csr\":{";
    oss << "\"privilege_mode\":\"" << json_esc(csr.privilege_mode) << "\"";
    oss << ",\"mstatus\":" << csr.mstatus;
    oss << ",\"mie\":" << csr.mie;
    oss << ",\"mip\":" << csr.mip;
    oss << ",\"mtvec\":" << csr.mtvec;
    oss << ",\"mepc\":" << csr.mepc;
    oss << ",\"mcause\":" << csr.mcause;
    oss << ",\"mtval\":" << csr.mtval;
    oss << "}";

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
