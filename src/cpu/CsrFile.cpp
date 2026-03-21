#include "CsrFile.h"
#include "../include/Constants.h"
#include <cstring>

namespace mycpu {

CsrFile::CsrFile() : privilege_mode_(PrivilegeMode::MACHINE) {
    std::memset(csrs_.data(), 0, sizeof(csrs_));

    // 初始化默认值
    csrs_[CSR::MHARTID] = 0;  // 单 hart，ID=0

    // mtvec 默认: 向量模式，基址 0
    csrs_[CSR::MTVEC] = 0;

    // mstatus 默认值:
    // 复位后MPP=3 (M态), MIE=0
    csrs_[CSR::MSTATUS] = (static_cast<uint32>(PrivilegeMode::MACHINE) << 11);
}

uint32 CsrFile::read(uint16 addr) const {
    checkReadPermission(addr);
    return csrs_[addr];
}

uint32 CsrFile::write(uint16 addr, uint32 value) {
    checkWritePermission(addr);
    uint32 old = csrs_[addr];
    csrs_[addr] = value;
    return old;
}

uint32 CsrFile::csrrs(uint16 addr, uint8 rs1) {
    if (rs1 == 0) return csrs_[addr];
    return write(addr, csrs_[addr] | rs1);
}

uint32 CsrFile::csrrc(uint16 addr, uint8 rs1) {
    if (rs1 == 0) return csrs_[addr];
    return write(addr, csrs_[addr] & ~rs1);
}

void CsrFile::setPrivilegeMode(PrivilegeMode mode) {
    privilege_mode_ = mode;
}

void CsrFile::enterTrap(uint32 cause, uint32 epc, uint32 tval) {
    uint32 mstatus = csrs_[CSR::MSTATUS];

    // 保存之前的 MIE (中断使能) 到 MPIE
    bool mie = (mstatus & MstatusBits::MIE) != 0;
    if (mie) {
        mstatus |= MstatusBits::MPIE;  // MPIE = MIE
    } else {
        mstatus &= ~MstatusBits::MPIE;
    }

    // 保存当前特权级到 MPP
    mstatus = (mstatus & ~MstatusBits::MPP) |
              (static_cast<uint32>(privilege_mode_) << 11);

    // 关闭全局中断
    mstatus &= ~MstatusBits::MIE;

    csrs_[CSR::MSTATUS] = mstatus;
    csrs_[CSR::MCAUSE] = cause;
    csrs_[CSR::MEPC] = epc;
    csrs_[CSR::MTVAL] = tval;

    // 切换到 M 态
    privilege_mode_ = PrivilegeMode::MACHINE;
}

void CsrFile::exitTrap() {
    uint32 mstatus = csrs_[CSR::MSTATUS];

    // 恢复 MPIE -> MIE
    bool mpie = (mstatus & MstatusBits::MPIE) != 0;
    if (mpie) {
        mstatus |= MstatusBits::MIE;
    } else {
        mstatus &= ~MstatusBits::MIE;
    }

    // 恢复 MPP -> privilege mode
    uint32 mpp = (mstatus & MstatusBits::MPP) >> 11;
    privilege_mode_ = static_cast<PrivilegeMode>(mpp);

    // 清除 MPIE 和 MPP
    mstatus &= ~(MstatusBits::MPIE | MstatusBits::MPP);
    csrs_[CSR::MSTATUS] = mstatus;
}

bool CsrFile::isInterruptEnabled() const {
    uint32 mstatus = csrs_[CSR::MSTATUS];
    return (mstatus & MstatusBits::MIE) != 0;
}

void CsrFile::enableInterrupts() {
    csrs_[CSR::MSTATUS] |= MstatusBits::MIE;
}

void CsrFile::disableInterrupts() {
    csrs_[CSR::MSTATUS] &= ~MstatusBits::MIE;
}

bool CsrFile::hasPendingInterrupt() const {
    uint32 mie = csrs_[CSR::MIE];
    uint32 mip = csrs_[CSR::MIP];
    uint32 pending = mie & mip;

    // 检查各个中断是否使能且挂起
    return pending != 0;
}

uint32 CsrFile::getHighestPriorityPendingInterrupt() const {
    uint32 mie = csrs_[CSR::MIE];
    uint32 mip = csrs_[CSR::MIP];
    uint32 pending = mie & mip;

    // 按优先级检查 (数字越小优先级越高)
    if ((pending & (1 << 3)) && (mip & (1 << 3)))   // MSIE
        return Cause::MACHINE_SOFTWARE_INTERRUPT;
    if ((pending & (1 << 7)) && (mip & (1 << 7)))   // MTIE
        return Cause::MACHINE_TIMER_INTERRUPT;
    if ((pending & (1 << 11)) && (mip & (1 << 11))) // MEIE
        return Cause::MACHINE_EXTERNAL_INTERRUPT;

    return 0;
}

void CsrFile::checkWritePermission(uint16 addr) {
    // 只允许 M 态写 CSR
    if (privilege_mode_ != PrivilegeMode::MACHINE) {
        // 简化处理：在非 M 态写入 CSR 触发异常
        // 这里暂时不处理，由调用者处理
    }

    // 只读 CSR 检查
    // MHARTID 是只读的
    if (addr == CSR::MHARTID) {
        // 忽略写操作
        return;
    }
}

void CsrFile::checkReadPermission(uint16 addr) const {
    // 简化处理：所有特权级都可以读当前级 CSR
    // 实际 RISC-V 有更复杂的权限检查
    (void)addr;
}

} // namespace mycpu
