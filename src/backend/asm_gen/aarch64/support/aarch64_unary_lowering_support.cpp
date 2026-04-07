#include "backend/asm_gen/aarch64/support/aarch64_unary_lowering_support.hpp"

#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                "AArch64 native backend: " + message);
}

} // namespace

bool emit_non_float128_unary(AArch64MachineBlock &machine_block,
                             const CoreIrUnaryInst &unary,
                             const AArch64VirtualReg &operand_reg,
                             const AArch64VirtualReg &dst_reg,
                             AArch64MachineFunction &function,
                             DiagnosticEngine &diagnostic_engine) {
    switch (unary.get_unary_opcode()) {
    case CoreIrUnaryOpcode::Negate:
        if (is_float_type(unary.get_type())) {
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg promoted =
                    promote_float16_to_float32(machine_block, operand_reg, function);
                const AArch64VirtualReg negated =
                    function.create_virtual_reg(AArch64VirtualRegKind::Float32);
                machine_block.append_instruction(AArch64MachineInstr(
                    "fneg", {def_vreg_operand(negated),
                             use_vreg_operand(promoted)}));
                demote_float32_to_float16(machine_block, negated, dst_reg);
                return true;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "fneg", {def_vreg_operand(dst_reg),
                         use_vreg_operand(operand_reg)}));
            return true;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "neg", {def_vreg_operand(dst_reg), use_vreg_operand(operand_reg)}));
        return true;
    case CoreIrUnaryOpcode::BitwiseNot:
        if (is_float_type(unary.get_type())) {
            add_backend_error(
                diagnostic_engine,
                "bitwise-not on floating-point values is not supported by the "
                "AArch64 native backend");
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "mvn", {def_vreg_operand(dst_reg), use_vreg_operand(operand_reg)}));
        return true;
    case CoreIrUnaryOpcode::LogicalNot:
        if (is_float_type(unary.get_operand()->get_type())) {
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg promoted =
                    promote_float16_to_float32(machine_block, operand_reg, function);
                machine_block.append_instruction(
                    AArch64MachineInstr("fcmp", {use_vreg_operand(promoted),
                                                 AArch64MachineOperand::immediate("#0.0")}));
            } else {
                machine_block.append_instruction(AArch64MachineInstr(
                    "fcmp", {use_vreg_operand(operand_reg),
                             AArch64MachineOperand::immediate("#0.0")}));
            }
        } else {
            machine_block.append_instruction(AArch64MachineInstr(
                "cmp", {use_vreg_operand(operand_reg),
                        AArch64MachineOperand::immediate("#0")}));
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "cset", {def_vreg_operand(dst_reg), condition_code_operand("eq")}));
        return true;
    }

    return false;
}

} // namespace sysycc
