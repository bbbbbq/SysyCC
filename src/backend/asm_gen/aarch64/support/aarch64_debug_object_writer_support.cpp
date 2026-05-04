#include "backend/asm_gen/aarch64/support/aarch64_debug_object_writer_support.hpp"

#include <algorithm>
#include <unordered_map>

namespace sysycc {

namespace {

constexpr std::uint16_t kDwarfLanguageC99 = 0x000c;

constexpr std::uint64_t kDwTagCompileUnit = 0x11;
constexpr std::uint64_t kDwTagSubprogram = 0x2e;
constexpr std::uint64_t kDwTagFormalParameter = 0x05;
constexpr std::uint64_t kDwTagVariable = 0x34;
constexpr std::uint64_t kDwTagBaseType = 0x24;
constexpr std::uint64_t kDwTagPointerType = 0x0f;
constexpr std::uint64_t kDwTagArrayType = 0x01;
constexpr std::uint64_t kDwTagSubrangeType = 0x21;
constexpr std::uint64_t kDwTagStructureType = 0x13;
constexpr std::uint64_t kDwTagMember = 0x0d;
constexpr std::uint64_t kDwTagUnspecifiedType = 0x3b;

constexpr std::uint64_t kDwChildrenNo = 0x00;
constexpr std::uint64_t kDwChildrenYes = 0x01;

constexpr std::uint64_t kDwAtName = 0x03;
constexpr std::uint64_t kDwAtLocation = 0x02;
constexpr std::uint64_t kDwAtByteSize = 0x0b;
constexpr std::uint64_t kDwAtStmtList = 0x10;
constexpr std::uint64_t kDwAtLowPc = 0x11;
constexpr std::uint64_t kDwAtHighPc = 0x12;
constexpr std::uint64_t kDwAtLanguage = 0x13;
constexpr std::uint64_t kDwAtCompDir = 0x1b;
constexpr std::uint64_t kDwAtProducer = 0x25;
constexpr std::uint64_t kDwAtCount = 0x37;
constexpr std::uint64_t kDwAtDataMemberLocation = 0x38;
constexpr std::uint64_t kDwAtDeclColumn = 0x39;
constexpr std::uint64_t kDwAtDeclFile = 0x3a;
constexpr std::uint64_t kDwAtDeclLine = 0x3b;
constexpr std::uint64_t kDwAtEncoding = 0x3e;
constexpr std::uint64_t kDwAtExternal = 0x3f;
constexpr std::uint64_t kDwAtFrameBase = 0x40;
constexpr std::uint64_t kDwAtType = 0x49;

constexpr std::uint64_t kDwFormAddr = 0x01;
constexpr std::uint64_t kDwFormData2 = 0x05;
constexpr std::uint64_t kDwFormData4 = 0x06;
constexpr std::uint64_t kDwFormData1 = 0x0b;
constexpr std::uint64_t kDwFormStrp = 0x0e;
constexpr std::uint64_t kDwFormRef4 = 0x13;
constexpr std::uint64_t kDwFormSecOffset = 0x17;
constexpr std::uint64_t kDwFormExprloc = 0x18;
constexpr std::uint64_t kDwFormFlagPresent = 0x19;

constexpr std::uint8_t kDwOpReg29 = 0x50 + 29;
constexpr std::uint8_t kDwOpFbreg = 0x91;

constexpr const char *kDebugAbbrevSectionSymbol = "__sysycc_debug_abbrev";
constexpr const char *kDebugLineSectionSymbol = "__sysycc_debug_line";
constexpr const char *kDebugStrSectionSymbol = "__sysycc_debug_str";

enum DwarfAbbrevCode : std::uint8_t {
    kAbbrevCompileUnit = 1,
    kAbbrevSubprogramWithType = 2,
    kAbbrevFormalParameter = 3,
    kAbbrevVariable = 4,
    kAbbrevBaseType = 5,
    kAbbrevPointerType = 6,
    kAbbrevStructureType = 7,
    kAbbrevUnspecifiedType = 8,
    kAbbrevArrayType = 9,
    kAbbrevSubrangeType = 10,
    kAbbrevMember = 11,
};

std::uint32_t append_debug_string(std::vector<std::uint8_t> &debug_str,
                                  const std::string &text) {
    const std::uint32_t offset = static_cast<std::uint32_t>(debug_str.size());
    debug_str.insert(debug_str.end(), text.begin(), text.end());
    debug_str.push_back('\0');
    return offset;
}

std::string basename_for_debug(const std::string &path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dirname_for_debug(const std::string &path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

void append_abbrev_attr(std::vector<std::uint8_t> &bytes, std::uint64_t attr,
                        std::uint64_t form) {
    append_uleb128(bytes, attr);
    append_uleb128(bytes, form);
}

void append_abbrev_entry(std::vector<std::uint8_t> &bytes, std::uint64_t code,
                         std::uint64_t tag, std::uint64_t children,
                         const std::vector<std::pair<std::uint64_t, std::uint64_t>>
                             &attributes) {
    append_uleb128(bytes, code);
    append_uleb128(bytes, tag);
    bytes.push_back(static_cast<std::uint8_t>(children));
    for (const auto &[attr, form] : attributes) {
        append_abbrev_attr(bytes, attr, form);
    }
    append_abbrev_attr(bytes, 0, 0);
}

void build_debug_abbrev_section_image(std::vector<SectionImage> &sections) {
    SectionImage &section =
        ensure_section_image(sections, AArch64SectionKind::DebugAbbrev);
    section.align = 1;

    append_abbrev_entry(section.bytes, kAbbrevCompileUnit, kDwTagCompileUnit,
                        kDwChildrenYes,
                        {{kDwAtProducer, kDwFormStrp},
                         {kDwAtLanguage, kDwFormData2},
                         {kDwAtName, kDwFormStrp},
                         {kDwAtCompDir, kDwFormStrp},
                         {kDwAtStmtList, kDwFormSecOffset}});
    append_abbrev_entry(section.bytes, kAbbrevSubprogramWithType, kDwTagSubprogram,
                        kDwChildrenYes,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtLowPc, kDwFormAddr},
                         {kDwAtHighPc, kDwFormData4},
                         {kDwAtFrameBase, kDwFormExprloc},
                         {kDwAtExternal, kDwFormFlagPresent},
                         {kDwAtType, kDwFormRef4}});
    append_abbrev_entry(section.bytes, kAbbrevFormalParameter,
                        kDwTagFormalParameter, kDwChildrenNo,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtDeclFile, kDwFormData4},
                         {kDwAtDeclLine, kDwFormData4},
                         {kDwAtDeclColumn, kDwFormData4},
                         {kDwAtType, kDwFormRef4},
                         {kDwAtLocation, kDwFormExprloc}});
    append_abbrev_entry(section.bytes, kAbbrevVariable, kDwTagVariable,
                        kDwChildrenNo,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtDeclFile, kDwFormData4},
                         {kDwAtDeclLine, kDwFormData4},
                         {kDwAtDeclColumn, kDwFormData4},
                         {kDwAtType, kDwFormRef4},
                         {kDwAtLocation, kDwFormExprloc}});
    append_abbrev_entry(section.bytes, kAbbrevBaseType, kDwTagBaseType,
                        kDwChildrenNo,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtByteSize, kDwFormData1},
                         {kDwAtEncoding, kDwFormData1}});
    append_abbrev_entry(section.bytes, kAbbrevPointerType, kDwTagPointerType,
                        kDwChildrenNo,
                        {{kDwAtByteSize, kDwFormData1},
                         {kDwAtType, kDwFormRef4}});
    append_abbrev_entry(section.bytes, kAbbrevArrayType, kDwTagArrayType,
                        kDwChildrenYes, {{kDwAtType, kDwFormRef4}});
    append_abbrev_entry(section.bytes, kAbbrevSubrangeType, kDwTagSubrangeType,
                        kDwChildrenNo, {{kDwAtCount, kDwFormData4}});
    append_abbrev_entry(section.bytes, kAbbrevStructureType, kDwTagStructureType,
                        kDwChildrenYes,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtByteSize, kDwFormData4}});
    append_abbrev_entry(section.bytes, kAbbrevMember, kDwTagMember,
                        kDwChildrenNo,
                        {{kDwAtName, kDwFormStrp},
                         {kDwAtType, kDwFormRef4},
                         {kDwAtDataMemberLocation, kDwFormData4}});
    append_abbrev_entry(section.bytes, kAbbrevUnspecifiedType,
                        kDwTagUnspecifiedType, kDwChildrenNo,
                        {{kDwAtName, kDwFormStrp}});
    append_uleb128(section.bytes, 0);
}

void append_debug_strp(SectionImage &debug_info,
                       std::vector<std::uint8_t> &debug_str,
                       const AArch64ObjectModule &object_module,
                       const std::string &text) {
    const std::uint32_t string_offset = append_debug_string(debug_str, text);
    const std::size_t reloc_offset = debug_info.bytes.size();
    append_pod(debug_info.bytes, string_offset);
    debug_info.relocations.push_back(PendingRelocation{
        reloc_offset,
        AArch64RelocationRecord{
            AArch64RelocationKind::Absolute32,
            object_module.make_symbol_reference(
                kDebugStrSectionSymbol, AArch64SymbolKind::Object,
                AArch64SymbolBinding::Local, AArch64SectionKind::DebugStr,
                string_offset, true),
            reloc_offset}});
}

void append_debug_line_sec_offset(SectionImage &debug_info,
                                  const AArch64ObjectModule &object_module) {
    const std::size_t reloc_offset = debug_info.bytes.size();
    append_pod(debug_info.bytes, std::uint32_t{0});
    debug_info.relocations.push_back(PendingRelocation{
        reloc_offset,
        AArch64RelocationRecord{
            AArch64RelocationKind::Absolute32,
            object_module.make_symbol_reference(
                kDebugLineSectionSymbol, AArch64SymbolKind::Object,
                AArch64SymbolBinding::Local, AArch64SectionKind::DebugLine,
                0, true),
            reloc_offset}});
}

void append_debug_abbrev_sec_offset(SectionImage &debug_info,
                                    const AArch64ObjectModule &object_module) {
    const std::size_t reloc_offset = debug_info.bytes.size();
    append_pod(debug_info.bytes, std::uint32_t{0});
    debug_info.relocations.push_back(PendingRelocation{
        reloc_offset,
        AArch64RelocationRecord{
            AArch64RelocationKind::Absolute32,
            object_module.make_symbol_reference(
                kDebugAbbrevSectionSymbol, AArch64SymbolKind::Object,
                AArch64SymbolBinding::Local, AArch64SectionKind::DebugAbbrev,
                0, true),
            reloc_offset}});
}

void append_debug_exprloc_fbreg(std::vector<std::uint8_t> &bytes,
                                long long frame_offset) {
    std::vector<std::uint8_t> expr;
    expr.push_back(kDwOpFbreg);
    append_sleb128(expr, frame_offset);
    append_uleb128(bytes, expr.size());
    bytes.insert(bytes.end(), expr.begin(), expr.end());
}

void append_debug_exprloc_reg29(std::vector<std::uint8_t> &bytes) {
    append_uleb128(bytes, 1);
    bytes.push_back(kDwOpReg29);
}

void append_debug_exprloc_register(std::vector<std::uint8_t> &bytes,
                                   unsigned dwarf_register) {
    std::vector<std::uint8_t> expr;
    if (dwarf_register <= 31) {
        expr.push_back(static_cast<std::uint8_t>(0x50U + dwarf_register));
    } else {
        expr.push_back(0x90U);
        append_uleb128(expr, dwarf_register);
    }
    append_uleb128(bytes, expr.size());
    bytes.insert(bytes.end(), expr.begin(), expr.end());
}

std::uint32_t type_ref_offset(
    const std::string &type_key,
    const std::unordered_map<std::string, std::uint32_t> &type_offsets) {
    const auto it = type_offsets.find(type_key);
    return it == type_offsets.end() ? 0 : it->second;
}

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

bool build_debug_info_section_images(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections) {
    if (object_module.get_debug_functions().empty() ||
        object_module.get_debug_file_entries().empty()) {
        return true;
    }

    build_debug_abbrev_section_image(sections);

    SectionImage &debug_str =
        ensure_section_image(sections, AArch64SectionKind::DebugStr);
    debug_str.align = 1;
    if (debug_str.bytes.empty()) {
        debug_str.bytes.push_back('\0');
    }

    SectionImage &debug_info =
        ensure_section_image(sections, AArch64SectionKind::DebugInfo);
    debug_info.align = 1;

    const std::string primary_path =
        object_module.get_debug_file_entries().front().path;
    const std::size_t unit_start = debug_info.bytes.size();
    append_pod(debug_info.bytes, std::uint32_t{0});
    append_pod(debug_info.bytes, std::uint16_t{4});
    append_debug_abbrev_sec_offset(debug_info, object_module);
    debug_info.bytes.push_back(8);

    append_uleb128(debug_info.bytes, kAbbrevCompileUnit);
    append_debug_strp(debug_info, debug_str.bytes, object_module,
                      "SysyCC AArch64 native backend");
    append_pod(debug_info.bytes, kDwarfLanguageC99);
    append_debug_strp(debug_info, debug_str.bytes, object_module,
                      basename_for_debug(primary_path));
    append_debug_strp(debug_info, debug_str.bytes, object_module,
                      dirname_for_debug(primary_path));
    append_debug_line_sec_offset(debug_info, object_module);

    std::unordered_map<std::string, std::uint32_t> type_offsets;
    for (const AArch64DebugTypeInfo &type : object_module.get_debug_types()) {
        if (type.key.empty()) {
            continue;
        }
        type_offsets[type.key] =
            static_cast<std::uint32_t>(debug_info.bytes.size() - unit_start);
        switch (type.kind) {
        case AArch64DebugTypeKind::Base:
            append_uleb128(debug_info.bytes, kAbbrevBaseType);
            append_debug_strp(debug_info, debug_str.bytes, object_module,
                              type.name);
            debug_info.bytes.push_back(static_cast<std::uint8_t>(type.byte_size));
            debug_info.bytes.push_back(static_cast<std::uint8_t>(type.encoding));
            break;
        case AArch64DebugTypeKind::Pointer:
            append_uleb128(debug_info.bytes, kAbbrevPointerType);
            debug_info.bytes.push_back(static_cast<std::uint8_t>(type.byte_size));
            append_pod(debug_info.bytes,
                       type_ref_offset(type.referenced_type_key, type_offsets));
            break;
        case AArch64DebugTypeKind::Array:
            append_uleb128(debug_info.bytes, kAbbrevArrayType);
            append_pod(debug_info.bytes,
                       type_ref_offset(type.referenced_type_key, type_offsets));
            append_uleb128(debug_info.bytes, kAbbrevSubrangeType);
            append_pod(debug_info.bytes,
                       static_cast<std::uint32_t>(type.element_count));
            append_uleb128(debug_info.bytes, 0);
            break;
        case AArch64DebugTypeKind::Structure:
            append_uleb128(debug_info.bytes, kAbbrevStructureType);
            append_debug_strp(debug_info, debug_str.bytes, object_module,
                              type.name);
            append_pod(debug_info.bytes,
                       static_cast<std::uint32_t>(type.byte_size));
            for (const AArch64DebugMemberInfo &member : type.members) {
                append_uleb128(debug_info.bytes, kAbbrevMember);
                append_debug_strp(debug_info, debug_str.bytes, object_module,
                                  member.name);
                append_pod(debug_info.bytes,
                           type_ref_offset(member.type_key, type_offsets));
                append_pod(debug_info.bytes,
                           static_cast<std::uint32_t>(member.offset));
            }
            append_uleb128(debug_info.bytes, 0);
            break;
        case AArch64DebugTypeKind::Unspecified:
            append_uleb128(debug_info.bytes, kAbbrevUnspecifiedType);
            append_debug_strp(debug_info, debug_str.bytes, object_module,
                              type.name);
            break;
        }
    }

    auto append_variable_die = [&](const AArch64DebugVariableInfo &variable,
                                   std::uint8_t abbrev_code) {
        if (variable.name.empty() ||
            (!variable.has_frame_offset && !variable.has_dwarf_register)) {
            return;
        }
        append_uleb128(debug_info.bytes, abbrev_code);
        append_debug_strp(debug_info, debug_str.bytes, object_module,
                          variable.name);
        append_pod(debug_info.bytes, variable.decl_file_id);
        append_pod(debug_info.bytes, variable.decl_line);
        append_pod(debug_info.bytes, variable.decl_column);
        append_pod(debug_info.bytes,
                   type_ref_offset(variable.type_key, type_offsets));
        if (variable.has_frame_offset) {
            append_debug_exprloc_fbreg(debug_info.bytes, variable.frame_offset);
        } else {
            append_debug_exprloc_register(debug_info.bytes,
                                          variable.dwarf_register);
        }
    };

    for (const AArch64DebugFunctionInfo &function :
         object_module.get_debug_functions()) {
        const auto scan_it = scanned_functions.find(function.name);
        if (scan_it == scanned_functions.end()) {
            continue;
        }
        append_uleb128(debug_info.bytes, kAbbrevSubprogramWithType);
        append_debug_strp(debug_info, debug_str.bytes, object_module,
                          function.name);
        const std::size_t reloc_offset = debug_info.bytes.size();
        append_pod(debug_info.bytes, std::uint64_t{0});
        debug_info.relocations.push_back(PendingRelocation{
            reloc_offset,
            AArch64RelocationRecord{
                AArch64RelocationKind::Absolute64,
                object_module.make_symbol_reference(
                    function.name, AArch64SymbolKind::Function,
                    AArch64SymbolBinding::Unknown, AArch64SectionKind::Text,
                    0, true),
                reloc_offset}});
        append_pod(debug_info.bytes,
                   static_cast<std::uint32_t>(scan_it->second.code_size));
        append_debug_exprloc_reg29(debug_info.bytes);
        append_pod(debug_info.bytes,
                   type_ref_offset(function.return_type_key, type_offsets));

        for (const AArch64DebugVariableInfo &parameter : function.parameters) {
            append_variable_die(parameter, kAbbrevFormalParameter);
        }
        for (const AArch64DebugVariableInfo &local_variable :
             function.local_variables) {
            append_variable_die(local_variable, kAbbrevVariable);
        }
        append_uleb128(debug_info.bytes, 0);
    }
    append_uleb128(debug_info.bytes, 0);

    overwrite_u32(debug_info.bytes, unit_start,
                  static_cast<std::uint32_t>(debug_info.bytes.size() -
                                             unit_start - 4));
    return true;
}

} // namespace sysycc
