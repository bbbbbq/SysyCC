#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
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
constexpr std::uint32_t kElfSectionNull = 0;
constexpr std::uint32_t kElfSectionProgBits = 1;
constexpr std::uint32_t kElfSectionSymTab = 2;
constexpr std::uint32_t kElfSectionStrTab = 3;
constexpr std::uint32_t kElfSectionRela = 4;
constexpr std::uint32_t kElfSectionNoBits = 8;
constexpr std::uint64_t kElfSectionFlagWrite = 0x1;
constexpr std::uint64_t kElfSectionFlagAlloc = 0x2;
constexpr std::uint16_t kElfSymbolUndefined = 0;
constexpr std::uint8_t kElfSymbolBindingLocal = 0;
constexpr std::uint8_t kElfSymbolBindingGlobal = 1;
constexpr std::uint8_t kElfSymbolTypeNoType = 0;
constexpr std::uint8_t kElfSymbolTypeObject = 1;
constexpr std::uint8_t kElfSymbolTypeFunction = 2;
constexpr std::uint32_t kAArch64RelocAbs64 = 257;
constexpr std::uint32_t kAArch64RelocAbs32 = 258;

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

struct DataSymbolDefinition {
    AArch64SectionKind section_kind = AArch64SectionKind::Data;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct PendingRelocation {
    std::size_t offset = 0;
    AArch64RelocationRecord record;
};

struct SectionImage {
    AArch64SectionKind kind = AArch64SectionKind::Data;
    std::string name;
    std::uint32_t type = kElfSectionProgBits;
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
    std::uint32_t type = kElfSectionProgBits;
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
    std::uint8_t binding = kElfSymbolBindingLocal;
    std::uint8_t type = kElfSymbolTypeObject;
    std::uint16_t section_index = kElfSymbolUndefined;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t index = 0;
};

template <typename T>
void append_pod(std::vector<std::uint8_t> &bytes, const T &value) {
    const auto *raw = reinterpret_cast<const std::uint8_t *>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

const char *section_name_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Data:
        return ".data";
    case AArch64SectionKind::ReadOnlyData:
        return ".rodata";
    case AArch64SectionKind::Bss:
        return ".bss";
    default:
        return nullptr;
    }
}

std::uint64_t section_flags_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Data:
    case AArch64SectionKind::Bss:
        return kElfSectionFlagAlloc | kElfSectionFlagWrite;
    case AArch64SectionKind::ReadOnlyData:
        return kElfSectionFlagAlloc;
    default:
        return 0;
    }
}

std::uint32_t relocation_type_for_data_record(AArch64RelocationKind kind) {
    switch (kind) {
    case AArch64RelocationKind::Absolute32:
        return kAArch64RelocAbs32;
    case AArch64RelocationKind::Absolute64:
        return kAArch64RelocAbs64;
    default:
        return 0;
    }
}

std::uint8_t elf_symbol_type(AArch64SymbolKind kind) {
    switch (kind) {
    case AArch64SymbolKind::Function:
    case AArch64SymbolKind::Helper:
        return kElfSymbolTypeFunction;
    case AArch64SymbolKind::Object:
    default:
        return kElfSymbolTypeObject;
    }
}

bool append_fragment_to_section(
    SectionImage &section, const AArch64DataFragment &fragment,
    DiagnosticEngine &diagnostic_engine) {
    const std::size_t fragment_base =
        section.type == kElfSectionNoBits ? section.nobits_size : section.bytes.size();
    if (const auto *zero_fill = fragment.get_zero_fill(); zero_fill != nullptr) {
        if (section.type == kElfSectionNoBits) {
            section.nobits_size += zero_fill->size;
        } else {
            section.bytes.insert(section.bytes.end(), zero_fill->size, 0);
        }
        return true;
    }
    if (const auto *bytes = fragment.get_byte_sequence(); bytes != nullptr) {
        if (section.type == kElfSectionNoBits) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 native data-only object writer cannot place initialized bytes into .bss");
            return false;
        }
        section.bytes.insert(section.bytes.end(), bytes->bytes.begin(),
                             bytes->bytes.end());
        for (const AArch64RelocationRecord &relocation : fragment.get_relocations()) {
            section.relocations.push_back(
                PendingRelocation{fragment_base + relocation.offset, relocation});
        }
        return true;
    }
    if (fragment.is_scalar_value()) {
        const std::size_t scalar_size = fragment.get_scalar_size();
        const std::uint64_t scalar_bits = fragment.get_scalar_bits();
        if (section.type == kElfSectionNoBits) {
            if (scalar_bits != 0 || !fragment.get_relocations().empty()) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    "AArch64 native data-only object writer cannot place non-zero or relocatable scalars into .bss");
                return false;
            }
            section.nobits_size += scalar_size;
            return true;
        }
        for (std::size_t byte_index = 0; byte_index < scalar_size; ++byte_index) {
            section.bytes.push_back(static_cast<std::uint8_t>(
                (scalar_bits >> (byte_index * 8)) & 0xffU));
        }
        for (const AArch64RelocationRecord &relocation : fragment.get_relocations()) {
            section.relocations.push_back(
                PendingRelocation{fragment_base + relocation.offset, relocation});
        }
        return true;
    }
    return true;
}

bool build_data_section_images(
    const AArch64ObjectModule &object_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DataSymbolDefinition> &symbol_definitions,
    DiagnosticEngine &diagnostic_engine) {
    auto ensure_section = [&](AArch64SectionKind kind) -> SectionImage & {
        for (SectionImage &section : sections) {
            if (section.kind == kind) {
                return section;
            }
        }
        SectionImage section;
        section.kind = kind;
        section.name = section_name_for_object(kind);
        section.flags = section_flags_for_object(kind);
        section.type = kind == AArch64SectionKind::Bss ? kElfSectionNoBits
                                                       : kElfSectionProgBits;
        sections.push_back(std::move(section));
        return sections.back();
    };

    for (const AArch64DataObject &data_object : object_module.get_data_objects()) {
        const char *section_name = section_name_for_object(data_object.get_section_kind());
        if (section_name == nullptr) {
            continue;
        }
        SectionImage &section = ensure_section(data_object.get_section_kind());
        const std::size_t align =
            std::max<std::size_t>(1, static_cast<std::size_t>(1)
                                         << data_object.get_align_log2());
        section.align = std::max<std::uint64_t>(section.align, align);
        const std::size_t section_size =
            section.type == kElfSectionNoBits ? section.nobits_size : section.bytes.size();
        const std::size_t aligned_size = align_to(section_size, align);
        if (section.type == kElfSectionNoBits) {
            section.nobits_size = aligned_size;
        } else {
            section.bytes.insert(section.bytes.end(), aligned_size - section_size, 0);
        }
        symbol_definitions[data_object.get_symbol_name()] = DataSymbolDefinition{
            data_object.get_section_kind(), aligned_size, data_object.get_size()};
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            if (!append_fragment_to_section(section, fragment, diagnostic_engine)) {
                return false;
            }
        }
    }
    return true;
}

bool build_symbol_entries(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, DataSymbolDefinition> &data_symbol_definitions,
    const std::vector<SectionImage> &data_sections,
    const AArch64DataOnlyObjectWriterOptions &options,
    std::vector<SymbolEntry> &symbols,
    std::unordered_map<std::string, std::uint32_t> &symbol_indices) {
    std::vector<std::string> needed_symbol_names;
    needed_symbol_names.reserve(data_symbol_definitions.size());
    for (const auto &[name, definition] : data_symbol_definitions) {
        (void)definition;
        needed_symbol_names.push_back(name);
    }
    for (const SectionImage &section : data_sections) {
        for (const PendingRelocation &relocation : section.relocations) {
            if (std::find(needed_symbol_names.begin(), needed_symbol_names.end(),
                          relocation.record.symbol_name) ==
                needed_symbol_names.end()) {
                needed_symbol_names.push_back(relocation.record.symbol_name);
            }
        }
    }

    std::vector<SymbolEntry> local_symbols;
    std::vector<SymbolEntry> global_symbols;
    for (const std::string &name : needed_symbol_names) {
        const auto module_symbol_it = object_module.get_symbols().find(name);
        const bool has_module_symbol =
            module_symbol_it != object_module.get_symbols().end();
        const AArch64SymbolKind symbol_kind =
            has_module_symbol ? module_symbol_it->second.get_kind()
                              : AArch64SymbolKind::Object;
        const bool symbol_is_global =
            has_module_symbol ? module_symbol_it->second.get_is_global() : true;

        SymbolEntry entry;
        entry.name = name;
        entry.type = elf_symbol_type(symbol_kind);

        const auto definition_it = data_symbol_definitions.find(name);
        if (definition_it != data_symbol_definitions.end()) {
            const DataSymbolDefinition &definition = definition_it->second;
            const auto section_it = std::find_if(
                data_sections.begin(), data_sections.end(),
                [&](const SectionImage &section) {
                    return section.kind == definition.section_kind;
                });
            if (section_it == data_sections.end()) {
                return false;
            }
            entry.section_index = section_it->section_index;
            entry.value = definition.offset;
            entry.size = definition.size;
            entry.binding = (options.force_defined_symbols_global || symbol_is_global)
                                ? kElfSymbolBindingGlobal
                                : kElfSymbolBindingLocal;
        } else {
            entry.binding = kElfSymbolBindingGlobal;
        }

        if (entry.binding == kElfSymbolBindingLocal) {
            local_symbols.push_back(std::move(entry));
        } else {
            global_symbols.push_back(std::move(entry));
        }
    }

    symbols.clear();
    symbols.push_back(SymbolEntry{});
    for (SymbolEntry &entry : local_symbols) {
        entry.index = static_cast<std::uint32_t>(symbols.size());
        symbol_indices[entry.name] = entry.index;
        symbols.push_back(std::move(entry));
    }
    for (SymbolEntry &entry : global_symbols) {
        entry.index = static_cast<std::uint32_t>(symbols.size());
        symbol_indices[entry.name] = entry.index;
        symbols.push_back(std::move(entry));
    }
    return true;
}

std::uint32_t append_string(std::vector<std::uint8_t> &table, const std::string &text) {
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

bool write_aarch64_data_only_object(
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64DataOnlyObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine) {
    std::vector<SectionImage> data_sections;
    std::unordered_map<std::string, DataSymbolDefinition> data_symbol_definitions;
    if (!build_data_section_images(object_module, data_sections,
                                   data_symbol_definitions,
                                   diagnostic_engine)) {
        return false;
    }

    if (data_sections.empty()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 native data-only object writer received a module without data sections");
        return false;
    }

    std::uint32_t next_section_index = 1;
    for (SectionImage &section : data_sections) {
        section.section_index = next_section_index++;
    }

    std::vector<SymbolEntry> symbols;
    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    if (!build_symbol_entries(object_module, data_symbol_definitions, data_sections,
                              options, symbols, symbol_indices)) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to build the AArch64 native data-only object symbol table");
        return false;
    }

    std::vector<FinalSection> sections;
    sections.reserve(data_sections.size() * 2 + 3);
    for (const SectionImage &data_section : data_sections) {
        FinalSection section;
        section.name = data_section.name;
        section.type = data_section.type;
        section.flags = data_section.flags;
        section.align = data_section.align;
        section.entry_size = data_section.entry_size;
        section.bytes = data_section.bytes;
        section.logical_size = data_section.type == kElfSectionNoBits
                                   ? data_section.nobits_size
                                   : data_section.bytes.size();
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
        if (!saw_global_symbol &&
            entry.binding == kElfSymbolBindingGlobal &&
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
    for (const SectionImage &data_section : data_sections) {
        if (data_section.relocations.empty()) {
            continue;
        }
        FinalSection rela_section;
        rela_section.name = ".rela" + data_section.name;
        rela_section.type = kElfSectionRela;
        rela_section.align = 8;
        rela_section.entry_size = sizeof(Elf64Rela);
        rela_section.info = data_section.section_index;
        for (const PendingRelocation &relocation : data_section.relocations) {
            const std::uint32_t reloc_type =
                relocation_type_for_data_record(relocation.record.kind);
            if (reloc_type == 0) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    "AArch64 native data-only object writer only supports absolute data relocations today");
                return false;
            }
            const auto symbol_index_it =
                symbol_indices.find(relocation.record.symbol_name);
            if (symbol_index_it == symbol_indices.end()) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    "missing relocation symbol '" + relocation.record.symbol_name +
                        "' while writing the AArch64 native data-only object");
                return false;
            }
            Elf64Rela rela;
            rela.offset = relocation.offset;
            rela.info = (static_cast<std::uint64_t>(symbol_index_it->second) << 32) |
                        reloc_type;
            rela.addend = relocation.record.addend;
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
        if (section.type == kElfSectionNoBits) {
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
                                    "failed to write the native AArch64 data-only object file");
        return false;
    }
    return true;
}

} // namespace sysycc
