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
        machine_block.append_instruction("and " + def_vreg_as(reg, false) + ", " +
                                         use_vreg_as(reg, false) + ", #1");
        break;
    case 8:
        machine_block.append_instruction("uxtb " + def_vreg_as(reg, false) + ", " +
                                         use_vreg_as(reg, false));
        break;
    case 16:
        machine_block.append_instruction("uxth " + def_vreg_as(reg, false) + ", " +
                                         use_vreg_as(reg, false));
        break;
    default:
        break;
    }
}

void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &dst_reg,
                                      const CoreIrType *source_type,
                                      const CoreIrType *target_type) {
    const auto *source_integer = as_integer_type(source_type);
    if (source_integer == nullptr) {
        return;
    }
    switch (source_integer->get_bit_width()) {
    case 1:
        machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                         use_vreg_as(dst_reg, false) + ", #1");
        break;
    case 8:
        machine_block.append_instruction("uxtb " + def_vreg_as(dst_reg, false) + ", " +
                                         use_vreg_as(dst_reg, false));
        break;
    case 16:
        machine_block.append_instruction("uxth " + def_vreg_as(dst_reg, false) + ", " +
                                         use_vreg_as(dst_reg, false));
        break;
    case 32:
        break;
    default:
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
        machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                         use_vreg_as(dst_reg, false) + ", #1");
        machine_block.append_instruction(
            "neg " + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
            use_vreg_as(dst_reg, target_uses_64bit));
        break;
    case 8:
        machine_block.append_instruction(
            std::string("sxtb ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
            use_vreg_as(dst_reg, false));
        break;
    case 16:
        machine_block.append_instruction(
            std::string("sxth ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
            use_vreg_as(dst_reg, false));
        break;
    case 32:
        if (target_uses_64bit) {
            machine_block.append_instruction("sxtw " + def_vreg_as(dst_reg, true) +
                                             ", " + use_vreg_as(dst_reg, false));
        }
        break;
    default:
        break;
    }
}

AArch64VirtualReg promote_float16_to_float32(AArch64MachineBlock &machine_block,
                                             const AArch64VirtualReg &source_reg,
                                             AArch64MachineFunction &function) {
    const AArch64VirtualReg promoted =
        function.create_virtual_reg(AArch64VirtualRegKind::Float32);
    machine_block.append_instruction("fcvt " + def_vreg(promoted) + ", " +
                                     use_vreg(source_reg));
    return promoted;
}

void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                               const AArch64VirtualReg &source_reg,
                               const AArch64VirtualReg &target_reg) {
    machine_block.append_instruction("fcvt " + def_vreg(target_reg) + ", " +
                                     use_vreg(source_reg));
}

} // namespace sysycc
