#include "backend/asm_gen/aarch64/support/aarch64_object_image_builder_support.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr std::uint32_t kElfSectionProgBits = 1;
constexpr std::uint32_t kElfSectionNoBits = 8;
constexpr std::uint8_t kElfSymbolBindingLocal = 0;
constexpr std::uint8_t kElfSymbolBindingGlobal = 1;
constexpr std::uint8_t kElfSymbolTypeObject = 1;
constexpr std::uint8_t kElfSymbolTypeFunction = 2;

void merge_symbol_descriptor(AArch64SymbolDescriptor &dst,
                             const AArch64SymbolDescriptor &src) {
    if (dst.kind == AArch64SymbolKind::Object && src.kind != AArch64SymbolKind::Object) {
        dst.kind = src.kind;
    }
    if (!dst.section_kind.has_value() && src.section_kind.has_value()) {
        dst.section_kind = src.section_kind;
    }
    if (dst.binding == AArch64SymbolBinding::Unknown &&
        src.binding != AArch64SymbolBinding::Unknown) {
        dst.binding = src.binding;
    }
    if (!dst.is_defined && src.is_defined) {
        dst.is_defined = true;
    }
}

std::uint8_t elf_symbol_type(AArch64SymbolKind kind) {
    switch (kind) {
    case AArch64SymbolKind::Function:
    case AArch64SymbolKind::Helper:
        return kElfSymbolTypeFunction;
    case AArch64SymbolKind::Label:
        return 0;
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

} // namespace

bool build_data_object_section_images(
    const AArch64ObjectModule &object_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
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
        defined_symbols[data_object.get_symbol_name()] = DefinedSymbol{
            data_object.get_section_kind(), aligned_size, data_object.get_size()};
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            if (!append_fragment_to_section(section, fragment, diagnostic_engine)) {
                return false;
            }
        }
    }
    return true;
}

bool build_full_symbol_entries(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    const std::vector<SectionImage> &sections,
    bool force_defined_symbols_global, std::vector<SymbolEntry> &symbols,
    std::unordered_map<std::string, std::uint32_t> &symbol_indices) {
    std::vector<std::string> needed_symbol_names;
    std::unordered_map<std::string, AArch64SymbolDescriptor> referenced_symbols;
    auto note_symbol_descriptor = [&](const AArch64SymbolDescriptor &descriptor) {
        auto [it, inserted] =
            referenced_symbols.emplace(descriptor.name, descriptor);
        if (!inserted) {
            merge_symbol_descriptor(it->second, descriptor);
        }
    };

    for (const auto &[name, definition] : defined_symbols) {
        (void)definition;
        needed_symbol_names.push_back(name);
    }
    for (const SectionImage &section : sections) {
        for (const PendingRelocation &relocation : section.relocations) {
            note_symbol_descriptor(relocation.record.target.symbol);
            if (std::find(needed_symbol_names.begin(), needed_symbol_names.end(),
                          relocation.record.target.get_name()) ==
                needed_symbol_names.end()) {
                needed_symbol_names.push_back(relocation.record.target.get_name());
            }
        }
    }

    std::vector<SymbolEntry> local_symbols;
    std::vector<SymbolEntry> global_symbols;
    for (const std::string &name : needed_symbol_names) {
        SymbolEntry entry;
        entry.name = name;

        const auto module_symbol_it = object_module.get_symbols().find(name);
        const bool has_module_symbol =
            module_symbol_it != object_module.get_symbols().end();
        const auto referenced_symbol_it = referenced_symbols.find(name);
        const AArch64SymbolKind symbol_kind =
            has_module_symbol ? module_symbol_it->second.get_kind()
                              : (referenced_symbol_it != referenced_symbols.end()
                                     ? referenced_symbol_it->second.kind
                                     : AArch64SymbolKind::Object);
        const AArch64SymbolBinding symbol_binding =
            has_module_symbol
                ? module_symbol_it->second.get_binding()
                : (referenced_symbol_it != referenced_symbols.end()
                       ? referenced_symbol_it->second.binding
                       : AArch64SymbolBinding::Unknown);
        entry.type = elf_symbol_type(symbol_kind);

        const auto definition_it = defined_symbols.find(name);
        if (definition_it != defined_symbols.end()) {
            const DefinedSymbol &definition = definition_it->second;
            const auto section_it = std::find_if(
                sections.begin(), sections.end(), [&](const SectionImage &section) {
                    return section.kind == definition.section_kind;
                });
            if (section_it == sections.end()) {
                return false;
            }
            entry.section_index = section_it->section_index;
            entry.value = definition.offset;
            entry.size = definition.size;
            entry.binding = (force_defined_symbols_global ||
                             symbol_binding == AArch64SymbolBinding::Global)
                                ? kElfSymbolBindingGlobal
                                : kElfSymbolBindingLocal;
        } else {
            entry.binding =
                symbol_binding == AArch64SymbolBinding::Local
                    ? kElfSymbolBindingLocal
                    : kElfSymbolBindingGlobal;
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

} // namespace sysycc
