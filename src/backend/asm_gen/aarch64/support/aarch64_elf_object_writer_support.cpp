#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_debug_object_writer_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_internal.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_elf_section_serializer_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_object_image_builder_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

std::size_t count_pending_relocations(const std::vector<SectionImage> &sections) {
    std::size_t relocations = 0;
    for (const SectionImage &section : sections) {
        relocations += section.relocations.size();
    }
    return relocations;
}

void append_object_writer_state_note(DiagnosticEngine &diagnostic_engine,
                                     const AArch64MachineModule &machine_module,
                                     const AArch64ObjectModule &object_module,
                                     const std::vector<SectionImage> &sections,
                                     const char *context) {
    diagnostic_engine.add_note(
        DiagnosticStage::Compiler,
        std::string(context) + ": functions=" +
            std::to_string(machine_module.get_functions().size()) +
            ", data_objects=" +
            std::to_string(object_module.get_data_objects().size()) +
            ", symbols=" + std::to_string(object_module.get_symbols().size()) +
            ", sections=" + std::to_string(sections.size()) +
            ", relocations=" + std::to_string(count_pending_relocations(sections)));
}

bool build_text_section_image(
    const AArch64MachineModule &machine_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    DiagnosticEngine &diagnostic_engine) {
    if (machine_module.get_functions().empty()) {
        return true;
    }
    SectionImage &text_section =
        ensure_section_image(sections, AArch64SectionKind::Text);
    text_section.align = std::max<std::uint64_t>(text_section.align, 4);

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        FunctionScanInfo scan_info = scan_function_layout(function, diagnostic_engine);
        const std::size_t function_offset = align_to(text_section.bytes.size(), 4);
        text_section.bytes.resize(function_offset, 0);
        defined_symbols[function.get_name()] =
            DefinedSymbol{AArch64SectionKind::Text, function_offset,
                          scan_info.code_size};
        for (const auto &[label, offset] : scan_info.label_offsets) {
            defined_symbols[label] = DefinedSymbol{AArch64SectionKind::Text,
                                                   function_offset + offset, 0};
        }

        std::size_t local_pc = 0;
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                if (!is_real_text_instruction(instruction)) {
                    continue;
                }
                const auto encoded = encode_machine_instruction(
                    instruction, function, scan_info, local_pc, diagnostic_engine);
                if (!encoded.has_value()) {
                    return false;
                }
                append_pod(text_section.bytes, encoded->word);
                for (AArch64RelocationRecord relocation : encoded->relocations) {
                    relocation.offset += function_offset;
                    text_section.relocations.push_back(
                        PendingRelocation{relocation.offset, std::move(relocation)});
                }
                local_pc += 4;
            }
        }
        scanned_functions.emplace(function.get_name(), std::move(scan_info));
    }
    return true;
}

} // namespace

bool write_aarch64_elf_object(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64ElfObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine) {
    std::vector<SectionImage> sections;
    std::unordered_map<std::string, DefinedSymbol> defined_symbols;
    std::unordered_map<std::string, FunctionScanInfo> scanned_functions;

    if (!build_data_object_section_images(object_module, sections, defined_symbols,
                                          diagnostic_engine) ||
        !build_text_section_image(machine_module, sections, defined_symbols,
                                  scanned_functions, diagnostic_engine) ||
        !build_eh_frame_section_image(machine_module, object_module,
                                      scanned_functions, sections) ||
        !build_debug_line_section_image(machine_module, object_module,
                                        scanned_functions, sections) ||
        !build_debug_info_section_images(object_module, scanned_functions,
                                         sections)) {
        append_object_writer_state_note(
            diagnostic_engine, machine_module, object_module, sections,
            "AArch64 object writer state after section image construction failure");
        return false;
    }

    auto has_section = [&](AArch64SectionKind kind) {
        return std::any_of(sections.begin(), sections.end(),
                           [&](const SectionImage &section) {
                               return section.kind == kind;
                           });
    };
    if (has_section(AArch64SectionKind::DebugLine)) {
        defined_symbols["__sysycc_debug_line"] =
            DefinedSymbol{AArch64SectionKind::DebugLine, 0, 0};
    }
    if (has_section(AArch64SectionKind::DebugAbbrev)) {
        defined_symbols["__sysycc_debug_abbrev"] =
            DefinedSymbol{AArch64SectionKind::DebugAbbrev, 0, 0};
    }
    if (has_section(AArch64SectionKind::DebugStr)) {
        defined_symbols["__sysycc_debug_str"] =
            DefinedSymbol{AArch64SectionKind::DebugStr, 0, 0};
    }

    if (sections.empty()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer received an empty module");
        append_object_writer_state_note(
            diagnostic_engine, machine_module, object_module, sections,
            "AArch64 object writer state for empty module");
        return false;
    }

    std::uint32_t next_section_index = 1;
    for (SectionImage &section : sections) {
        section.section_index = next_section_index++;
    }

    std::vector<SymbolEntry> symbols;
    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    if (!build_full_symbol_entries(object_module, defined_symbols, sections,
                                   options.force_defined_symbols_global, symbols,
                                   symbol_indices)) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to build the native AArch64 object symbol table");
        append_object_writer_state_note(
            diagnostic_engine, machine_module, object_module, sections,
            "AArch64 object writer state after symbol table failure");
        return false;
    }

    return write_aarch64_elf_sectioned_object(
        object_file, sections, std::move(symbols), symbol_indices,
        diagnostic_engine, "AArch64 direct object writer: ");
}

bool write_aarch64_data_only_object(
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64DataOnlyObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine) {
    if (object_module.get_data_objects().empty()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 native data-only object writer received a module without data sections");
        return false;
    }
    return write_aarch64_elf_object(
        AArch64MachineModule{}, object_module, object_file,
        AArch64ElfObjectWriterOptions{
            .force_defined_symbols_global = options.force_defined_symbols_global},
        diagnostic_engine);
}

} // namespace sysycc
