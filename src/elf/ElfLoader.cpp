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
    // 妫€鏌?Magic Number
    if (std::memcmp(elf_data_.data(), ElfConst::ELF_MAGIC, 4) != 0) {
        return false;
    }

    // 妫€鏌?ELF Class (鍙敮鎸?32-bit)
    if (elf_data_[4] != ElfConst::ELFCLASS32) {
        return false;
    }

    // 妫€鏌?Endianness (鍙敮鎸佸皬绔?
    if (elf_data_[5] != ElfConst::ELFDATA2LSB) {
        return false;
    }

        // Ensure segment data is loaded with 4-byte alignment.
    std::memcpy(&elf_header_, elf_data_.data(), sizeof(Elf32_Ehdr));

    if (elf_header_.e_machine != ElfConst::EM_RISCV) {
        is_riscv_ = false;
        return false;
    }
    is_riscv_ = true;

    // 瑙ｆ瀽绋嬪簭澶?
    program_headers_.clear();
    if (elf_header_.e_phoff > 0 && elf_header_.e_phnum > 0) {
        for (uint16 i = 0; i < elf_header_.e_phnum; ++i) {
            Elf32_Phdr phdr;
            size_t offset = elf_header_.e_phoff + i * elf_header_.e_phentsize;
            std::memcpy(&phdr, elf_data_.data() + offset, sizeof(Elf32_Phdr));
            program_headers_.push_back(phdr);

            // 璁板綍鏈€澶у姞杞藉湴鍧€
            uint32 end_addr = phdr.p_vaddr + phdr.p_memsz;
            if (end_addr > max_load_addr_) {
                max_load_addr_ = end_addr;
            }
        }
    }

    if (!parseSectionHeaders()) {
        return false;
    }

    return true;
}

bool ElfLoader::parseSectionHeaders() {
    section_headers_.clear();
    section_string_table_.clear();

    if (elf_header_.e_shoff == 0 || elf_header_.e_shnum == 0) {
        return true;
    }

    for (uint16 i = 0; i < elf_header_.e_shnum; ++i) {
        Elf32_Shdr shdr;
        size_t offset = elf_header_.e_shoff + i * elf_header_.e_shentsize;
        if (offset + sizeof(Elf32_Shdr) > elf_data_.size()) {
            return false;
        }
        std::memcpy(&shdr, elf_data_.data() + offset, sizeof(Elf32_Shdr));
        section_headers_.push_back(shdr);
    }

    if (elf_header_.e_shstrndx >= section_headers_.size()) {
        return true;
    }

    const auto& shstr = section_headers_[elf_header_.e_shstrndx];
    if (shstr.sh_offset + shstr.sh_size > elf_data_.size()) {
        return false;
    }

    section_string_table_.assign(
        reinterpret_cast<const char*>(elf_data_.data() + shstr.sh_offset),
        shstr.sh_size
    );
    return true;
}

ElfLoadResult ElfLoader::loadToMemory(std::shared_ptr<Memory> memory,
                                      std::shared_ptr<Bus> bus) {
    (void)bus;
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
        // 鍙鐞?PT_LOAD 娈?
        if (phdr.p_type != ElfConst::PT_LOAD) {
            continue;
        }

        if (phdr.p_filesz == 0 && phdr.p_memsz == 0) {
            continue;
        }

        // 妫€鏌ヨ竟鐣?
        if (phdr.p_offset + phdr.p_filesz > elf_data_.size()) {
            result.error_message = "Segment out of bounds";
            return result;
        }

        // 鍔犺浇鏁版嵁鍒板唴瀛?
        uint32 vaddr = phdr.p_vaddr;
        uint32 memsz = phdr.p_memsz;

        // 鍏堟竻闆?.bss 娈?
        if (phdr.p_filesz < phdr.p_memsz) {
            uint32 bss_start = vaddr + phdr.p_filesz;
            uint32 bss_size = memsz - phdr.p_filesz;
            for (uint32 i = 0; i < bss_size; ++i) {
                memory->writeByte(bss_start + i, 0);
            }
        }

        // Copy segment bytes into simulated memory.
        for (uint32 i = 0; i < phdr.p_filesz; ++i) {
            memory->writeByte(vaddr + i, elf_data_[phdr.p_offset + i]);
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

uint32 ElfLoader::getSectionAddress(const std::string& name) const {
    if (section_string_table_.empty()) {
        return 0;
    }

    for (const auto& shdr : section_headers_) {
        if (shdr.sh_name >= section_string_table_.size()) {
            continue;
        }

        const char* section_name = section_string_table_.c_str() + shdr.sh_name;
        if (name == section_name) {
            return shdr.sh_addr;
        }
    }

    return 0;
}

std::string ElfLoader::readString(uint32 offset) const {
    if (offset >= elf_data_.size()) return "";
    const char* str = reinterpret_cast<const char*>(elf_data_.data() + offset);
    return std::string(str);
}

} // namespace mycpu
