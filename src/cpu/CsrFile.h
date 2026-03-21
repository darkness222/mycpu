#pragma once

#include "../include/Types.h"

namespace mycpu {

// ===== RISC-V 特权级别 =====
enum class PrivilegeMode : uint8_t {
    USER = 0,       // U 态
    SUPERVISOR = 1, // S 态
    MACHINE = 3     // M 态（最高）
};

// ===== 常用 CSR 地址 (Machine-Level) =====
namespace CSR {
    // mstatus
    constexpr uint16 MSTATUS   = 0x300;
    // 中断相关
    constexpr uint16 MIE        = 0x304;  // Machine Interrupt Enable
    constexpr uint16 MTVEC      = 0x305;  // Machine Trap Vector
    constexpr uint16 MCAUSE     = 0x342;  // Machine Cause
    constexpr uint16 MEPC       = 0x341;  // Machine Exception PC
    constexpr uint16 MTVAL     = 0x343;  // Machine Trap Value
    constexpr uint16 MIP       = 0x344;  // Machine Interrupt Pending
    // 计数器和计时器
    constexpr uint16 MCYCLE    = 0xB00;
    constexpr uint16 MINSTRET  = 0xB02;
    constexpr uint16 MHARTID    = 0xF14;
    // 物理内存保护 (简化)
    constexpr uint16 PMPCFG0   = 0x3A0;
    constexpr uint16 PMPADDR0   = 0x3B0;
}

// ===== mstatus 位域 =====
namespace MstatusBits {
    constexpr uint32 MIE   = 1u << 3;   // Machine Interrupt Enable
    constexpr uint32 MPIE  = 1u << 7;  // Previous MIE
    constexpr uint32 MPP   = 3u << 11; // Previous Privilege Mode
    constexpr uint32 SIE   = 1u << 1;  // Supervisor IE
    constexpr uint32 UIE   = 1u << 0;  // User IE
}

// ===== 中断/异常原因 (mcause) =====
namespace Cause {
    // 异常 (最高位=0)
    constexpr uint32 INSTRUCTION_MISALIGNED = 0;
    constexpr uint32 INSTRUCTION_ACCESS_FAULT = 1;
    constexpr uint32 ILLEGAL_INSTRUCTION = 2;
    constexpr uint32 BREAKPOINT = 3;
    constexpr uint32 LOAD_ADDRESS_MISALIGNED = 4;
    constexpr uint32 LOAD_ACCESS_FAULT = 5;
    constexpr uint32 STORE_AMO_ADDRESS_MISALIGNED = 6;
    constexpr uint32 STORE_AMO_ACCESS_FAULT = 7;
    constexpr uint32 ECALL_FROM_U = 8;
    constexpr uint32 ECALL_FROM_S = 9;
    constexpr uint32 ECALL_FROM_M = 11;
    constexpr uint32 INSTRUCTION_PAGE_FAULT = 12;
    constexpr uint32 LOAD_PAGE_FAULT = 13;
    constexpr uint32 STORE_PAGE_FAULT = 15;

    // 中断 (最高位=1，即负数)
    constexpr uint32 INTERRUPT_MASK = 0x80000000u;
    constexpr uint32 MACHINE_SOFTWARE_INTERRUPT = 3 + INTERRUPT_MASK;
    constexpr uint32 MACHINE_TIMER_INTERRUPT    = 7 + INTERRUPT_MASK;
    constexpr uint32 MACHINE_EXTERNAL_INTERRUPT  = 11 + INTERRUPT_MASK;
}

// ===== CSR 寄存器文件 =====
class CsrFile {
public:
    CsrFile();

    // 读取 CSR
    uint32 read(uint16 addr) const;
    // 写入 CSR (返回写入前的值)
    uint32 write(uint16 addr, uint32 value);
    // 条件写入（只在 rs1 != 0 时写入）
    uint32 csrrs(uint16 addr, uint8 rs1);
    // 条件写入（只清除 rs1 中为 1 的位）
    uint32 csrrc(uint16 addr, uint8 rs1);

    // 特权级别操作
    void setPrivilegeMode(PrivilegeMode mode);
    PrivilegeMode getPrivilegeMode() const { return privilege_mode_; }

    // Trap 相关
    void enterTrap(uint32 cause, uint32 epc, uint32 tval);
    void exitTrap();  // mret

    // 全局中断开关
    bool isInterruptEnabled() const;
    void enableInterrupts();
    void disableInterrupts();

    // 计时器
    void incrementCycle() { csrs_[CSR::MCYCLE]++; }
    void incrementInstret() { csrs_[CSR::MINSTRET]++; }

    uint32 getCycle() const { return csrs_[CSR::MCYCLE]; }
    uint32 getInstret() const { return csrs_[CSR::MINSTRET]; }

    // 清除待处理中断
    void clearPendingInterrupt(uint32 cause);

    uint32 getMstatus() const { return csrs_[CSR::MSTATUS]; }
    uint32 getMcause() const { return csrs_[CSR::MCAUSE]; }
    uint32 getMepc() const { return csrs_[CSR::MEPC]; }
    uint32 getMtvec() const { return csrs_[CSR::MTVEC]; }
    uint32 getMtval() const { return csrs_[CSR::MTVAL]; }
    uint32 getMie() const { return csrs_[CSR::MIE]; }
    uint32 getMip() const { return csrs_[CSR::MIP]; }

    bool hasPendingInterrupt() const;
    uint32 getHighestPriorityPendingInterrupt() const;

private:
    std::array<uint32, 0x1000> csrs_;  // 4KB CSR 地址空间
    PrivilegeMode privilege_mode_;

    void checkWritePermission(uint16 addr);
    void checkReadPermission(uint16 addr) const;
};

} // namespace mycpu
