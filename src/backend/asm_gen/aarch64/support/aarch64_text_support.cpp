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

std::string stack_pointer_name(bool use_64bit) {
    return use_64bit ? "sp" : "wsp";
}

std::string zero_register_name(bool use_64bit) {
    return use_64bit ? "xzr" : "wzr";
}

AArch64MachineOperand stack_pointer_operand(bool use_64bit) {
    return AArch64MachineOperand::stack_pointer(use_64bit);
}

AArch64MachineOperand zero_register_operand(bool use_64bit) {
    return AArch64MachineOperand::zero_register(use_64bit);
}

AArch64MachineOperand condition_code_operand(std::string_view condition) {
    return AArch64MachineOperand::condition_code(std::string(condition));
}

AArch64MachineOperand shift_operand(std::string_view mnemonic, unsigned amount) {
    return AArch64MachineOperand::shift(std::string(mnemonic), amount);
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

std::string render_virtual_reg_fallback(
    const AArch64MachineVirtualRegOperand &virtual_reg) {
    return virtual_reg.is_def ? def_vreg(virtual_reg.reg) : use_vreg(virtual_reg.reg);
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

AArch64MachineOperand use_vreg_operand(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand::use_virtual_reg(reg);
}

AArch64MachineOperand def_vreg_operand(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand::def_virtual_reg(reg);
}

AArch64MachineOperand use_vreg_operand_as(const AArch64VirtualReg &reg,
                                          bool use_64bit) {
    return use_vreg_operand_as_kind(
        reg, use_64bit ? AArch64VirtualRegKind::General64
                       : AArch64VirtualRegKind::General32);
}

AArch64MachineOperand def_vreg_operand_as(const AArch64VirtualReg &reg,
                                          bool use_64bit) {
    return def_vreg_operand_as_kind(
        reg, use_64bit ? AArch64VirtualRegKind::General64
                       : AArch64VirtualRegKind::General32);
}

AArch64MachineOperand use_vreg_operand_as_kind(const AArch64VirtualReg &reg,
                                               AArch64VirtualRegKind kind) {
    return AArch64MachineOperand::use_virtual_reg(
        AArch64VirtualReg(reg.get_id(), kind));
}

AArch64MachineOperand def_vreg_operand_as_kind(const AArch64VirtualReg &reg,
                                               AArch64VirtualRegKind kind) {
    return AArch64MachineOperand::def_virtual_reg(
        AArch64VirtualReg(reg.get_id(), kind));
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
        machine_block.append_instruction(AArch64MachineInstr(
            fp_move_mnemonic(dst_reg.get_kind()),
            {AArch64MachineOperand::def_virtual_reg(dst_reg),
             AArch64MachineOperand::use_virtual_reg(
                 AArch64VirtualReg(src_reg.get_id(), dst_reg.get_kind()))}));
        return;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov",
        {AArch64MachineOperand::def_virtual_reg(dst_reg),
         AArch64MachineOperand::use_virtual_reg(
             AArch64VirtualReg(src_reg.get_id(), dst_reg.get_kind()))}));
}

void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &dst_reg,
                                   unsigned physical_reg,
                                   AArch64VirtualRegKind physical_kind) {
    if (dst_reg.is_floating_point()) {
        machine_block.append_instruction(AArch64MachineInstr(
            fp_move_mnemonic(dst_reg.get_kind()),
            {AArch64MachineOperand::def_virtual_reg(dst_reg),
             AArch64MachineOperand::physical_reg(physical_reg, physical_kind)}));
        return;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov",
        {AArch64MachineOperand::def_virtual_reg(dst_reg),
         AArch64MachineOperand::physical_reg(physical_reg, physical_kind)}));
}

void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                 unsigned physical_reg,
                                 AArch64VirtualRegKind physical_kind,
                                 const AArch64VirtualReg &src_reg) {
    if (src_reg.is_floating_point()) {
        machine_block.append_instruction(AArch64MachineInstr(
            fp_move_mnemonic(src_reg.get_kind()),
            {AArch64MachineOperand::physical_reg(physical_reg, physical_kind),
             AArch64MachineOperand::use_virtual_reg(src_reg)}));
        return;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov",
        {AArch64MachineOperand::physical_reg(physical_reg, physical_kind),
         AArch64MachineOperand::use_virtual_reg(AArch64VirtualReg(
             src_reg.get_id(),
             uses_general_64bit_register(physical_kind)
                 ? AArch64VirtualRegKind::General64
                 : AArch64VirtualRegKind::General32))}));
}

std::vector<ParsedVirtualRegRef>
collect_virtual_reg_refs(const AArch64MachineOperand &operand) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return {{virtual_reg->reg.get_id(), virtual_reg->reg.get_kind(),
                 virtual_reg->is_def}};
    }
    if (const auto *memory = operand.get_memory_address_operand(); memory != nullptr) {
        if (memory->base_kind !=
            AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg) {
            return {};
        }
        return {{memory->virtual_reg.get_id(), memory->virtual_reg.get_kind(), false}};
    }
    if (operand.get_physical_reg_operand() != nullptr ||
        operand.get_condition_code_operand() != nullptr ||
        operand.get_zero_register_operand() != nullptr ||
        operand.get_shift_operand() != nullptr ||
        operand.get_stack_pointer_operand() != nullptr) {
        return {};
    }
    return {};
}

namespace {

std::string render_memory_address_operand_for_asm(
    const AArch64MachineMemoryAddressOperand &memory,
    const AArch64MachineFunction &function) {
    std::string base_text;
    switch (memory.base_kind) {
    case AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg: {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(memory.virtual_reg.get_id());
        if (!physical_reg.has_value()) {
            base_text = use_vreg(memory.virtual_reg);
            break;
        }
        base_text =
            render_physical_register(*physical_reg, memory.virtual_reg.get_kind());
        break;
    }
    case AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg:
        base_text = render_physical_register(memory.physical_reg, true);
        break;
    case AArch64MachineMemoryAddressOperand::BaseKind::StackPointer:
        base_text = stack_pointer_name(memory.stack_pointer_use_64bit);
        break;
    }

    std::string offset_text;
    if (const std::optional<long long> immediate_offset =
            memory.get_immediate_offset();
        immediate_offset.has_value()) {
        offset_text = "#" + std::to_string(*immediate_offset);
    } else if (const std::string *symbolic_offset =
                   memory.get_symbolic_offset_text();
               symbolic_offset != nullptr) {
        offset_text = *symbolic_offset;
    }

    if (offset_text.empty()) {
        return "[" + base_text + "]";
    }
    switch (memory.address_mode) {
    case AArch64MachineMemoryAddressOperand::AddressMode::Offset:
        return "[" + base_text + ", " + offset_text + "]";
    case AArch64MachineMemoryAddressOperand::AddressMode::PreIndex:
        return "[" + base_text + ", " + offset_text + "]!";
    case AArch64MachineMemoryAddressOperand::AddressMode::PostIndex:
        return "[" + base_text + "], " + offset_text;
    }
    return "[" + base_text + ", " + offset_text + "]";
}

} // namespace

std::vector<std::size_t> collect_explicit_vreg_ids(
    const std::vector<AArch64MachineOperand> &operands, bool defs) {
    std::vector<std::size_t> ids;
    std::unordered_set<std::size_t> seen;
    for (const AArch64MachineOperand &operand : operands) {
        for (const ParsedVirtualRegRef &ref : collect_virtual_reg_refs(operand)) {
            if (ref.is_def != defs || seen.find(ref.id) != seen.end()) {
                continue;
            }
            seen.insert(ref.id);
            ids.push_back(ref.id);
        }
    }
    return ids;
}

std::string render_machine_operand_for_asm(const AArch64MachineOperand &operand,
                                           const AArch64MachineFunction &function) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            return render_virtual_reg_fallback(*virtual_reg);
        }
        return render_physical_register(*physical_reg, virtual_reg->reg.get_kind());
    }
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        return render_physical_register(physical_reg->reg_number, physical_reg->kind);
    }
    if (const auto *immediate = operand.get_immediate_operand(); immediate != nullptr) {
        return immediate->asm_text;
    }
    if (const auto *symbol = operand.get_symbol_operand(); symbol != nullptr) {
        return symbol->symbol_text;
    }
    if (const auto *label = operand.get_label_operand(); label != nullptr) {
        return label->label_text;
    }
    if (const auto *condition_code = operand.get_condition_code_operand();
        condition_code != nullptr) {
        return condition_code->code;
    }
    if (const auto *zero_register = operand.get_zero_register_operand();
        zero_register != nullptr) {
        return zero_register_name(zero_register->use_64bit);
    }
    if (const auto *shift = operand.get_shift_operand(); shift != nullptr) {
        return shift->mnemonic + " #" + std::to_string(shift->amount);
    }
    if (const auto *stack_pointer = operand.get_stack_pointer_operand();
        stack_pointer != nullptr) {
        return stack_pointer_name(stack_pointer->use_64bit);
    }
    if (const auto *memory = operand.get_memory_address_operand(); memory != nullptr) {
        return render_memory_address_operand_for_asm(*memory, function);
    }
    return {};
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
