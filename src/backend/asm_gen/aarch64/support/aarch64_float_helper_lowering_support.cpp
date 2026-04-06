#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool prepare_integer_value_for_runtime_helper(AArch64MachineBlock &machine_block,
                                              AArch64FloatHelperLoweringContext &context,
                                              const CoreIrType *source_type,
                                              CoreIrCastKind cast_kind,
                                              const AArch64VirtualReg &source_reg,
                                              const AArch64VirtualReg &prepared_reg) {
    if (prepared_reg.get_id() != source_reg.get_id()) {
        machine_block.append_instruction("mov " + def_vreg(prepared_reg) + ", " +
                                         use_vreg(source_reg));
    }
    if (cast_kind == CoreIrCastKind::SignedIntToFloat) {
        context.apply_sign_extend_to_virtual_reg(
            machine_block, prepared_reg, source_type,
            prepared_reg.get_kind() == AArch64VirtualRegKind::General64
                ? context.create_fake_pointer_type()
                : source_type);
    } else {
        context.apply_zero_extend_to_virtual_reg(
            machine_block, prepared_reg, source_type,
            prepared_reg.get_kind() == AArch64VirtualRegKind::General64
                ? context.create_fake_pointer_type()
                : source_type);
    }
    return true;
}

} // namespace

bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                 AArch64FloatHelperLoweringContext &context,
                                 CoreIrBinaryOpcode opcode,
                                 const AArch64VirtualReg &lhs_reg,
                                 const AArch64VirtualReg &rhs_reg,
                                 const AArch64VirtualReg &dst_reg) {
    std::string helper_name;
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
        helper_name = "__addtf3";
        break;
    case CoreIrBinaryOpcode::Sub:
        helper_name = "__subtf3";
        break;
    case CoreIrBinaryOpcode::Mul:
        helper_name = "__multf3";
        break;
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
        helper_name = "__divtf3";
        break;
    default:
        context.report_error(
            "unsupported float128 binary opcode in the AArch64 native backend");
        return false;
    }
    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
        AArch64VirtualRegKind::Float128, lhs_reg);
    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V1),
        AArch64VirtualRegKind::Float128, rhs_reg);
    context.append_helper_call(machine_block, helper_name);
    context.append_copy_from_physical_reg(
        machine_block, dst_reg, static_cast<unsigned>(AArch64PhysicalReg::V0),
        AArch64VirtualRegKind::Float128);
    return true;
}

bool emit_float128_compare_helper(AArch64MachineBlock &machine_block,
                                  AArch64FloatHelperLoweringContext &context,
                                  CoreIrComparePredicate predicate,
                                  const AArch64VirtualReg &lhs_reg,
                                  const AArch64VirtualReg &rhs_reg,
                                  const AArch64VirtualReg &dst_reg,
                                  AArch64MachineFunction &function) {
    const AArch64VirtualReg unordered_reg =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg compare_reg =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg unordered_value_reg =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const bool unordered_result =
        predicate == CoreIrComparePredicate::NotEqual;
    std::string helper_name;
    std::string condition;
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        helper_name = "__eqtf2";
        condition = "eq";
        break;
    case CoreIrComparePredicate::NotEqual:
        helper_name = "__eqtf2";
        condition = "ne";
        break;
    case CoreIrComparePredicate::SignedLess:
    case CoreIrComparePredicate::UnsignedLess:
        helper_name = "__lttf2";
        condition = "lt";
        break;
    case CoreIrComparePredicate::SignedLessEqual:
    case CoreIrComparePredicate::UnsignedLessEqual:
        helper_name = "__getf2";
        condition = "le";
        break;
    case CoreIrComparePredicate::SignedGreater:
    case CoreIrComparePredicate::UnsignedGreater:
        helper_name = "__getf2";
        condition = "gt";
        break;
    case CoreIrComparePredicate::SignedGreaterEqual:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        helper_name = "__getf2";
        condition = "ge";
        break;
    }

    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
        AArch64VirtualRegKind::Float128, lhs_reg);
    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V1),
        AArch64VirtualRegKind::Float128, rhs_reg);
    context.append_helper_call(machine_block, "__unordtf2");
    machine_block.append_instruction("mov " + def_vreg(unordered_reg) + ", w0");

    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
        AArch64VirtualRegKind::Float128, lhs_reg);
    context.append_copy_to_physical_reg(
        machine_block, static_cast<unsigned>(AArch64PhysicalReg::V1),
        AArch64VirtualRegKind::Float128, rhs_reg);
    context.append_helper_call(machine_block, helper_name);
    machine_block.append_instruction("mov " + def_vreg(compare_reg) + ", w0");

    machine_block.append_instruction("cmp " + use_vreg(compare_reg) + ", #0");
    machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", " + condition);
    machine_block.append_instruction("mov " + def_vreg(unordered_value_reg) + ", #" +
                                     std::string(unordered_result ? "1" : "0"));
    machine_block.append_instruction("cmp " + use_vreg(unordered_reg) + ", #0");
    machine_block.append_instruction("csel " + def_vreg(dst_reg) + ", " +
                                     use_vreg(unordered_value_reg) + ", " +
                                     use_vreg(dst_reg) + ", ne");
    return true;
}

bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                               AArch64FloatHelperLoweringContext &context,
                               const CoreIrCastInst &cast,
                               const AArch64VirtualReg &operand_reg,
                               const AArch64VirtualReg &dst_reg,
                               AArch64MachineFunction &function) {
    switch (cast.get_cast_kind()) {
    case CoreIrCastKind::SignedIntToFloat:
    case CoreIrCastKind::UnsignedIntToFloat: {
        const bool operand_is_64bit =
            uses_64bit_register(cast.get_operand()->get_type());
        const AArch64VirtualReg prepared_reg = function.create_virtual_reg(
            operand_is_64bit ? AArch64VirtualRegKind::General64
                             : AArch64VirtualRegKind::General32);
        prepare_integer_value_for_runtime_helper(machine_block, context,
                                                 cast.get_operand()->get_type(),
                                                 cast.get_cast_kind(), operand_reg,
                                                 prepared_reg);
        const std::string helper_name =
            operand_is_64bit
                ? (cast.get_cast_kind() == CoreIrCastKind::SignedIntToFloat
                       ? "__floatditf"
                       : "__floatunditf")
                : (cast.get_cast_kind() == CoreIrCastKind::SignedIntToFloat
                       ? "__floatsitf"
                       : "__floatunsitf");
        context.append_copy_to_physical_reg(
            machine_block, static_cast<unsigned>(AArch64PhysicalReg::X0),
            operand_is_64bit ? AArch64VirtualRegKind::General64
                             : AArch64VirtualRegKind::General32,
            prepared_reg);
        context.append_helper_call(machine_block, helper_name);
        context.append_copy_from_physical_reg(
            machine_block, dst_reg, static_cast<unsigned>(AArch64PhysicalReg::V0),
            AArch64VirtualRegKind::Float128);
        return true;
    }
    case CoreIrCastKind::FloatToSignedInt:
    case CoreIrCastKind::FloatToUnsignedInt: {
        const bool target_is_64bit = uses_64bit_register(cast.get_type());
        const std::string helper_name =
            target_is_64bit
                ? (cast.get_cast_kind() == CoreIrCastKind::FloatToSignedInt
                       ? "__fixtfdi"
                       : "__fixunstfdi")
                : (cast.get_cast_kind() == CoreIrCastKind::FloatToSignedInt
                       ? "__fixtfsi"
                       : "__fixunstfsi");
        context.append_copy_to_physical_reg(
            machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
            AArch64VirtualRegKind::Float128, operand_reg);
        context.append_helper_call(machine_block, helper_name);
        context.append_copy_from_physical_reg(
            machine_block, dst_reg, static_cast<unsigned>(AArch64PhysicalReg::X0),
            target_is_64bit ? AArch64VirtualRegKind::General64
                            : AArch64VirtualRegKind::General32);
        context.apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
        return true;
    }
    case CoreIrCastKind::FloatExtend: {
        if (dst_reg.get_kind() != AArch64VirtualRegKind::Float128) {
            return false;
        }
        if (const auto *float_constant =
                dynamic_cast<const CoreIrConstantFloat *>(cast.get_operand());
            float_constant != nullptr) {
            const std::string literal_text = float_constant->get_literal_text();
            const bool had_long_double_suffix =
                !literal_text.empty() &&
                (literal_text.back() == 'l' || literal_text.back() == 'L');
            if (had_long_double_suffix &&
                !float128_literal_is_supported_by_helper_path(
                    strip_floating_literal_suffix(literal_text))) {
                context.report_error(
                    "float128 literal is not exactly representable by the current "
                    "AArch64 helper-based materialization path");
                return false;
            }
        }
        AArch64VirtualReg helper_operand = operand_reg;
        if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            helper_operand =
                context.promote_float16_to_float32(machine_block, operand_reg, function);
        }
        const std::string helper_name =
            helper_operand.get_kind() == AArch64VirtualRegKind::Float32
                ? "__extendsftf2"
                : "__extenddftf2";
        context.append_copy_to_physical_reg(
            machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
            helper_operand.get_kind(), helper_operand);
        context.append_helper_call(machine_block, helper_name);
        context.append_copy_from_physical_reg(
            machine_block, dst_reg, static_cast<unsigned>(AArch64PhysicalReg::V0),
            AArch64VirtualRegKind::Float128);
        return true;
    }
    case CoreIrCastKind::FloatTruncate: {
        if (operand_reg.get_kind() != AArch64VirtualRegKind::Float128) {
            return false;
        }
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg narrowed =
                function.create_virtual_reg(AArch64VirtualRegKind::Float32);
            context.append_copy_to_physical_reg(
                machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
                AArch64VirtualRegKind::Float128, operand_reg);
            context.append_helper_call(machine_block, "__trunctfsf2");
            context.append_copy_from_physical_reg(
                machine_block, narrowed,
                static_cast<unsigned>(AArch64PhysicalReg::V0),
                AArch64VirtualRegKind::Float32);
            context.demote_float32_to_float16(machine_block, narrowed, dst_reg);
            return true;
        }
        const std::string helper_name =
            dst_reg.get_kind() == AArch64VirtualRegKind::Float32
                ? "__trunctfsf2"
                : "__trunctfdf2";
        context.append_copy_to_physical_reg(
            machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
            AArch64VirtualRegKind::Float128, operand_reg);
        context.append_helper_call(machine_block, helper_name);
        context.append_copy_from_physical_reg(machine_block, dst_reg,
                                              static_cast<unsigned>(AArch64PhysicalReg::V0),
                                              dst_reg.get_kind());
        return true;
    }
    default:
        return false;
    }
}

} // namespace sysycc
