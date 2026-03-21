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
    if (addr >= MEMORY_SIZE) {
        return 0;
    }
    return memory_[addr];
}

uint16 Memory::readHalfWord(Address addr) const {
    if (addr + 1 >= MEMORY_SIZE) {
        return 0;
    }
    uint16 value = memory_[addr] | (memory_[addr + 1] << 8);
    return value;
}

uint32 Memory::readWord(Address addr) const {
    if (addr + 3 >= MEMORY_SIZE) {
        return 0;
    }
    uint32 value = memory_[addr] | (memory_[addr + 1] << 8) |
                   (memory_[addr + 2] << 16) | (memory_[addr + 3] << 24);
    return value;
}

void Memory::writeByte(Address addr, uint8 value) {
    if (addr >= MEMORY_SIZE) {
        return;
    }
    memory_[addr] = value;
}

void Memory::writeHalfWord(Address addr, uint16 value) {
    if (addr + 1 >= MEMORY_SIZE) {
        return;
    }
    memory_[addr] = static_cast<uint8>(value & 0xFF);
    memory_[addr + 1] = static_cast<uint8>((value >> 8) & 0xFF);
}

void Memory::writeWord(Address addr, uint32 value) {
    if (addr + 3 >= MEMORY_SIZE) {
        return;
    }
    memory_[addr] = static_cast<uint8>(value & 0xFF);
    memory_[addr + 1] = static_cast<uint8>((value >> 8) & 0xFF);
    memory_[addr + 2] = static_cast<uint8>((value >> 16) & 0xFF);
    memory_[addr + 3] = static_cast<uint8>((value >> 24) & 0xFF);
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
    return addr < MEMORY_SIZE;
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
