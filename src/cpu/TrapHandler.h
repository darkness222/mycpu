#pragma once

#include "../include/Types.h"
#include "CsrFile.h"
#include <memory>

namespace mycpu {

class Memory;
class Bus;

// ===== Trap 信息 =====
struct TrapInfo {
    bool is_interrupt;     // true=中断, false=异常
    uint32 cause;          // mcause 值
    uint32 epc;           // 异常/中断发生的 PC
    uint32 tval;          // 附加信息 (如错误地址)
    bool handled;

    TrapInfo() : is_interrupt(false), cause(0), epc(0), tval(0), handled(false) {}
};

// ===== Trap 处理模块 =====
class TrapHandler {
public:
    TrapHandler();
    void setMemory(std::shared_ptr<Memory> memory) { memory_ = memory; }
    void setBus(std::shared_ptr<Bus> bus) { bus_ = bus; }
    void setCsr(CsrFile* csr) { csr_ = csr; }

    // ===== 异常处理 =====
    // 取指地址未对齐 (JAL/分支目标非4字节对齐)
    TrapInfo handleInstructionAddressMisaligned(uint32 pc, uint32 target);
    // 取指访问错误 (内存不可读)
    TrapInfo handleInstructionAccessFault(uint32 pc);
    // 非法指令
    TrapInfo handleIllegalInstruction(uint32 instr, uint32 pc);
    // 断点 (ebreak)
    TrapInfo handleBreakpoint(uint32 pc);
    // 加载地址未对齐
    TrapInfo handleLoadAddressMisaligned(uint32 addr, uint32 pc);
    // 加载访问错误 (地址越界/不可读)
    TrapInfo handleLoadAccessFault(uint32 addr, uint32 pc);
    // 存储地址未对齐
    TrapInfo handleStoreAddressMisaligned(uint32 addr, uint32 pc);
    // 存储访问错误
    TrapInfo handleStoreAccessFault(uint32 addr, uint32 pc);
    // 系统调用 (ecall)
    TrapInfo handleEcall(uint32 pc);
    // 页面错误 (简化版暂无分页，这里先预留)
    TrapInfo handleInstructionPageFault(uint32 addr, uint32 pc);
    TrapInfo handleLoadPageFault(uint32 addr, uint32 pc);
    TrapInfo handleStorePageFault(uint32 addr, uint32 pc);

    // ===== 中断处理 =====
    // 检查是否有待处理的中断（需要 CSR 支持）
    bool hasPendingInterrupt() const;
    // 获取最高优先级的中断
    TrapInfo getPendingInterrupt(uint32 pc);
    // 清除中断
    void clearInterrupt(uint32 cause);

    // ===== CSR 访问 =====
    void raiseInterrupt(uint32 cause);
    bool isInterruptPending(uint32 cause) const;

    // ===== Trap 入口/返回 =====
    // 执行 trap：将 EPC/cause/mtval 存入 CSR，切换到 trap 向量
    void enterTrap(const TrapInfo& trap);
    // 执行 mret：从 CSR 恢复 EPC 和特权级
    uint32 exitTrap();  // 返回 mret 后的 PC

    // ===== 内存访问 (trap 处理用) =====
    void setTrapVector(uint32 base, bool vectored = true) {
        trap_vector_base_ = base;
        trap_vectored_ = vectored;
    }
    uint32 calculateTrapVector(uint32 cause) const;

    // ===== 调试 =====
    std::string getLastTrapInfo() const { return last_trap_info_; }
    const TrapInfo& getCurrentTrap() const { return current_trap_; }

private:
    std::shared_ptr<Memory> memory_;
    std::shared_ptr<Bus> bus_;
    CsrFile* csr_ = nullptr;

    TrapInfo current_trap_;
    std::string last_trap_info_;

    uint32 trap_vector_base_;
    bool trap_vectored_;

    void logTrap(const std::string& info);
};

} // namespace mycpu
