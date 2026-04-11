#include "backend/asm_gen/aarch64/support/aarch64_debug_object_writer_support.hpp"

namespace sysycc {

namespace {

void append_dwarf_advance(std::vector<std::uint8_t> &bytes,
                          std::size_t byte_delta) {
    const std::size_t units = byte_delta / 4;
    if (units == 0) {
        return;
    }
    if (units < 0x40) {
        bytes.push_back(static_cast<std::uint8_t>(0x40U | units));
        return;
    }
    if (units <= 0xffU) {
        bytes.push_back(0x02U);
        bytes.push_back(static_cast<std::uint8_t>(units));
        return;
    }
    if (units <= 0xffffU) {
        bytes.push_back(0x03U);
        std::uint16_t value = static_cast<std::uint16_t>(units);
        append_pod(bytes, value);
        return;
    }
    bytes.push_back(0x04U);
    std::uint32_t value = static_cast<std::uint32_t>(units);
    append_pod(bytes, value);
}

void append_cfi_note_bytes(std::vector<std::uint8_t> &bytes,
                           const ParsedCfiNote &note) {
    switch (note.kind) {
    case ParsedCfiNoteKind::DefCfa:
        bytes.push_back(0x0cU);
        append_uleb128(bytes, note.reg);
        append_uleb128(bytes, static_cast<std::uint64_t>(note.offset));
        return;
    case ParsedCfiNoteKind::DefCfaRegister:
        bytes.push_back(0x0dU);
        append_uleb128(bytes, note.reg);
        return;
    case ParsedCfiNoteKind::DefCfaOffset:
        bytes.push_back(0x0eU);
        append_uleb128(bytes, static_cast<std::uint64_t>(note.offset));
        return;
    case ParsedCfiNoteKind::Offset: {
        const std::uint64_t scaled = static_cast<std::uint64_t>((-note.offset) / 8);
        if (note.reg < 64) {
            bytes.push_back(static_cast<std::uint8_t>(0x80U | note.reg));
        } else {
            bytes.push_back(0x05U);
            append_uleb128(bytes, note.reg);
        }
        append_uleb128(bytes, scaled);
        return;
    }
    case ParsedCfiNoteKind::Restore:
        if (note.reg < 64) {
            bytes.push_back(static_cast<std::uint8_t>(0xc0U | note.reg));
        } else {
            bytes.push_back(0x06U);
            append_uleb128(bytes, note.reg);
        }
        return;
    }
}

void append_debug_line_set_address(SectionImage &section,
                                   const AArch64ObjectModule &object_module,
                                   const std::string &function_name,
                                   long long addend) {
    section.bytes.push_back(0);
    append_uleb128(section.bytes, 1 + 8);
    section.bytes.push_back(2);
    const std::size_t reloc_offset = section.bytes.size();
    append_pod(section.bytes, std::uint64_t{0});
    section.relocations.push_back(PendingRelocation{
        reloc_offset,
        AArch64RelocationRecord{
            AArch64RelocationKind::Absolute64,
            object_module.make_symbol_reference(
                function_name, AArch64SymbolKind::Function,
                AArch64SymbolBinding::Unknown, AArch64SectionKind::Text,
                addend, true),
            reloc_offset}});
}

} // namespace

bool build_eh_frame_section_image(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections) {
    if (machine_module.get_functions().empty()) {
        return true;
    }
    SectionImage &section =
        ensure_section_image(sections, AArch64SectionKind::EhFrame);
    section.align = std::max<std::uint64_t>(section.align, 8);

    const std::size_t cie_start = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    append_pod(section.bytes, std::uint32_t{0});
    section.bytes.push_back(1);
    section.bytes.push_back('z');
    section.bytes.push_back('R');
    section.bytes.push_back('\0');
    append_uleb128(section.bytes, 4);
    append_sleb128(section.bytes, -8);
    append_uleb128(section.bytes, 30);
    append_uleb128(section.bytes, 1);
    section.bytes.push_back(0x1bU);
    section.bytes.push_back(0x0cU);
    append_uleb128(section.bytes, 31);
    append_uleb128(section.bytes, 0);
    section.bytes.resize(align_to(section.bytes.size(), 4), 0);
    overwrite_u32(section.bytes, cie_start,
                  static_cast<std::uint32_t>(section.bytes.size() - cie_start - 4));

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        const auto scan_it = scanned_functions.find(function.get_name());
        if (scan_it == scanned_functions.end()) {
            continue;
        }
        const FunctionScanInfo &scan = scan_it->second;
        const std::size_t fde_start = section.bytes.size();
        append_pod(section.bytes, std::uint32_t{0});
        append_pod(section.bytes,
                   static_cast<std::uint32_t>((fde_start + 4) - cie_start));
        const std::size_t reloc_offset = section.bytes.size();
        append_pod(section.bytes, std::uint32_t{0});
        section.relocations.push_back(PendingRelocation{
            reloc_offset,
            AArch64RelocationRecord{
                AArch64RelocationKind::Prel32,
                object_module.make_symbol_reference(
                    function.get_name(), AArch64SymbolKind::Function,
                    AArch64SymbolBinding::Unknown, AArch64SectionKind::Text,
                    0, true),
                reloc_offset}});
        append_pod(section.bytes, static_cast<std::uint32_t>(scan.code_size));
        append_uleb128(section.bytes, 0);

        std::size_t current_pc = 0;
        for (const AArch64CfiDirective &directive :
             function.get_frame_record().get_cfi_directives()) {
            if (directive.kind == AArch64CfiDirectiveKind::StartProcedure ||
                directive.kind == AArch64CfiDirectiveKind::EndProcedure) {
                continue;
            }
            append_dwarf_advance(section.bytes,
                                 directive.code_offset - current_pc);
            current_pc = directive.code_offset;
            switch (directive.kind) {
            case AArch64CfiDirectiveKind::DefCfa:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::DefCfa,
                                                    .reg = directive.reg,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::DefCfaRegister:
                append_cfi_note_bytes(
                    section.bytes,
                    ParsedCfiNote{
                        .pc_offset = directive.code_offset,
                        .kind = ParsedCfiNoteKind::DefCfaRegister,
                        .reg = directive.reg});
                break;
            case AArch64CfiDirectiveKind::DefCfaOffset:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::DefCfaOffset,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::Offset:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::Offset,
                                                    .reg = directive.reg,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::Restore:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::Restore,
                                                    .reg = directive.reg});
                break;
            case AArch64CfiDirectiveKind::StartProcedure:
            case AArch64CfiDirectiveKind::EndProcedure:
                break;
            }
        }
        section.bytes.resize(align_to(section.bytes.size(), 4), 0);
        overwrite_u32(section.bytes, fde_start,
                      static_cast<std::uint32_t>(section.bytes.size() - fde_start -
                                                 4));
    }

    return true;
}

bool build_debug_line_section_image(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections) {
    if (object_module.get_debug_file_entries().empty()) {
        return true;
    }

    bool has_locations = false;
    for (const auto &[name, scan] : scanned_functions) {
        (void)name;
        if (!scan.source_locations.empty()) {
            has_locations = true;
            break;
        }
    }
    if (!has_locations) {
        return true;
    }

    SectionImage &section =
        ensure_section_image(sections, AArch64SectionKind::DebugLine);
    section.align = 1;

    const std::size_t unit_length_offset = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    append_pod(section.bytes, std::uint16_t{2});
    const std::size_t header_length_offset = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    const std::size_t header_start = section.bytes.size();
    section.bytes.push_back(4);
    section.bytes.push_back(1);
    section.bytes.push_back(static_cast<std::uint8_t>(-5));
    section.bytes.push_back(14);
    section.bytes.push_back(10);
    const std::uint8_t standard_opcode_lengths[] = {0, 1, 1, 1, 1, 0, 0, 0, 1};
    section.bytes.insert(section.bytes.end(), std::begin(standard_opcode_lengths),
                         std::end(standard_opcode_lengths));
    section.bytes.push_back('\0');
    for (const AArch64DebugFileEntry &entry : object_module.get_debug_file_entries()) {
        section.bytes.insert(section.bytes.end(), entry.path.begin(), entry.path.end());
        section.bytes.push_back('\0');
        append_uleb128(section.bytes, 0);
        append_uleb128(section.bytes, 0);
        append_uleb128(section.bytes, 0);
    }
    section.bytes.push_back('\0');
    overwrite_u32(section.bytes, header_length_offset,
                  static_cast<std::uint32_t>(section.bytes.size() - header_start));

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        const auto scan_it = scanned_functions.find(function.get_name());
        if (scan_it == scanned_functions.end() ||
            scan_it->second.source_locations.empty()) {
            continue;
        }
        const FunctionScanInfo &scan = scan_it->second;
        const SourceLocationNote &first = scan.source_locations.front();
        append_debug_line_set_address(section, object_module, function.get_name(),
                                      static_cast<long long>(first.pc_offset));

        unsigned current_file = 1;
        int current_line = 1;
        int current_column = 0;
        std::size_t current_pc = first.pc_offset;

        auto append_note = [&](const SourceLocationNote &note, bool is_first) {
            if (!is_first && note.pc_offset > current_pc) {
                section.bytes.push_back(2);
                append_uleb128(section.bytes,
                               static_cast<std::uint64_t>((note.pc_offset - current_pc) /
                                                          4));
            }
            if (note.file_id != current_file) {
                section.bytes.push_back(4);
                append_uleb128(section.bytes, note.file_id);
            }
            if (note.column != current_column) {
                section.bytes.push_back(5);
                append_uleb128(section.bytes, static_cast<std::uint64_t>(note.column));
            }
            if (note.line != current_line) {
                section.bytes.push_back(3);
                append_sleb128(section.bytes,
                               static_cast<std::int64_t>(note.line - current_line));
            }
            section.bytes.push_back(1);
            current_file = note.file_id;
            current_line = note.line;
            current_column = note.column;
            current_pc = note.pc_offset;
        };

        append_note(first, true);
        for (std::size_t index = 1; index < scan.source_locations.size(); ++index) {
            append_note(scan.source_locations[index], false);
        }
        if (scan.code_size > current_pc) {
            section.bytes.push_back(2);
            append_uleb128(section.bytes,
                           static_cast<std::uint64_t>((scan.code_size - current_pc) /
                                                      4));
        }
        section.bytes.push_back(0);
        append_uleb128(section.bytes, 1);
        section.bytes.push_back(1);
    }

    overwrite_u32(section.bytes, unit_length_offset,
                  static_cast<std::uint32_t>(section.bytes.size() -
                                             unit_length_offset - 4));
    return true;
}

} // namespace sysycc
