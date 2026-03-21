#include "RegisterFile.h"
#include "../include/Constants.h"
#include <stdexcept>
#include <sstream>

namespace mycpu {

RegisterFile::RegisterFile() : last_write_reg_(0), last_write_value_(0) {
    registers_.fill(0);
}

void RegisterFile::reset() {
    registers_.fill(0);
    registers_[constants::REG_SP] = constants::DEFAULT_STACK_POINTER;
    clearLastWrite();
}

int32 RegisterFile::read(uint8 reg_num) const {
    if (reg_num >= NUM_REGISTERS) {
        throw std::out_of_range("Register index out of range");
    }
    if (reg_num == 0) {
        return 0;
    }
    return registers_[reg_num];
}

void RegisterFile::write(uint8 reg_num, int32 value) {
    if (reg_num >= NUM_REGISTERS) {
        return;
    }
    if (reg_num == 0) {
        return;
    }
    registers_[reg_num] = value;
    last_write_reg_ = reg_num;
    last_write_value_ = value;
}

std::array<int32, NUM_REGISTERS> RegisterFile::getAllRegisters() const {
    std::array<int32, NUM_REGISTERS> result;
    for (size_t i = 0; i < NUM_REGISTERS; ++i) {
        result[i] = (i == 0) ? 0 : registers_[i];
    }
    return result;
}

std::string RegisterFile::getRegisterName(uint8 reg_num) const {
    static const std::array<std::string, 32> names = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31"
    };
    if (reg_num < names.size()) {
        return names[reg_num];
    }
    return "unknown";
}

} // namespace mycpu
