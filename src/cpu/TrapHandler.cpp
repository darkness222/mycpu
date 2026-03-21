#include "TrapHandler.h"
#include "../memory/Memory.h"
#include "../bus/Bus.h"
#include <sstream>

namespace mycpu {

TrapHandler::TrapHandler()
    : trap_vector_base_(0), trap_vectored_(true) {
}

void TrapHandler::logTrap(const std::string& info) {
    last_trap_info_ = info;
}

// ===== 异常处理 =====

TrapInfo TrapHandler::handleInstructionAddressMisaligned(uint32 pc, uint32 target) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::INSTRUCTION_MISALIGNED;
    trap.epc = pc;
    trap.tval = target;
    std::ostringstream oss;
    oss << "Exception: Instruction address misaligned. PC=0x" << std::hex << pc
        << " target=0x" << target;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleInstructionAccessFault(uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::INSTRUCTION_ACCESS_FAULT;
    trap.epc = pc;
    trap.tval = pc;
    std::ostringstream oss;
    oss << "Exception: Instruction access fault at PC=0x" << std::hex << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleIllegalInstruction(uint32 instr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::ILLEGAL_INSTRUCTION;
    trap.epc = pc;
    trap.tval = instr;
    std::ostringstream oss;
    oss << "Exception: Illegal instruction 0x" << std::hex << instr
        << " at PC=0x" << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleBreakpoint(uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::BREAKPOINT;
    trap.epc = pc;
    trap.tval = pc;
    std::ostringstream oss;
    oss << "Exception: Breakpoint at PC=0x" << std::hex << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleLoadAddressMisaligned(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::LOAD_ADDRESS_MISALIGNED;
    trap.epc = pc;
    trap.tval = addr;
    std::ostringstream oss;
    oss << "Exception: Load address misaligned 0x" << std::hex << addr
        << " at PC=0x" << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleLoadAccessFault(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::LOAD_ACCESS_FAULT;
    trap.epc = pc;
    trap.tval = addr;
    std::ostringstream oss;
    oss << "Exception: Load access fault at 0x" << std::hex << addr
        << " PC=0x" << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleStoreAddressMisaligned(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::STORE_AMO_ADDRESS_MISALIGNED;
    trap.epc = pc;
    trap.tval = addr;
    std::ostringstream oss;
    oss << "Exception: Store address misaligned 0x" << std::hex << addr
        << " at PC=0x" << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleStoreAccessFault(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::STORE_AMO_ACCESS_FAULT;
    trap.epc = pc;
    trap.tval = addr;
    std::ostringstream oss;
    oss << "Exception: Store access fault at 0x" << std::hex << addr
        << " PC=0x" << pc;
    logTrap(oss.str());
    return trap;
}

TrapInfo TrapHandler::handleEcall(uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.epc = pc;
    trap.tval = 0;

    // 根据当前特权级决定 ecall 原因
    switch (csr_.getPrivilegeMode()) {
        case PrivilegeMode::USER:
            trap.cause = Cause::ECALL_FROM_U;
            break;
        case PrivilegeMode::SUPERVISOR:
            trap.cause = Cause::ECALL_FROM_S;
            break;
        case PrivilegeMode::MACHINE:
        default:
            trap.cause = Cause::ECALL_FROM_M;
            break;
    }

    std::ostringstream oss;
    oss << "Exception: ECALL from " << static_cast<int>(csr_.getPrivilegeMode())
        << " at PC=0x" << std::hex << pc;
    logTrap(oss.str());
    return trap;
}

// ===== 页面错误 (预留) =====

TrapInfo TrapHandler::handleInstructionPageFault(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::INSTRUCTION_PAGE_FAULT;
    trap.epc = pc;
    trap.tval = addr;
    logTrap("Exception: Instruction page fault");
    return trap;
}

TrapInfo TrapHandler::handleLoadPageFault(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::LOAD_PAGE_FAULT;
    trap.epc = pc;
    trap.tval = addr;
    logTrap("Exception: Load page fault");
    return trap;
}

TrapInfo TrapHandler::handleStorePageFault(uint32 addr, uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = false;
    trap.cause = Cause::STORE_PAGE_FAULT;
    trap.epc = pc;
    trap.tval = addr;
    logTrap("Exception: Store page fault");
    return trap;
}

// ===== 中断处理 =====

bool TrapHandler::hasPendingInterrupt() const {
    return csr_.hasPendingInterrupt();
}

TrapInfo TrapHandler::getPendingInterrupt(uint32 pc) {
    TrapInfo trap;
    trap.is_interrupt = true;
    trap.epc = pc;
    trap.cause = csr_.getHighestPriorityPendingInterrupt();
    trap.tval = 0;

    if (trap.cause == Cause::MACHINE_SOFTWARE_INTERRUPT) {
        logTrap("Interrupt: Machine software interrupt");
    } else if (trap.cause == Cause::MACHINE_TIMER_INTERRUPT) {
        logTrap("Interrupt: Machine timer interrupt");
    } else if (trap.cause == Cause::MACHINE_EXTERNAL_INTERRUPT) {
        logTrap("Interrupt: Machine external interrupt");
    }

    return trap;
}

void TrapHandler::raiseInterrupt(uint32 cause) {
    uint32 bit = 0;
    if (cause == Cause::MACHINE_SOFTWARE_INTERRUPT) bit = 1 << 3;
    else if (cause == Cause::MACHINE_TIMER_INTERRUPT) bit = 1 << 7;
    else if (cause == Cause::MACHINE_EXTERNAL_INTERRUPT) bit = 1 << 11;

    uint32 mip = csr_.read(CSR::MIP);
    csr_.write(CSR::MIP, mip | bit);
}

bool TrapHandler::isInterruptPending(uint32 cause) const {
    uint32 bit = 0;
    if (cause == Cause::MACHINE_SOFTWARE_INTERRUPT) bit = 1 << 3;
    else if (cause == Cause::MACHINE_TIMER_INTERRUPT) bit = 1 << 7;
    else if (cause == Cause::MACHINE_EXTERNAL_INTERRUPT) bit = 1 << 11;

    uint32 mip = csr_.read(CSR::MIP);
    return (mip & bit) != 0;
}

void TrapHandler::clearInterrupt(uint32 cause) {
    uint32 bit = 0;
    if (cause == Cause::MACHINE_SOFTWARE_INTERRUPT) bit = 1 << 3;
    else if (cause == Cause::MACHINE_TIMER_INTERRUPT) bit = 1 << 7;
    else if (cause == Cause::MACHINE_EXTERNAL_INTERRUPT) bit = 1 << 11;

    uint32 mip = csr_.read(CSR::MIP);
    csr_.write(CSR::MIP, mip & ~bit);
}

// ===== Trap 入口/返回 =====

void TrapHandler::enterTrap(const TrapInfo& trap) {
    current_trap_ = trap;
    csr_.enterTrap(trap.cause, trap.epc, trap.tval);
}

uint32 TrapHandler::exitTrap() {
    uint32 mepc = csr_.getMepc();
    csr_.exitTrap();
    return mepc;
}

uint32 TrapHandler::calculateTrapVector(uint32 cause) const {
    uint32 base = csr_.getMtvec();
    bool vectored = trap_vectored_ && (base & 0x1) == 0;

    if (vectored && !((cause & Cause::INTERRUPT_MASK) == 0)) {
        // 向量模式：base + 4 * cause
        return (base & ~0xFF) + ((cause & ~Cause::INTERRUPT_MASK) * 4);
    } else {
        // 非向量模式：直接跳到 base
        return base & ~0x3F;
    }
}

} // namespace mycpu
