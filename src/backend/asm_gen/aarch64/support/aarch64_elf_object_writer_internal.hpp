#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.hpp"

namespace sysycc {

inline constexpr std::uint32_t kAArch64ElfSectionProgBits = 1;
inline constexpr std::uint32_t kAArch64ElfSectionNoBits = 8;

struct PendingRelocation {
    std::size_t offset = 0;
    AArch64RelocationRecord record;
};

struct SectionImage {
    AArch64SectionKind kind = AArch64SectionKind::Data;
    std::string name;
    std::uint32_t type = kAArch64ElfSectionProgBits;
    std::uint64_t flags = 0;
    std::uint64_t align = 1;
    std::uint64_t entry_size = 0;
    std::vector<std::uint8_t> bytes;
    std::size_t nobits_size = 0;
    std::vector<PendingRelocation> relocations;
    std::uint32_t section_index = 0;
};

struct FinalSection {
    std::string name;
    std::uint32_t type = kAArch64ElfSectionProgBits;
    std::uint64_t flags = 0;
    std::uint64_t align = 1;
    std::uint64_t entry_size = 0;
    std::vector<std::uint8_t> bytes;
    std::size_t logical_size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t file_offset = 0;
};

struct SymbolEntry {
    std::string name;
    std::uint8_t binding = 0;
    std::uint8_t type = 1;
    std::uint16_t section_index = 0;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t index = 0;
};

struct DefinedSymbol {
    AArch64SectionKind section_kind = AArch64SectionKind::Data;
    std::size_t offset = 0;
    std::size_t size = 0;
};

enum class ParsedCfiNoteKind : unsigned char {
    DefCfa,
    DefCfaRegister,
    DefCfaOffset,
    Offset,
    Restore,
};

struct ParsedCfiNote {
    std::size_t pc_offset = 0;
    ParsedCfiNoteKind kind = ParsedCfiNoteKind::DefCfa;
    unsigned reg = 0;
    long long offset = 0;
};

template <typename T>
inline void append_pod(std::vector<std::uint8_t> &bytes, const T &value) {
    const auto *raw = reinterpret_cast<const std::uint8_t *>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

inline std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

inline void overwrite_u32(std::vector<std::uint8_t> &bytes, std::size_t offset,
                          std::uint32_t value) {
    if (offset + sizeof(value) > bytes.size()) {
        return;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

inline void append_uleb128(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    } while (value != 0);
}

inline void append_sleb128(std::vector<std::uint8_t> &bytes, std::int64_t value) {
    bool more = true;
    while (more) {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        const bool sign_bit_set = (byte & 0x40U) != 0;
        value >>= 7;
        more = !((value == 0 && !sign_bit_set) || (value == -1 && sign_bit_set));
        if (more) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    }
}

inline const char *section_name_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Text:
        return ".text";
    case AArch64SectionKind::Data:
        return ".data";
    case AArch64SectionKind::ReadOnlyData:
        return ".rodata";
    case AArch64SectionKind::Bss:
        return ".bss";
    case AArch64SectionKind::EhFrame:
        return ".eh_frame";
    case AArch64SectionKind::DebugLine:
        return ".debug_line";
    case AArch64SectionKind::DebugInfo:
        return ".debug_info";
    case AArch64SectionKind::DebugAbbrev:
        return ".debug_abbrev";
    case AArch64SectionKind::DebugStr:
        return ".debug_str";
    default:
        return nullptr;
    }
}

inline std::uint64_t section_flags_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Text:
        return 0x2 | 0x4;
    case AArch64SectionKind::Data:
    case AArch64SectionKind::Bss:
        return 0x2 | 0x1;
    case AArch64SectionKind::ReadOnlyData:
    case AArch64SectionKind::EhFrame:
        return 0x2;
    case AArch64SectionKind::DebugLine:
    case AArch64SectionKind::DebugFrame:
    case AArch64SectionKind::DebugInfo:
    case AArch64SectionKind::DebugAbbrev:
    case AArch64SectionKind::DebugStr:
        return 0;
    default:
        return 0;
    }
}

inline SectionImage &ensure_section_image(std::vector<SectionImage> &sections,
                                          AArch64SectionKind kind) {
    for (SectionImage &section : sections) {
        if (section.kind == kind) {
            return section;
        }
    }
    SectionImage section;
    section.kind = kind;
    section.name = section_name_for_object(kind);
    section.flags = section_flags_for_object(kind);
    section.type = kind == AArch64SectionKind::Bss ? kAArch64ElfSectionNoBits
                                                   : kAArch64ElfSectionProgBits;
    section.align = kind == AArch64SectionKind::Text ? 4 : 1;
    sections.push_back(std::move(section));
    return sections.back();
}

} // namespace sysycc
