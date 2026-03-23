#include "Memory.h"
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace mycpu {

Memory::Memory() {
    memory_.fill(0);
    paging_enabled_ = false;
}

void Memory::reset() {
    memory_.fill(0);
    clearPageTable();
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
    if (paging_enabled_) {
        Address translated = 0;
        bool page_fault = false;
        return translateForAccess(addr, MemoryAccessType::LOAD, translated, page_fault);
    }
    return isPhysicalAddressValid(addr);
}

bool Memory::isPhysicalAddressValid(Address addr) const {
    size_t index = 0;
    return translateAddress(addr, index);
}

void Memory::enablePaging(bool enable) {
    paging_enabled_ = enable;
}

void Memory::clearPageTable() {
    page_table_.clear();
    paging_enabled_ = false;
}

void Memory::mapPage(uint32 virtual_page, uint32 physical_page,
                     bool readable, bool writable, bool executable) {
    PageTableEntry entry;
    entry.physical_page = physical_page;
    entry.valid = true;
    entry.readable = readable;
    entry.writable = writable;
    entry.executable = executable;
    page_table_[virtual_page] = entry;
}

void Memory::identityMapRange(Address start, Address end,
                              bool readable, bool writable, bool executable) {
    const uint32 page_size = 4096;
    const uint32 first_page = start / page_size;
    const uint32 last_page = end / page_size;
    for (uint32 page = first_page; page <= last_page; ++page) {
        mapPage(page, page, readable, writable, executable);
    }
}

bool Memory::translateForAccess(Address addr, MemoryAccessType access, Address& translated_addr, bool& page_fault) const {
    page_fault = false;

    if (!paging_enabled_) {
        translated_addr = addr;
        if (addr >= constants::MMIO_BASE) {
            return true;
        }
        return isPhysicalAddressValid(addr);
    }

    const uint32 page_size = 4096;
    const uint32 virtual_page = addr / page_size;
    const uint32 offset = addr % page_size;
    auto it = page_table_.find(virtual_page);
    if (it == page_table_.end() || !it->second.valid) {
        page_fault = true;
        return false;
    }

    const PageTableEntry& entry = it->second;
    bool permitted = false;
    switch (access) {
        case MemoryAccessType::FETCH:
            permitted = entry.executable;
            break;
        case MemoryAccessType::LOAD:
            permitted = entry.readable;
            break;
        case MemoryAccessType::STORE:
            permitted = entry.writable;
            break;
    }

    if (!permitted) {
        page_fault = true;
        return false;
    }

    translated_addr = entry.physical_page * page_size + offset;
    if (translated_addr >= constants::MMIO_BASE) {
        return true;
    }
    return isPhysicalAddressValid(translated_addr);
}

std::vector<std::pair<uint32, PageTableEntry>> Memory::getPageTableSnapshot() const {
    std::vector<std::pair<uint32, PageTableEntry>> mappings(page_table_.begin(), page_table_.end());
    std::sort(mappings.begin(), mappings.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return mappings;
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
