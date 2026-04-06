#include "backend/asm_gen/aarch64/support/aarch64_cast_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

bool emit_non_float128_cast(AArch64MachineBlock &machine_block,
                            const CoreIrCastInst &cast,
                            const AArch64VirtualReg &operand_reg,
                            const AArch64VirtualReg &dst_reg,
                            AArch64MachineFunction &function) {
    switch (cast.get_cast_kind()) {
    case CoreIrCastKind::Truncate:
        machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                         use_vreg_as(operand_reg,
                                                     uses_64bit_register(cast.get_type())));
        apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
        return true;
    case CoreIrCastKind::ZeroExtend:
        machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) + ", " +
                                         use_vreg_as(operand_reg, false));
        apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                         cast.get_operand()->get_type(),
                                         cast.get_type());
        return true;
    case CoreIrCastKind::SignExtend:
        if (uses_64bit_register(cast.get_type())) {
            machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                             ", " + use_vreg_as(operand_reg, false));
        } else {
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
        }
        apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                         cast.get_operand()->get_type(),
                                         cast.get_type());
        return true;
    case CoreIrCastKind::PtrToInt:
        if (uses_64bit_register(cast.get_operand()->get_type()) &&
            !uses_64bit_register(cast.get_type())) {
            machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                             ", " + use_vreg_as(operand_reg, false));
        } else {
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(operand_reg,
                                                         uses_64bit_register(cast.get_type())));
        }
        apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
        return true;
    case CoreIrCastKind::IntToPtr:
        if (uses_64bit_register(cast.get_operand()->get_type())) {
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(operand_reg, true));
        } else {
            machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                             ", " + use_vreg_as(operand_reg, false));
            apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                             cast.get_operand()->get_type(),
                                             cast.get_type());
        }
        return true;
    case CoreIrCastKind::SignedIntToFloat:
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg widened =
                function.create_virtual_reg(AArch64VirtualRegKind::Float32);
            machine_block.append_instruction("scvtf " + def_vreg(widened) + ", " +
                                             use_vreg_as(
                                                 operand_reg,
                                                 uses_64bit_register(
                                                     cast.get_operand()->get_type())));
            demote_float32_to_float16(machine_block, widened, dst_reg);
            return true;
        }
        machine_block.append_instruction("scvtf " + def_vreg(dst_reg) + ", " +
                                         use_vreg_as(
                                             operand_reg,
                                             uses_64bit_register(
                                                 cast.get_operand()->get_type())));
        return true;
    case CoreIrCastKind::UnsignedIntToFloat:
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg widened =
                function.create_virtual_reg(AArch64VirtualRegKind::Float32);
            machine_block.append_instruction("ucvtf " + def_vreg(widened) + ", " +
                                             use_vreg_as(
                                                 operand_reg,
                                                 uses_64bit_register(
                                                     cast.get_operand()->get_type())));
            demote_float32_to_float16(machine_block, widened, dst_reg);
            return true;
        }
        machine_block.append_instruction("ucvtf " + def_vreg(dst_reg) + ", " +
                                         use_vreg_as(
                                             operand_reg,
                                             uses_64bit_register(
                                                 cast.get_operand()->get_type())));
        return true;
    case CoreIrCastKind::FloatToSignedInt:
        if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg widened =
                promote_float16_to_float32(machine_block, operand_reg, function);
            machine_block.append_instruction("fcvtzs " +
                                             def_vreg_as(dst_reg,
                                                         uses_64bit_register(cast.get_type())) +
                                             ", " + use_vreg(widened));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            return true;
        }
        machine_block.append_instruction("fcvtzs " +
                                         def_vreg_as(dst_reg,
                                                     uses_64bit_register(cast.get_type())) +
                                         ", " + use_vreg(operand_reg));
        apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
        return true;
    case CoreIrCastKind::FloatToUnsignedInt:
        if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg widened =
                promote_float16_to_float32(machine_block, operand_reg, function);
            machine_block.append_instruction("fcvtzu " +
                                             def_vreg_as(dst_reg,
                                                         uses_64bit_register(cast.get_type())) +
                                             ", " + use_vreg(widened));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            return true;
        }
        machine_block.append_instruction("fcvtzu " +
                                         def_vreg_as(dst_reg,
                                                     uses_64bit_register(cast.get_type())) +
                                         ", " + use_vreg(operand_reg));
        apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
        return true;
    case CoreIrCastKind::FloatExtend:
        if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg widened =
                promote_float16_to_float32(machine_block, operand_reg, function);
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float32) {
                machine_block.append_instruction("fmov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg(widened));
            } else {
                machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                                 use_vreg(widened));
            }
            return true;
        }
        machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                         use_vreg(operand_reg));
        return true;
    case CoreIrCastKind::FloatTruncate:
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float32) {
                demote_float32_to_float16(machine_block, operand_reg, dst_reg);
            } else {
                const AArch64VirtualReg narrowed =
                    function.create_virtual_reg(AArch64VirtualRegKind::Float32);
                machine_block.append_instruction("fcvt " + def_vreg(narrowed) + ", " +
                                                 use_vreg(operand_reg));
                demote_float32_to_float16(machine_block, narrowed, dst_reg);
            }
            return true;
        }
        machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                         use_vreg(operand_reg));
        return true;
    }
    return false;
}

} // namespace sysycc
