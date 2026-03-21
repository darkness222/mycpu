#pragma once

#include "../include/Types.h"
#include "../include/Constants.h"
#include <array>
#include <string>

namespace mycpu {

class RegisterFile {
public:
    RegisterFile();

    void reset();
    int32 read(uint8 reg_num) const;
    void write(uint8 reg_num, int32 value);

    std::array<int32, NUM_REGISTERS> getAllRegisters() const;
    std::string getRegisterName(uint8 reg_num) const;

    uint8 getLastWriteReg() const { return last_write_reg_; }
    int32 getLastWriteValue() const { return last_write_value_; }
    bool wasWritten() const { return last_write_reg_ != 0 || last_write_value_ != 0; }

    void clearLastWrite() { last_write_reg_ = 0; last_write_value_ = 0; }

private:
    std::array<int32, NUM_REGISTERS> registers_;
    uint8 last_write_reg_;
    int32 last_write_value_;
};

} // namespace mycpu
