#include "backend/asm_gen/aarch64/support/aarch64_elf_section_serializer_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr unsigned char kElfClass64 = 2;
constexpr unsigned char kElfDataLittleEndian = 1;
constexpr unsigned char kElfVersionCurrent = 1;
constexpr std::uint16_t kElfTypeRelocatable = 1;
constexpr std::uint16_t kElfMachineAArch64 = 183;
constexpr std::uint32_t kElfSectionSymTab = 2;
constexpr std::uint32_t kElfSectionStrTab = 3;
constexpr std::uint32_t kElfSectionRela = 4;
constexpr std::uint8_t kElfSymbolBindingGlobal = 1;
constexpr std::uint32_t kAArch64RelocAbs64 = 257;
constexpr std::uint32_t kAArch64RelocAbs32 = 258;
constexpr std::uint32_t kAArch64RelocPrel32 = 261;
constexpr std::uint32_t kAArch64RelocAdrPrelPgHi21 = 275;
constexpr std::uint32_t kAArch64RelocAddAbsLo12Nc = 277;
constexpr std::uint32_t kAArch64RelocJump26 = 282;
constexpr std::uint32_t kAArch64RelocCall26 = 283;
constexpr std::uint32_t kAArch64RelocAdrGotPage = 311;
constexpr std::uint32_t kAArch64RelocLd64GotLo12Nc = 312;

struct Elf64Header {
    std::array<unsigned char, 16> ident{};
    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint32_t version = 0;
    std::uint64_t entry = 0;
    std::uint64_t program_header_offset = 0;
    std::uint64_t section_header_offset = 0;
    std::uint32_t flags = 0;
    std::uint16_t header_size = 0;
    std::uint16_t program_header_entry_size = 0;
    std::uint16_t program_header_count = 0;
    std::uint16_t section_header_entry_size = 0;
    std::uint16_t section_header_count = 0;
    std::uint16_t section_name_string_table_index = 0;
};

struct Elf64SectionHeader {
    std::uint32_t name = 0;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t address = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t address_align = 0;
    std::uint64_t entry_size = 0;
};

struct Elf64Symbol {
    std::uint32_t name = 0;
    unsigned char info = 0;
    unsigned char other = 0;
    std::uint16_t section_index = 0;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
};

struct Elf64Rela {
    std::uint64_t offset = 0;
    std::uint64_t info = 0;
    std::int64_t addend = 0;
};

static_assert(sizeof(Elf64Header) == 64);
static_assert(sizeof(Elf64SectionHeader) == 64);
static_assert(sizeof(Elf64Symbol) == 24);
static_assert(sizeof(Elf64Rela) == 24);

std::uint32_t relocation_type_for_record(AArch64RelocationKind kind) {
    switch (kind) {
    case AArch64RelocationKind::Absolute32:
        return kAArch64RelocAbs32;
    case AArch64RelocationKind::Absolute64:
        return kAArch64RelocAbs64;
    case AArch64RelocationKind::Prel32:
        return kAArch64RelocPrel32;
    case AArch64RelocationKind::Page21:
        return kAArch64RelocAdrPrelPgHi21;
    case AArch64RelocationKind::PageOffset12:
        return kAArch64RelocAddAbsLo12Nc;
    case AArch64RelocationKind::Branch26:
        return kAArch64RelocJump26;
    case AArch64RelocationKind::Call26:
        return kAArch64RelocCall26;
    case AArch64RelocationKind::GotPage21:
        return kAArch64RelocAdrGotPage;
    case AArch64RelocationKind::GotLo12:
        return kAArch64RelocLd64GotLo12Nc;
    default:
        return 0;
    }
}

std::uint32_t append_string(std::vector<std::uint8_t> &table,
                            const std::string &text) {
    const std::uint32_t offset = static_cast<std::uint32_t>(table.size());
    table.insert(table.end(), text.begin(), text.end());
    table.push_back('\0');
    return offset;
}

bool write_output_file(const std::filesystem::path &path,
                       const std::vector<std::uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

} // namespace

bool write_aarch64_elf_sectioned_object(
    const std::filesystem::path &object_file,
    const std::vector<SectionImage> &section_images,
    std::vector<SymbolEntry> symbols,
    const std::unordered_map<std::string, std::uint32_t> &symbol_indices,
    DiagnosticEngine &diagnostic_engine, const char *error_prefix) {
    std::vector<FinalSection> sections;
    sections.reserve(section_images.size() * 2 + 3);
    for (const SectionImage &image : section_images) {
        FinalSection section;
        section.name = image.name;
        section.type = image.type;
        section.flags = image.flags;
        section.align = image.align;
        section.entry_size = image.entry_size;
        section.bytes = image.bytes;
        section.logical_size =
            image.type == kAArch64ElfSectionNoBits ? image.nobits_size
                                                   : image.bytes.size();
        sections.push_back(std::move(section));
    }

    std::vector<std::uint8_t> symbol_string_table(1, '\0');
    for (std::size_t index = 1; index < symbols.size(); ++index) {
        symbols[index].name_offset =
            append_string(symbol_string_table, symbols[index].name);
    }

    std::vector<std::uint8_t> symtab_bytes;
    symtab_bytes.reserve(symbols.size() * sizeof(Elf64Symbol));
    std::uint32_t first_global_symbol_index = 1;
    bool saw_global_symbol = false;
    for (const SymbolEntry &entry : symbols) {
        if (!saw_global_symbol && entry.binding == kElfSymbolBindingGlobal &&
            entry.index != 0) {
            first_global_symbol_index = entry.index;
            saw_global_symbol = true;
        }
        Elf64Symbol elf_symbol;
        elf_symbol.name = entry.name_offset;
        elf_symbol.info = static_cast<unsigned char>((entry.binding << 4) |
                                                     (entry.type & 0x0fU));
        elf_symbol.section_index = entry.section_index;
        elf_symbol.value = entry.value;
        elf_symbol.size = entry.size;
        append_pod(symtab_bytes, elf_symbol);
    }

    std::vector<FinalSection> relocation_sections;
    for (const SectionImage &image : section_images) {
        if (image.relocations.empty()) {
            continue;
        }
        FinalSection rela_section;
        rela_section.name = ".rela" + image.name;
        rela_section.type = kElfSectionRela;
        rela_section.align = 8;
        rela_section.entry_size = sizeof(Elf64Rela);
        rela_section.info = image.section_index;
        for (const PendingRelocation &relocation : image.relocations) {
            const std::uint32_t reloc_type =
                relocation_type_for_record(relocation.record.kind);
            if (reloc_type == 0) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    std::string(error_prefix) +
                        "encountered an unsupported relocation kind");
                return false;
            }
            const auto symbol_index_it =
                symbol_indices.find(relocation.record.target.get_name());
            if (symbol_index_it == symbol_indices.end()) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    std::string(error_prefix) + "missing relocation symbol '" +
                        relocation.record.target.get_name() + "'");
                return false;
            }
            Elf64Rela rela;
            rela.offset = relocation.offset;
            rela.info = (static_cast<std::uint64_t>(symbol_index_it->second) << 32) |
                        reloc_type;
            rela.addend = relocation.record.target.addend;
            append_pod(rela_section.bytes, rela);
        }
        rela_section.logical_size = rela_section.bytes.size();
        relocation_sections.push_back(std::move(rela_section));
    }

    const std::uint32_t symtab_section_index = static_cast<std::uint32_t>(
        sections.size() + relocation_sections.size() + 1);
    const std::uint32_t strtab_section_index = symtab_section_index + 1;
    for (FinalSection &rela_section : relocation_sections) {
        rela_section.link = symtab_section_index;
        sections.push_back(std::move(rela_section));
    }

    FinalSection symtab_section;
    symtab_section.name = ".symtab";
    symtab_section.type = kElfSectionSymTab;
    symtab_section.align = 8;
    symtab_section.entry_size = sizeof(Elf64Symbol);
    symtab_section.bytes = std::move(symtab_bytes);
    symtab_section.logical_size = symtab_section.bytes.size();
    symtab_section.link = strtab_section_index;
    symtab_section.info = saw_global_symbol ? first_global_symbol_index
                                            : static_cast<std::uint32_t>(symbols.size());
    sections.push_back(std::move(symtab_section));

    FinalSection strtab_section;
    strtab_section.name = ".strtab";
    strtab_section.type = kElfSectionStrTab;
    strtab_section.align = 1;
    strtab_section.bytes = std::move(symbol_string_table);
    strtab_section.logical_size = strtab_section.bytes.size();
    sections.push_back(std::move(strtab_section));

    std::vector<std::uint8_t> section_name_table(1, '\0');
    std::vector<std::uint32_t> section_name_offsets;
    section_name_offsets.reserve(sections.size() + 1);
    section_name_offsets.push_back(0);
    for (const FinalSection &section : sections) {
        section_name_offsets.push_back(append_string(section_name_table, section.name));
    }
    const std::uint32_t shstrtab_name_offset =
        append_string(section_name_table, ".shstrtab");
    section_name_offsets.push_back(shstrtab_name_offset);

    FinalSection shstrtab_section;
    shstrtab_section.name = ".shstrtab";
    shstrtab_section.type = kElfSectionStrTab;
    shstrtab_section.align = 1;
    shstrtab_section.bytes = std::move(section_name_table);
    shstrtab_section.logical_size = shstrtab_section.bytes.size();
    sections.push_back(std::move(shstrtab_section));

    const std::uint16_t shstrtab_section_index =
        static_cast<std::uint16_t>(sections.size());

    std::vector<std::uint8_t> object_bytes(sizeof(Elf64Header), 0);
    for (FinalSection &section : sections) {
        if (section.type == kAArch64ElfSectionNoBits) {
            section.file_offset = align_to(object_bytes.size(), section.align);
            continue;
        }
        object_bytes.resize(align_to(object_bytes.size(), section.align), 0);
        section.file_offset = object_bytes.size();
        object_bytes.insert(object_bytes.end(), section.bytes.begin(),
                            section.bytes.end());
    }
    const std::uint64_t section_header_offset = align_to(object_bytes.size(), 8);
    object_bytes.resize(section_header_offset, 0);

    std::vector<Elf64SectionHeader> section_headers(sections.size() + 1);
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const FinalSection &section = sections[index];
        Elf64SectionHeader &header = section_headers[index + 1];
        header.name = section_name_offsets[index + 1];
        header.type = section.type;
        header.flags = section.flags;
        header.offset = section.file_offset;
        header.size = section.logical_size;
        header.link = section.link;
        header.info = section.info;
        header.address_align = std::max<std::uint64_t>(1, section.align);
        header.entry_size = section.entry_size;
    }
    section_headers.back().name = shstrtab_name_offset;

    for (const Elf64SectionHeader &header : section_headers) {
        append_pod(object_bytes, header);
    }

    Elf64Header header;
    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = kElfClass64;
    header.ident[5] = kElfDataLittleEndian;
    header.ident[6] = kElfVersionCurrent;
    header.type = kElfTypeRelocatable;
    header.machine = kElfMachineAArch64;
    header.version = kElfVersionCurrent;
    header.section_header_offset = section_header_offset;
    header.header_size = sizeof(Elf64Header);
    header.section_header_entry_size = sizeof(Elf64SectionHeader);
    header.section_header_count = static_cast<std::uint16_t>(section_headers.size());
    header.section_name_string_table_index = shstrtab_section_index;
    std::memcpy(object_bytes.data(), &header, sizeof(header));

    if (!write_output_file(object_file, object_bytes)) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                    std::string(error_prefix) +
                                        "failed to write the native AArch64 object file");
        return false;
    }
    return true;
}

} // namespace sysycc
