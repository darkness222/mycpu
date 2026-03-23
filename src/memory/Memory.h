#pragma once

#include "../include/Types.h"
#include "../include/Constants.h"
#include <array>
#include <unordered_map>
#include <vector>

namespace mycpu {

enum class MemoryAccessType : uint8_t {
    FETCH = 0,
    LOAD = 1,
    STORE = 2
};

struct PageTableEntry {
    uint32 physical_page;
    bool valid;
    bool readable;
    bool writable;
    bool executable;

    PageTableEntry()
        : physical_page(0), valid(false), readable(false), writable(false), executable(false) {}
};

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
    bool isPhysicalAddressValid(Address addr) const;

    void enablePaging(bool enable);
    bool isPagingEnabled() const { return paging_enabled_; }
    void clearPageTable();
    void mapPage(uint32 virtual_page, uint32 physical_page,
                 bool readable = true, bool writable = true, bool executable = true);
    void identityMapRange(Address start, Address end,
                          bool readable = true, bool writable = true, bool executable = true);
    bool translateForAccess(Address addr, MemoryAccessType access, Address& translated_addr, bool& page_fault) const;
    std::vector<std::pair<uint32, PageTableEntry>> getPageTableSnapshot() const;
    size_t getMappedPageCount() const { return page_table_.size(); }

    static std::string segmentToString(MemorySegment seg);

private:
    std::array<uint8, MEMORY_SIZE> memory_;
    std::unordered_map<uint32, PageTableEntry> page_table_;
    bool paging_enabled_;
    void checkBounds(Address addr) const;
    bool translateAddress(Address addr, size_t& index) const;
};

} // namespace mycpu
