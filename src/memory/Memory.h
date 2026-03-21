#pragma once

#include "../include/Types.h"
#include "../include/Constants.h"
#include <array>
#include <unordered_map>

namespace mycpu {

class Memory {
public:
    Memory();
    ~Memory() = default;

    void reset();

    uint8 readByte(Address addr) const;
    uint16 readHalfWord(Address addr) const;
    uint32 readWord(Address addr) const;

    void writeByte(Address addr, uint8 value);
    void writeHalfWord(Address addr, uint16 value);
    void writeWord(Address addr, uint32 value);

    void loadBinary(const std::vector<uint8>& data, Address start_addr);

    std::unordered_map<Address, uint32> getMemorySnapshot() const;

    MemorySegment getSegment(Address addr) const;
    bool isAddressValid(Address addr) const;

    static std::string segmentToString(MemorySegment seg);

private:
    std::array<uint8, MEMORY_SIZE> memory_;
    void checkBounds(Address addr) const;
};

} // namespace mycpu
