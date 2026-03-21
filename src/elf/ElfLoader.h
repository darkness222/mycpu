#pragma once

#include "../include/Types.h"
#include <string>
#include <vector>
#include <memory>

namespace mycpu {

class Memory;
class Bus;

// ===== ELF 文件格式常量 (32-bit) =====
namespace ElfConst {
    // ELF Magic Number
    constexpr uint8 ELF_MAGIC[4] = {0x7F, 'E', 'L', 'F'};

    // ELF Class
    constexpr uint8 ELFCLASS32 = 1;
    constexpr uint8 ELFCLASS64 = 2;

    // ELF Endianness
    constexpr uint8 ELFDATA2LSB = 1;  // Little endian
    constexpr uint8 ELFDATA2MSB = 2;  // Big endian

    // ELF Type
    constexpr uint16 ET_NONE = 0;
    constexpr uint16 ET_REL = 1;    // Relocatable file
    constexpr uint16 ET_EXEC = 2;  // Executable file

    // RISC-V Architecture
    constexpr uint16 EM_RISCV = 0xF3;

    // Program Header Types
    constexpr uint32 PT_NULL = 0;
    constexpr uint32 PT_LOAD = 1;
    constexpr uint32 PT_DYNAMIC = 2;
    constexpr uint32 PT_INTERP = 3;
    constexpr uint32 PT_NOTE = 4;
    constexpr uint32 PT_PHDR = 6;
    constexpr uint32 PT_TLS = 7;

    // Section Header Types
    constexpr uint32 SHT_NULL = 0;
    constexpr uint32 SHT_PROGBITS = 1;
    constexpr uint32 SHT_SYMTAB = 2;
    constexpr uint32 SHT_STRTAB = 3;
    constexpr uint32 SHT_RELA = 4;
    constexpr uint32 SHT_NOBITS = 8;
}

// ===== ELF Header (32-bit) =====
#pragma pack(push, 1)
struct Elf32_Ehdr {
    uint8  e_ident[16];   // Magic + class + endian + version + osabi
    uint16 e_type;         // Object file type
    uint16 e_machine;      // Architecture
    uint32 e_version;      // Object file version
    uint32 e_entry;       // Entry point virtual address
    uint32 e_phoff;       // Program header table file offset
    uint32 e_shoff;       // Section header table file offset
    uint32 e_flags;       // Processor-specific flags
    uint16 e_ehsize;      // ELF header size
    uint16 e_phentsize;   // Program header table entry size
    uint16 e_phnum;       // Program header table entry count
    uint16 e_shentsize;   // Section header table entry size
    uint16 e_shnum;       // Section header table entry count
    uint16 e_shstrndx;    // Section header string table index
};

// Program Header
struct Elf32_Phdr {
    uint32 p_type;    // Segment type
    uint32 p_offset;  // Segment file offset
    uint32 p_vaddr;   // Segment virtual address
    uint32 p_paddr;   // Segment physical address
    uint32 p_filesz;  // Segment size in file
    uint32 p_memsz;   // Segment size in memory
    uint32 p_flags;   // Segment flags
    uint32 p_align;   // Segment alignment
};
#pragma pack(pop)

// ===== ELF 加载信息 =====
struct ElfLoadResult {
    bool success;
    std::string error_message;
    uint32 entry_point;
    uint32 load_size;
    uint32 segment_count;
    std::vector<uint32> loaded_segments;  // 各段的起始地址
};

// ===== ELF 加载器 =====
class ElfLoader {
public:
    ElfLoader();

    // 从字节向量加载 (前端 RPC 场景)
    bool loadFromBytes(const std::vector<uint8>& data);

    // 加载到内存
    ElfLoadResult loadToMemory(std::shared_ptr<Memory> memory,
                               std::shared_ptr<Bus> bus = nullptr);

    // 检查是否是有效的 ELF 文件
    bool isValidElf() const { return is_valid_; }

    // 获取入口点
    uint32 getEntryPoint() const { return elf_header_.e_entry; }

    // 获取程序头数量
    uint16 getProgramHeaderCount() const { return elf_header_.e_phnum; }

    // 获取机器架构
    uint16 getMachine() const { return elf_header_.e_machine; }

    // 获取文件类型
    uint16 getFileType() const { return elf_header_.e_type; }

    // 打印 ELF 信息 (调试用)
    std::string getInfo() const;

private:
    bool parseHeader();
    std::string readString(uint32 offset) const;

    bool is_valid_;
    bool is_riscv_;
    std::vector<uint8> elf_data_;
    Elf32_Ehdr elf_header_;
    std::vector<Elf32_Phdr> program_headers_;
    uint32 max_load_addr_;
};

} // namespace mycpu
