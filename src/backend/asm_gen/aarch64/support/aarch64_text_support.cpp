#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

#include <cctype>
#include <unordered_set>

namespace sysycc {

bool is_float_physical_reg(unsigned reg) {
    return reg >= static_cast<unsigned>(AArch64PhysicalReg::V0) &&
           reg <= static_cast<unsigned>(AArch64PhysicalReg::V31);
}

unsigned dwarf_register_number(unsigned physical_reg) {
    if (is_float_physical_reg(physical_reg)) {
        return 64 + (physical_reg - static_cast<unsigned>(AArch64PhysicalReg::V0));
    }
    return physical_reg;
}

const char *section_name(AArch64SectionKind section_kind) {
    switch (section_kind) {
    case AArch64SectionKind::Text:
        return ".text";
    case AArch64SectionKind::Data:
        return ".data";
    case AArch64SectionKind::ReadOnlyData:
        return ".section .rodata";
    case AArch64SectionKind::Bss:
        return ".bss";
    case AArch64SectionKind::EhFrame:
        return ".section .eh_frame";
    case AArch64SectionKind::DebugFrame:
        return ".section .debug_frame";
    case AArch64SectionKind::DebugLine:
        return ".section .debug_line";
    }
    return ".text";
}

std::string quote_asm_string(const std::string &text) {
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');
    for (char ch : text) {
        switch (ch) {
        case '\\':
        case '"':
            quoted.push_back('\\');
            quoted.push_back(ch);
            break;
        case '\n':
            quoted += "\\n";
            break;
        case '\t':
            quoted += "\\t";
            break;
        default:
            quoted.push_back(ch);
            break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

bool uses_general_64bit_register(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::General64;
}

char virtual_reg_suffix(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::General32:
        return 'w';
    case AArch64VirtualRegKind::General64:
        return 'x';
    case AArch64VirtualRegKind::Float16:
        return 'h';
    case AArch64VirtualRegKind::Float32:
        return 's';
    case AArch64VirtualRegKind::Float64:
        return 'd';
    case AArch64VirtualRegKind::Float128:
        return 'q';
    }
    return 'w';
}

AArch64VirtualRegKind virtual_reg_kind_from_suffix(char suffix) {
    switch (suffix) {
    case 'x':
        return AArch64VirtualRegKind::General64;
    case 'h':
        return AArch64VirtualRegKind::Float16;
    case 's':
        return AArch64VirtualRegKind::Float32;
    case 'd':
        return AArch64VirtualRegKind::Float64;
    case 'q':
        return AArch64VirtualRegKind::Float128;
    case 'w':
    default:
        return AArch64VirtualRegKind::General32;
    }
}

std::pair<std::string, std::vector<AArch64MachineOperand>>
parse_machine_instruction_text(std::string text) {
    const std::size_t separator = text.find(' ');
    if (separator == std::string::npos) {
        return {std::move(text), {}};
    }

    std::string mnemonic = text.substr(0, separator);
    std::vector<AArch64MachineOperand> operands;
    const std::string operand_text = text.substr(separator + 1);
    std::string current_operand;
    int bracket_depth = 0;
    for (std::size_t index = 0; index < operand_text.size(); ++index) {
        const char ch = operand_text[index];
        if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']') {
            --bracket_depth;
        }
        if (ch == ',' && bracket_depth == 0 &&
            index + 1 < operand_text.size() && operand_text[index + 1] == ' ') {
            operands.emplace_back(std::move(current_operand));
            current_operand.clear();
            ++index;
            continue;
        }
        current_operand.push_back(ch);
    }
    if (!current_operand.empty()) {
        operands.emplace_back(std::move(current_operand));
    }
    return {std::move(mnemonic), std::move(operands)};
}

std::string render_physical_register(unsigned reg_number, AArch64VirtualRegKind kind) {
    if (reg_number >= static_cast<unsigned>(AArch64PhysicalReg::V0) &&
        reg_number <= static_cast<unsigned>(AArch64PhysicalReg::V31)) {
        return std::string(1, virtual_reg_suffix(kind)) + std::to_string(
                   reg_number - static_cast<unsigned>(AArch64PhysicalReg::V0));
    }
    return std::string(uses_general_64bit_register(kind) ? "x" : "w") +
           std::to_string(reg_number);
}

std::string render_physical_register(unsigned reg_number, bool use_64bit) {
    return render_physical_register(
        reg_number, use_64bit ? AArch64VirtualRegKind::General64
                              : AArch64VirtualRegKind::General32);
}

std::string zero_register_name(bool use_64bit) {
    return use_64bit ? "xzr" : "wzr";
}

namespace {

std::string vreg_token(char role, const AArch64VirtualReg &reg) {
    return "%" + std::string(1, role) + std::to_string(reg.get_id()) +
           virtual_reg_suffix(reg.get_kind());
}

std::string vreg_token_with_kind(char role, const AArch64VirtualReg &reg,
                                 AArch64VirtualRegKind kind) {
    return "%" + std::string(1, role) + std::to_string(reg.get_id()) +
           virtual_reg_suffix(kind);
}

} // namespace

std::string use_vreg(const AArch64VirtualReg &reg) { return vreg_token('u', reg); }

std::string def_vreg(const AArch64VirtualReg &reg) { return vreg_token('d', reg); }

std::string use_vreg_as(const AArch64VirtualReg &reg, bool use_64bit) {
    return vreg_token_with_kind(
        'u', reg,
        use_64bit ? AArch64VirtualRegKind::General64
                  : AArch64VirtualRegKind::General32);
}

std::string def_vreg_as(const AArch64VirtualReg &reg, bool use_64bit) {
    return vreg_token_with_kind(
        'd', reg,
        use_64bit ? AArch64VirtualRegKind::General64
                  : AArch64VirtualRegKind::General32);
}

std::string use_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind) {
    return vreg_token_with_kind('u', reg, kind);
}

std::string def_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind) {
    return vreg_token_with_kind('d', reg, kind);
}

std::string fp_move_mnemonic(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::Float128 ? "mov" : "fmov";
}

void append_register_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &dst_reg,
                          const AArch64VirtualReg &src_reg) {
    if (dst_reg.get_id() == src_reg.get_id() &&
        dst_reg.get_kind() == src_reg.get_kind()) {
        return;
    }
    if (dst_reg.is_floating_point() && src_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(dst_reg.get_kind()) + " " +
                                         def_vreg(dst_reg) + ", " +
                                         use_vreg_as_kind(src_reg, dst_reg.get_kind()));
        return;
    }
    machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                     use_vreg_as_kind(src_reg, dst_reg.get_kind()));
}

void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &dst_reg,
                                   unsigned physical_reg,
                                   AArch64VirtualRegKind physical_kind) {
    const std::string physical_name =
        render_physical_register(physical_reg, physical_kind);
    if (dst_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(dst_reg.get_kind()) + " " +
                                         def_vreg(dst_reg) + ", " + physical_name);
        return;
    }
    machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                     physical_name);
}

void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                 unsigned physical_reg,
                                 AArch64VirtualRegKind physical_kind,
                                 const AArch64VirtualReg &src_reg) {
    const std::string physical_name =
        render_physical_register(physical_reg, physical_kind);
    if (src_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(src_reg.get_kind()) + " " +
                                         physical_name + ", " + use_vreg(src_reg));
        return;
    }
    machine_block.append_instruction("mov " + physical_name + ", " +
                                     use_vreg_as_kind(
                                         src_reg,
                                         uses_general_64bit_register(physical_kind)
                                             ? AArch64VirtualRegKind::General64
                                             : AArch64VirtualRegKind::General32));
}

std::vector<ParsedVirtualRegRef> parse_virtual_reg_refs(const std::string &text) {
    std::vector<ParsedVirtualRegRef> refs;
    for (std::size_t index = 0; index + 3 < text.size(); ++index) {
        if (text[index] != '%' || (text[index + 1] != 'u' && text[index + 1] != 'd')) {
            continue;
        }
        std::size_t cursor = index + 2;
        if (!std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            continue;
        }
        std::size_t id = 0;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            id = (id * 10) + static_cast<std::size_t>(text[cursor] - '0');
            ++cursor;
        }
        if (cursor >= text.size() ||
            (text[cursor] != 'x' && text[cursor] != 'w' && text[cursor] != 'h' &&
             text[cursor] != 's' && text[cursor] != 'd' && text[cursor] != 'q')) {
            continue;
        }
        refs.push_back({id, virtual_reg_kind_from_suffix(text[cursor]),
                        text[index + 1] == 'd', index, cursor - index + 1});
        index = cursor;
    }
    return refs;
}

std::vector<std::size_t> collect_explicit_vreg_ids(
    const std::vector<AArch64MachineOperand> &operands, bool defs) {
    std::vector<std::size_t> ids;
    std::unordered_set<std::size_t> seen;
    for (const AArch64MachineOperand &operand : operands) {
        for (const ParsedVirtualRegRef &ref : parse_virtual_reg_refs(operand.get_text())) {
            if (ref.is_def != defs || seen.find(ref.id) != seen.end()) {
                continue;
            }
            seen.insert(ref.id);
            ids.push_back(ref.id);
        }
    }
    return ids;
}

std::string substitute_virtual_registers(const std::string &text,
                                         const AArch64MachineFunction &function) {
    std::string rendered = text;
    const std::vector<ParsedVirtualRegRef> refs = parse_virtual_reg_refs(text);
    std::size_t delta = 0;
    for (const ParsedVirtualRegRef &ref : refs) {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(ref.id);
        if (!physical_reg.has_value()) {
            continue;
        }
        const std::string reg_name =
            render_physical_register(*physical_reg, ref.kind);
        rendered.replace(ref.offset + delta, ref.length, reg_name);
        delta += reg_name.size() - ref.length;
    }
    return rendered;
}

std::string render_vector_move_operand(const std::string &text) {
    std::string rendered;
    rendered.reserve(text.size() + 4);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != 'q' || index + 1 >= text.size() ||
            !std::isdigit(static_cast<unsigned char>(text[index + 1]))) {
            rendered.push_back(text[index]);
            continue;
        }
        if (index > 0) {
            const char previous = text[index - 1];
            if (std::isalnum(static_cast<unsigned char>(previous)) != 0 ||
                previous == '%' || previous == '.') {
                rendered.push_back(text[index]);
                continue;
            }
        }
        std::size_t cursor = index + 1;
        std::string reg_number;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            reg_number.push_back(text[cursor]);
            ++cursor;
        }
        rendered += "v" + reg_number + ".16b";
        index = cursor - 1;
    }
    return rendered;
}

} // namespace sysycc
