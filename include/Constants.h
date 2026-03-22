#pragma once

#include "Types.h"

namespace mycpu {
namespace constants {

// ===== 内存配置 =====
constexpr uint32 MEMORY_BASE = 0x00000000;
constexpr uint32 MEMORY_SIZE = 256 * 1024;     // 256KB
constexpr uint32 MEMORY_END = MEMORY_BASE + MEMORY_SIZE - 1;
constexpr uint32 RISCV_TEST_BASE = 0x80000000;
constexpr uint32 RISCV_TEST_TOHOST_FALLBACK = RISCV_TEST_BASE + 0x1000;

// ===== 内存区域边界 =====
constexpr uint32 TEXT_START = 0x00000000;
constexpr uint32 TEXT_END = 0x000000FF;

constexpr uint32 DATA_START = 0x00000100;
constexpr uint32 DATA_END = 0x00000FFF;

constexpr uint32 STACK_START = 0x00002000;
constexpr uint32 STACK_END = 0x00003FFF;

constexpr uint32 HEAP_START = 0x00004000;
constexpr uint32 HEAP_END = 0x00005FFF;

// ===== MMIO 区域 =====
constexpr uint32 MMIO_BASE = 0x10000000;
constexpr uint32 UART_BASE = 0x10000000;
constexpr uint32 TIMER_BASE = 0x10000010;
constexpr uint32 INTERRUPT_BASE = 0x10000020;

// ===== 寄存器索引 =====
constexpr uint8 REG_ZERO = 0;     // x0 - 硬编码为0
constexpr uint8 REG_RA = 1;       // x1 - 返回地址
constexpr uint8 REG_SP = 2;       // x2 - 栈指针
constexpr uint8 REG_GP = 3;       // x3 - 全局指针
constexpr uint8 REG_TP = 4;       // x4 - 线程指针
constexpr uint8 REG_T0 = 5;       // x5 - 临时寄存器
constexpr uint8 REG_T1 = 6;       // x6
constexpr uint8 REG_T2 = 7;       // x7
constexpr uint8 REG_S0 = 8;       // x8 - 保存寄存器/帧指针
constexpr uint8 REG_S1 = 9;       // x9
constexpr uint8 REG_A0 = 10;      // x10 - 函数参数/返回值
constexpr uint8 REG_A1 = 11;      // x11
constexpr uint8 REG_A2 = 12;      // x12
constexpr uint8 REG_A3 = 13;      // x13
constexpr uint8 REG_A4 = 14;      // x14
constexpr uint8 REG_A5 = 15;      // x15
constexpr uint8 REG_A6 = 16;      // x16
constexpr uint8 REG_A7 = 17;      // x17
constexpr uint8 REG_S2 = 18;      // x18
constexpr uint8 REG_S3 = 19;      // x19
constexpr uint8 REG_S4 = 20;      // x20
constexpr uint8 REG_S5 = 21;      // x21
constexpr uint8 REG_S6 = 22;      // x22
constexpr uint8 REG_S7 = 23;      // x23
constexpr uint8 REG_S8 = 24;      // x24
constexpr uint8 REG_S9 = 25;      // x25
constexpr uint8 REG_S10 = 26;     // x26
constexpr uint8 REG_S11 = 27;     // x27
constexpr uint8 REG_T3 = 28;      // x28 - 临时寄存器
constexpr uint8 REG_T4 = 29;      // x29
constexpr uint8 REG_T5 = 30;      // x30
constexpr uint8 REG_T6 = 31;      // x31

// ===== 特殊地址 =====
constexpr uint32 RESET_VECTOR = 0x00000000;
constexpr uint32 TRAP_VECTOR = 0x00000040;
constexpr uint32 INTERRUPT_VECTOR = 0x00000080;

// ===== 模拟器配置 =====
constexpr uint32 DEFAULT_STACK_POINTER = STACK_END - 4;
constexpr uint32 DEFAULT_HEAP_POINTER = HEAP_START;
constexpr int EXECUTION_DELAY_MS = 300;  // 单步执行延迟

// ===== 功能码定义 =====
namespace Funct3 {
    // 加载/存储
    constexpr uint8 LB = 0x0;
    constexpr uint8 LH = 0x1;
    constexpr uint8 LW = 0x2;
    constexpr uint8 LBU = 0x4;
    constexpr uint8 LHU = 0x5;
    constexpr uint8 SB = 0x0;
    constexpr uint8 SH = 0x1;
    constexpr uint8 SW = 0x2;

    // 分支
    constexpr uint8 BEQ = 0x0;
    constexpr uint8 BNE = 0x1;
    constexpr uint8 BLT = 0x4;
    constexpr uint8 BGE = 0x5;
    constexpr uint8 BLTU = 0x6;
    constexpr uint8 BGEU = 0x7;

    // 立即数运算
    constexpr uint8 ADDI = 0x0;
    constexpr uint8 SLLI = 0x1;
    constexpr uint8 SLTI = 0x2;
    constexpr uint8 SLTIU = 0x3;
    constexpr uint8 XORI = 0x4;
    constexpr uint8 SRLI = 0x5;
    constexpr uint8 SRAI = 0x5;
    constexpr uint8 ORI = 0x6;
    constexpr uint8 ANDI = 0x7;

    // 寄存器运算
    constexpr uint8 ADD_SUB = 0x0;
    constexpr uint8 SLL = 0x1;
    constexpr uint8 SLT = 0x2;
    constexpr uint8 SLTU = 0x3;
    constexpr uint8 XOR = 0x4;
    constexpr uint8 SRL_SRA = 0x5;
    constexpr uint8 OR = 0x6;
    constexpr uint8 AND = 0x7;
}

// ===== 异常码 =====
namespace ExceptionCode {
    constexpr uint8 NONE = 0;
    constexpr uint8 ILLEGAL_INSTRUCTION = 2;
    constexpr uint8 BREAKPOINT = 3;
    constexpr uint8 LOAD_ADDRESS_MISALIGNED = 4;
    constexpr uint8 LOAD_ACCESS_FAULT = 5;
    constexpr uint8 STORE_ADDRESS_MISALIGNED = 6;
    constexpr uint8 STORE_ACCESS_FAULT = 7;
    constexpr uint8 ECALL_FROM_U = 8;
    constexpr uint8 ECALL_FROM_S = 9;
    constexpr uint8 ECALL_FROM_M = 11;
}

// ===== 颜色定义 (用于前端展示) =====
namespace DisplayColors {
    const std::string FETCH_COLOR = "#74c0fc";
    const std::string DECODE_COLOR = "#ffd43b";
    const std::string EXECUTE_COLOR = "#69db7c";
    const std::string MEMORY_COLOR = "#ff8787";
    const std::string WRITEBACK_COLOR = "#da77f2";
    const std::string STALL_COLOR = "#ffd43b";
    const std::string FLUSH_COLOR = "#ff6b6b";
    const std::string HIGHLIGHT_COLOR = "#ffd43b";
    const std::string BACKGROUND_COLOR = "#f4efea";
}

// ===== 指令名称 =====
const std::string INSTR_LUI = "lui";
const std::string INSTR_AUIPC = "auipc";
const std::string INSTR_JAL = "jal";
const std::string INSTR_JALR = "jalr";
const std::string INSTR_BEQ = "beq";
const std::string INSTR_BNE = "bne";
const std::string INSTR_BLT = "blt";
const std::string INSTR_BGE = "bge";
const std::string INSTR_BLTU = "bltu";
const std::string INSTR_BGEU = "bgeu";
const std::string INSTR_LB = "lb";
const std::string INSTR_LH = "lh";
const std::string INSTR_LW = "lw";
const std::string INSTR_LBU = "lbu";
const std::string INSTR_LHU = "lhu";
const std::string INSTR_SB = "sb";
const std::string INSTR_SH = "sh";
const std::string INSTR_SW = "sw";
const std::string INSTR_ADDI = "addi";
const std::string INSTR_SLTI = "slti";
const std::string INSTR_SLTIU = "sltiu";
const std::string INSTR_XORI = "xori";
const std::string INSTR_ORI = "ori";
const std::string INSTR_ANDI = "andi";
const std::string INSTR_SLLI = "slli";
const std::string INSTR_SRLI = "srli";
const std::string INSTR_SRAI = "srai";
const std::string INSTR_ADD = "add";
const std::string INSTR_SUB = "sub";
const std::string INSTR_SLL = "sll";
const std::string INSTR_SLT = "slt";
const std::string INSTR_SLTU = "sltu";
const std::string INSTR_XOR = "xor";
const std::string INSTR_SRL = "srl";
const std::string INSTR_SRA = "sra";
const std::string INSTR_OR = "or";
const std::string INSTR_AND = "and";
const std::string INSTR_ECALL = "ecall";
const std::string INSTR_EBREAK = "ebreak";
const std::string INSTR_HALT = "halt";  // 自定义指令

} // namespace constants
} // namespace mycpu
