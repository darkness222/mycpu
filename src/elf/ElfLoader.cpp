#include "ElfLoader.h"
#include "../memory/Memory.h"
#include "../bus/Bus.h"
#include <cstring>
#include <sstream>

namespace mycpu {

ElfLoader::ElfLoader()
    : is_valid_(false), is_riscv_(false), max_load_addr_(0) {
    std::memset(&elf_header_, 0, sizeof(elf_header_));
}

bool ElfLoader::loadFromBytes(const std::vector<uint8>& data) {
    elf_data_ = data;

    if (elf_data_.size() < sizeof(Elf32_Ehdr)) {
        return false;
    }

    is_valid_ = parseHeader();
    return is_valid_;
}

bool ElfLoader::parseHeader() {
    // 检查 Magic Number
    if (std::memcmp(elf_data_.data(), ElfConst::ELF_MAGIC, 4) != 0) {
        return false;
    }

    // 检查 ELF Class (只支持 32-bit)
    if (elf_data_[4] != ElfConst::ELFCLASS32) {
        return false;
    }

    // 检查 Endianness (只支持小端)
    if (elf_data_[5] != ElfConst::ELFDATA2LSB) {
        return false;
    }

    // 检查机器架构
    std::memcpy(&elf_header_, elf_data_.data(), sizeof(Elf32_Ehdr));

    if (elf_header_.e_machine != ElfConst::EM_RISCV) {
        is_riscv_ = false;
        return false;
    }
    is_riscv_ = true;

    // 解析程序头
    program_headers_.clear();
    if (elf_header_.e_phoff > 0 && elf_header_.e_phnum > 0) {
        for (uint16 i = 0; i < elf_header_.e_phnum; ++i) {
            Elf32_Phdr phdr;
            size_t offset = elf_header_.e_phoff + i * elf_header_.e_phentsize;
            std::memcpy(&phdr, elf_data_.data() + offset, sizeof(Elf32_Phdr));
            program_headers_.push_back(phdr);

            // 记录最大加载地址
            uint32 end_addr = phdr.p_vaddr + phdr.p_memsz;
            if (end_addr > max_load_addr_) {
                max_load_addr_ = end_addr;
            }
        }
    }

    return true;
}

ElfLoadResult ElfLoader::loadToMemory(std::shared_ptr<Memory> memory,
                                      std::shared_ptr<Bus> bus) {
    ElfLoadResult result;
    result.success = false;
    result.entry_point = 0;
    result.load_size = 0;
    result.segment_count = 0;

    if (!is_valid_ || !memory) {
        result.error_message = "Invalid ELF or no memory";
        return result;
    }

    if (!is_riscv_) {
        result.error_message = "Not a RISC-V ELF file";
        return result;
    }

    result.entry_point = elf_header_.e_entry;

    for (const auto& phdr : program_headers_) {
        // 只处理 PT_LOAD 段
        if (phdr.p_type != ElfConst::PT_LOAD) {
            continue;
        }

        if (phdr.p_filesz == 0 && phdr.p_memsz == 0) {
            continue;
        }

        // 检查边界
        if (phdr.p_offset + phdr.p_filesz > elf_data_.size()) {
            result.error_message = "Segment out of bounds";
            return result;
        }

        // 加载数据到内存
        uint32 vaddr = phdr.p_vaddr;
        uint32 memsz = phdr.p_memsz;

        // 先清零 .bss 段
        if (phdr.p_filesz < phdr.p_memsz) {
            uint32 bss_start = vaddr + phdr.p_filesz;
            uint32 bss_size = memsz - phdr.p_filesz;
            for (uint32 i = 0; i < bss_size; i += 4) {
                memory->writeWord(bss_start + i, 0);
            }
        }

        // 复制文件数据到内存
        for (uint32 i = 0; i < phdr.p_filesz; i += 4) {
            uint32 word = 0;
            std::memcpy(&word, elf_data_.data() + phdr.p_offset + i, 4);
            memory->writeWord(vaddr + i, word);
        }

        result.loaded_segments.push_back(vaddr);
        result.load_size += phdr.p_memsz;
        result.segment_count++;
    }

    result.success = true;
    return result;
}

std::string ElfLoader::getInfo() const {
    std::ostringstream oss;
    oss << "ELF Info:\n";
    oss << "  Valid: " << (is_valid_ ? "Yes" : "No") << "\n";
    oss << "  RISC-V: " << (is_riscv_ ? "Yes" : "No") << "\n";
    oss << "  Entry: 0x" << std::hex << elf_header_.e_entry << "\n";
    oss << "  Type: " << std::dec;

    switch (elf_header_.e_type) {
        case ElfConst::ET_NONE: oss << "None"; break;
        case ElfConst::ET_REL: oss << "Relocatable"; break;
        case ElfConst::ET_EXEC: oss << "Executable"; break;
        default: oss << "Unknown"; break;
    }

    oss << "\n  Program Headers: " << std::dec << elf_header_.e_phnum << "\n";
    oss << "  Sections: " << elf_header_.e_shnum << "\n";

    for (size_t i = 0; i < program_headers_.size(); ++i) {
        const auto& phdr = program_headers_[i];
        if (phdr.p_type == ElfConst::PT_LOAD) {
            oss << "  Segment " << i << ": "
                << "vaddr=0x" << std::hex << phdr.p_vaddr
                << " filesz=0x" << phdr.p_filesz
                << " memsz=0x" << phdr.p_memsz
                << " flags=0x" << phdr.p_flags << "\n";
        }
    }

    return oss.str();
}

std::string ElfLoader::readString(uint32 offset) const {
    if (offset >= elf_data_.size()) return "";
    const char* str = reinterpret_cast<const char*>(elf_data_.data() + offset);
    return std::string(str);
}

} // namespace mycpu
