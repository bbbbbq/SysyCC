#include "backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_cast_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_compare_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_unary_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"

namespace sysycc {

namespace {

const CoreIrConstantFloat &float128_zero_constant() {
    static CoreIrFloatType float128_type(CoreIrFloatKind::Float128);
    static CoreIrConstantFloat zero_constant(&float128_type, "0.0");
    return zero_constant;
}

} // namespace

bool emit_binary_instruction(AArch64MachineBlock &machine_block,
                             AArch64ScalarLoweringContext &context,
                             const CoreIrBinaryInst &binary,
                             AArch64MachineFunction &function) {
    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, binary.get_lhs(), lhs_reg) ||
        !context.ensure_value_in_vreg(machine_block, binary.get_rhs(), rhs_reg) ||
        !context.require_canonical_vreg(&binary, dst_reg)) {
        return false;
    }

    if (is_float_type(binary.get_type()) &&
        dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
        return context.emit_float128_binary_helper(machine_block,
                                                   binary.get_binary_opcode(),
                                                   lhs_reg, rhs_reg, dst_reg);
    }

    return emit_non_float128_binary(machine_block, binary, lhs_reg, rhs_reg, dst_reg,
                                    function, context.diagnostic_engine());
}

bool emit_unary_instruction(AArch64MachineBlock &machine_block,
                            AArch64ScalarLoweringContext &context,
                            const CoreIrUnaryInst &unary,
                            AArch64MachineFunction &function) {
    AArch64VirtualReg operand_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, unary.get_operand(),
                                      operand_reg) ||
        !context.require_canonical_vreg(&unary, dst_reg)) {
        return false;
    }

    switch (unary.get_unary_opcode()) {
    case CoreIrUnaryOpcode::Negate:
        if (is_float_type(unary.get_type()) &&
            dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
            const AArch64VirtualReg zero_reg =
                function.create_virtual_reg(AArch64VirtualRegKind::Float128);
            if (!context.materialize_float_constant(machine_block,
                                                    float128_zero_constant(),
                                                    zero_reg, function) ||
                !context.emit_float128_binary_helper(machine_block,
                                                     CoreIrBinaryOpcode::Sub,
                                                     zero_reg, operand_reg,
                                                     dst_reg)) {
                return false;
            }
            return true;
        }
        return emit_non_float128_unary(machine_block, unary, operand_reg, dst_reg,
                                       function, context.diagnostic_engine());
    case CoreIrUnaryOpcode::BitwiseNot:
    case CoreIrUnaryOpcode::LogicalNot:
        if (unary.get_unary_opcode() == CoreIrUnaryOpcode::LogicalNot &&
            is_float_type(unary.get_operand()->get_type()) &&
            operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
            const AArch64VirtualReg zero_reg =
                function.create_virtual_reg(AArch64VirtualRegKind::Float128);
            if (!context.materialize_float_constant(machine_block,
                                                    float128_zero_constant(),
                                                    zero_reg, function) ||
                !context.emit_float128_compare_helper(
                    machine_block, CoreIrComparePredicate::Equal, operand_reg,
                    zero_reg, dst_reg, function)) {
                return false;
            }
            return true;
        }
        return emit_non_float128_unary(machine_block, unary, operand_reg, dst_reg,
                                       function, context.diagnostic_engine());
    }

    return false;
}

bool emit_compare_instruction(AArch64MachineBlock &machine_block,
                              AArch64ScalarLoweringContext &context,
                              const CoreIrCompareInst &compare,
                              AArch64MachineFunction &function) {
    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, compare.get_lhs(), lhs_reg) ||
        !context.ensure_value_in_vreg(machine_block, compare.get_rhs(), rhs_reg) ||
        !context.require_canonical_vreg(&compare, dst_reg)) {
        return false;
    }

    if (lhs_reg.get_kind() == AArch64VirtualRegKind::Float128) {
        return context.emit_float128_compare_helper(machine_block,
                                                    compare.get_predicate(),
                                                    lhs_reg, rhs_reg, dst_reg,
                                                    function);
    }

    return emit_non_float128_compare(machine_block, compare, lhs_reg, rhs_reg,
                                     dst_reg, function);
}

bool emit_select_instruction(AArch64MachineBlock &machine_block,
                             AArch64ScalarLoweringContext &context,
                             const CoreIrSelectInst &select,
                             AArch64MachineFunction &function) {
    AArch64VirtualReg condition_reg;
    AArch64VirtualReg true_reg;
    AArch64VirtualReg false_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, select.get_condition(),
                                      condition_reg) ||
        !context.ensure_value_in_vreg(machine_block, select.get_true_value(),
                                      true_reg) ||
        !context.ensure_value_in_vreg(machine_block, select.get_false_value(),
                                      false_reg) ||
        !context.require_canonical_vreg(&select, dst_reg)) {
        return false;
    }

    if (dst_reg.is_floating_point()) {
        return false;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "cmp", {use_vreg_operand(condition_reg),
                AArch64MachineOperand::immediate("#0")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "csel", {def_vreg_operand(dst_reg), use_vreg_operand(true_reg),
                 use_vreg_operand(false_reg), condition_code_operand("ne")}));
    return true;
}

bool emit_cast_instruction(AArch64MachineBlock &machine_block,
                           AArch64ScalarLoweringContext &context,
                           const CoreIrCastInst &cast,
                           AArch64MachineFunction &function) {
    AArch64VirtualReg operand_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, cast.get_operand(),
                                      operand_reg) ||
        !context.require_canonical_vreg(&cast, dst_reg)) {
        return false;
    }

    if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
        operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
        return context.emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, function);
    }

    return emit_non_float128_cast(machine_block, cast, operand_reg, dst_reg,
                                  function);
}

} // namespace sysycc
