#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &reg,
                                   const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return;
    }
    switch (integer_type->get_bit_width()) {
    case 1:
        machine_block.append_instruction(AArch64MachineInstr(
            "and", {def_vreg_operand_as(reg, false), use_vreg_operand_as(reg, false),
                    AArch64MachineOperand::immediate("#1")}));
        break;
    case 8:
        machine_block.append_instruction(AArch64MachineInstr(
            "uxtb", {def_vreg_operand_as(reg, false), use_vreg_operand_as(reg, false)}));
        break;
    case 16:
        machine_block.append_instruction(AArch64MachineInstr(
            "uxth", {def_vreg_operand_as(reg, false), use_vreg_operand_as(reg, false)}));
        break;
    case 32:
    case 64:
        break;
    default:
        if (integer_type->get_bit_width() < 64) {
            machine_block.append_instruction(AArch64MachineInstr(
                "ubfx", {def_vreg_operand_as(reg, integer_type->get_bit_width() > 32),
                         use_vreg_operand_as(reg, integer_type->get_bit_width() > 32),
                         AArch64MachineOperand::immediate("#0"),
                         AArch64MachineOperand::immediate(
                             "#" + std::to_string(integer_type->get_bit_width()))}));
        }
        break;
    }
}

void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &dst_reg,
                                      const CoreIrType *source_type,
                                      const CoreIrType *target_type) {
    const auto *source_integer = as_integer_type(source_type);
    const bool target_uses_64bit = uses_64bit_register(target_type);
    if (source_integer == nullptr) {
        return;
    }
    switch (source_integer->get_bit_width()) {
    case 1:
        machine_block.append_instruction(AArch64MachineInstr(
            "and",
            {def_vreg_operand_as(dst_reg, false), use_vreg_operand_as(dst_reg, false),
             AArch64MachineOperand::immediate("#1")}));
        if (target_uses_64bit) {
            machine_block.append_instruction(AArch64MachineInstr(
                "uxtw", {def_vreg_operand_as(dst_reg, true),
                         use_vreg_operand_as(dst_reg, false)}));
        }
        break;
    case 8:
        machine_block.append_instruction(
            AArch64MachineInstr("uxtb", {def_vreg_operand_as(dst_reg, target_uses_64bit),
                                         use_vreg_operand_as(dst_reg, false)}));
        break;
    case 16:
        machine_block.append_instruction(
            AArch64MachineInstr("uxth", {def_vreg_operand_as(dst_reg, target_uses_64bit),
                                         use_vreg_operand_as(dst_reg, false)}));
        break;
    case 32:
        if (target_uses_64bit) {
            machine_block.append_instruction(AArch64MachineInstr(
                "uxtw", {def_vreg_operand_as(dst_reg, true),
                         use_vreg_operand_as(dst_reg, false)}));
        }
        break;
    case 64:
        break;
    default:
        if (source_integer->get_bit_width() < 64) {
            machine_block.append_instruction(AArch64MachineInstr(
                "ubfx", {def_vreg_operand_as(dst_reg,
                                             target_uses_64bit ||
                                                 source_integer->get_bit_width() > 32),
                         use_vreg_operand_as(dst_reg, false),
                         AArch64MachineOperand::immediate("#0"),
                         AArch64MachineOperand::immediate(
                             "#" + std::to_string(source_integer->get_bit_width()))}));
        }
        break;
    }
}

void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &dst_reg,
                                      const CoreIrType *source_type,
                                      const CoreIrType *target_type) {
    const auto *source_integer = as_integer_type(source_type);
    const bool target_uses_64bit = uses_64bit_register(target_type);
    if (source_integer == nullptr) {
        return;
    }
    switch (source_integer->get_bit_width()) {
    case 1:
        machine_block.append_instruction(AArch64MachineInstr(
            "and",
            {def_vreg_operand_as(dst_reg, false), use_vreg_operand_as(dst_reg, false),
             AArch64MachineOperand::immediate("#1")}));
        machine_block.append_instruction(AArch64MachineInstr(
            "neg", {def_vreg_operand_as(dst_reg, target_uses_64bit),
                    use_vreg_operand_as(dst_reg, target_uses_64bit)}));
        break;
    case 8:
        machine_block.append_instruction(AArch64MachineInstr(
            "sxtb", {def_vreg_operand_as(dst_reg, target_uses_64bit),
                     use_vreg_operand_as(dst_reg, false)}));
        break;
    case 16:
        machine_block.append_instruction(AArch64MachineInstr(
            "sxth", {def_vreg_operand_as(dst_reg, target_uses_64bit),
                     use_vreg_operand_as(dst_reg, false)}));
        break;
    case 32:
        if (target_uses_64bit) {
            machine_block.append_instruction(AArch64MachineInstr(
                "sxtw", {def_vreg_operand_as(dst_reg, true),
                         use_vreg_operand_as(dst_reg, false)}));
        }
        break;
    case 64:
        break;
    default:
        if (source_integer->get_bit_width() < 64) {
            const bool use_64bit = target_uses_64bit ||
                                   source_integer->get_bit_width() > 32;
            machine_block.append_instruction(AArch64MachineInstr(
                "sbfx", {def_vreg_operand_as(dst_reg, use_64bit),
                         use_vreg_operand_as(dst_reg, use_64bit),
                         AArch64MachineOperand::immediate("#0"),
                         AArch64MachineOperand::immediate(
                             "#" + std::to_string(source_integer->get_bit_width()))}));
        }
        break;
    }
}

AArch64VirtualReg promote_float16_to_float32(AArch64MachineBlock &machine_block,
                                             const AArch64VirtualReg &source_reg,
                                             AArch64MachineFunction &function) {
    const AArch64VirtualReg promoted =
        function.create_virtual_reg(AArch64VirtualRegKind::Float32);
    machine_block.append_instruction(AArch64MachineInstr(
        "fcvt", {def_vreg_operand(promoted), use_vreg_operand(source_reg)}));
    return promoted;
}

void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                               const AArch64VirtualReg &source_reg,
                               const AArch64VirtualReg &target_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "fcvt", {def_vreg_operand(target_reg), use_vreg_operand(source_reg)}));
}

} // namespace sysycc
