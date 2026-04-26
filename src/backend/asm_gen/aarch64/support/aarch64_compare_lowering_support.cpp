#include "backend/asm_gen/aarch64/support/aarch64_compare_lowering_support.hpp"

#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

std::string float_condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
    case CoreIrComparePredicate::UnsignedLess:
        return "mi";
    case CoreIrComparePredicate::SignedLessEqual:
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::SignedGreater:
    case CoreIrComparePredicate::UnsignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "ge";
    }
    return "eq";
}

std::string integer_condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
        return "lt";
    case CoreIrComparePredicate::SignedLessEqual:
        return "le";
    case CoreIrComparePredicate::SignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
        return "ge";
    case CoreIrComparePredicate::UnsignedLess:
        return "lo";
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::UnsignedGreater:
        return "hi";
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "hs";
    }
    return "eq";
}

} // namespace

bool emit_non_float128_compare(AArch64MachineBlock &machine_block,
                               const CoreIrCompareInst &compare,
                               const AArch64VirtualReg &lhs_reg,
                               const AArch64VirtualReg &rhs_reg,
                               const AArch64VirtualReg &dst_reg,
                               AArch64MachineFunction &function) {
    if (is_float_type(compare.get_lhs()->get_type())) {
        if (lhs_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg lhs32 =
                promote_float16_to_float32(machine_block, lhs_reg, function);
            const AArch64VirtualReg rhs32 =
                promote_float16_to_float32(machine_block, rhs_reg, function);
            machine_block.append_instruction(
                AArch64MachineInstr("fcmp", {use_vreg_operand(lhs32),
                                             use_vreg_operand(rhs32)}));
        } else {
            machine_block.append_instruction(
                AArch64MachineInstr("fcmp", {use_vreg_operand(lhs_reg),
                                             use_vreg_operand(rhs_reg)}));
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "cset", {def_vreg_operand(dst_reg),
                     condition_code_operand(float_condition_code(compare.get_predicate()))}));
        return true;
    }

    machine_block.append_instruction(
        AArch64MachineInstr("cmp", {use_vreg_operand(lhs_reg),
                                    use_vreg_operand(rhs_reg)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "cset", {def_vreg_operand(dst_reg),
                 condition_code_operand(integer_condition_code(compare.get_predicate()))}));
    return true;
}

bool emit_non_float128_compare_immediate(AArch64MachineBlock &machine_block,
                                         const CoreIrCompareInst &compare,
                                         const AArch64VirtualReg &lhs_reg,
                                         long long rhs_immediate,
                                         const AArch64VirtualReg &dst_reg,
                                         AArch64MachineFunction &function) {
    (void)function;
    if (is_float_type(compare.get_lhs()->get_type())) {
        return false;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "cmp", {use_vreg_operand(lhs_reg),
                AArch64MachineOperand::immediate("#" +
                                                 std::to_string(rhs_immediate))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "cset", {def_vreg_operand(dst_reg),
                 condition_code_operand(integer_condition_code(compare.get_predicate()))}));
    return true;
}

} // namespace sysycc
