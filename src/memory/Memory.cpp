#include "Memory.h"
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace mycpu {

Memory::Memory() {
    memory_.fill(0);
}

void Memory::reset() {
    memory_.fill(0);
}

uint8 Memory::readByte(Address addr) const {
    size_t index = 0;
    if (!translateAddress(addr, index)) {
        return 0;
    }
    return memory_[index];
}

uint16 Memory::readHalfWord(Address addr) const {
    size_t index = 0;
    if (!translateAddress(addr, index) || index + 1 >= MEMORY_SIZE) {
        return 0;
    }
    uint16 value = memory_[index] | (memory_[index + 1] << 8);
    return value;
}

uint32 Memory::readWord(Address addr) const {
    size_t index = 0;
    if (!translateAddress(addr, index) || index + 3 >= MEMORY_SIZE) {
        return 0;
    }
    uint32 value = memory_[index] | (memory_[index + 1] << 8) |
                   (memory_[index + 2] << 16) | (memory_[index + 3] << 24);
    return value;
}

void Memory::writeByte(Address addr, uint8 value) {
    size_t index = 0;
    if (!translateAddress(addr, index)) {
        return;
    }
    memory_[index] = value;
}

void Memory::writeHalfWord(Address addr, uint16 value) {
    size_t index = 0;
    if (!translateAddress(addr, index) || index + 1 >= MEMORY_SIZE) {
        return;
    }
    memory_[index] = static_cast<uint8>(value & 0xFF);
    memory_[index + 1] = static_cast<uint8>((value >> 8) & 0xFF);
}

void Memory::writeWord(Address addr, uint32 value) {
    size_t index = 0;
    if (!translateAddress(addr, index) || index + 3 >= MEMORY_SIZE) {
        return;
    }
    memory_[index] = static_cast<uint8>(value & 0xFF);
    memory_[index + 1] = static_cast<uint8>((value >> 8) & 0xFF);
    memory_[index + 2] = static_cast<uint8>((value >> 16) & 0xFF);
    memory_[index + 3] = static_cast<uint8>((value >> 24) & 0xFF);
}

void Memory::loadBinary(const std::vector<uint8>& data, Address start_addr) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (start_addr + i < MEMORY_SIZE) {
            memory_[start_addr + i] = data[i];
        }
    }
}

std::unordered_map<Address, uint32> Memory::getMemorySnapshot() const {
    std::unordered_map<Address, uint32> snapshot;
    for (size_t i = 0; i < MEMORY_SIZE; i += 4) {
        if (i + 3 < MEMORY_SIZE) {
            uint32 value = readWord(static_cast<Address>(i));
            if (value != 0) {
                snapshot[static_cast<Address>(i)] = value;
            }
        }
    }
    return snapshot;
}

MemorySegment Memory::getSegment(Address addr) const {
    if (addr >= constants::TEXT_START && addr <= constants::TEXT_END) {
        return MemorySegment::TEXT;
    } else if (addr >= constants::DATA_START && addr <= constants::DATA_END) {
        return MemorySegment::DATA;
    } else if (addr >= constants::STACK_START && addr <= constants::STACK_END) {
        return MemorySegment::STACK;
    } else if (addr >= constants::HEAP_START && addr <= constants::HEAP_END) {
        return MemorySegment::HEAP;
    } else if (addr >= constants::MMIO_BASE) {
        return MemorySegment::MMIO;
    }
    return MemorySegment::OTHER;
}

bool Memory::isAddressValid(Address addr) const {
    size_t index = 0;
    return translateAddress(addr, index);
}

bool Memory::translateAddress(Address addr, size_t& index) const {
    if (addr < MEMORY_SIZE) {
        index = static_cast<size_t>(addr);
        return true;
    }

    const uint64 base = constants::RISCV_TEST_BASE;
    const uint64 current = addr;
    if (current >= base && current < base + MEMORY_SIZE) {
        index = static_cast<size_t>(current - base);
        return true;
    }

    return false;
}

std::string Memory::segmentToString(MemorySegment seg) {
    switch (seg) {
        case MemorySegment::TEXT: return "TEXT";
        case MemorySegment::DATA: return "DATA";
        case MemorySegment::STACK: return "STACK";
        case MemorySegment::HEAP: return "HEAP";
        case MemorySegment::MMIO: return "MMIO";
        default: return "OTHER";
    }
}

} // namespace mycpu
