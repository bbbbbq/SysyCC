#include "backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.hpp"

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

bool emit_integer_remainder(AArch64MachineBlock &machine_block,
                            CoreIrBinaryOpcode opcode,
                            const CoreIrBinaryInst &binary,
                            const AArch64VirtualReg &lhs_reg,
                            const AArch64VirtualReg &rhs_reg,
                            const AArch64VirtualReg &dst_reg,
                            AArch64MachineFunction &function) {
    const AArch64VirtualReg quotient_reg =
        function.create_virtual_reg(classify_virtual_reg_kind(binary.get_lhs()->get_type()));
    const AArch64VirtualReg product_reg =
        function.create_virtual_reg(classify_virtual_reg_kind(binary.get_lhs()->get_type()));
    machine_block.append_instruction(
        AArch64MachineInstr("mov", {def_vreg_operand(dst_reg),
                                    use_vreg_operand(lhs_reg)}));
    machine_block.append_instruction(
        AArch64MachineInstr("mov", {def_vreg_operand(quotient_reg),
                                    use_vreg_operand(lhs_reg)}));
    machine_block.append_instruction(AArch64MachineInstr(
        opcode == CoreIrBinaryOpcode::SRem ? "sdiv" : "udiv",
        {def_vreg_operand(quotient_reg), use_vreg_operand(quotient_reg),
         use_vreg_operand(rhs_reg)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "mul", {def_vreg_operand(product_reg), use_vreg_operand(quotient_reg),
                use_vreg_operand(rhs_reg)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "sub", {def_vreg_operand(dst_reg), use_vreg_operand(dst_reg),
                use_vreg_operand(product_reg)}));
    return true;
}

} // namespace

bool emit_non_float128_binary(AArch64MachineBlock &machine_block,
                              const CoreIrBinaryInst &binary,
                              const AArch64VirtualReg &lhs_reg,
                              const AArch64VirtualReg &rhs_reg,
                              const AArch64VirtualReg &dst_reg,
                              AArch64MachineFunction &function,
                              DiagnosticEngine &diagnostic_engine) {
    std::string opcode;
    if (is_float_type(binary.get_type())) {
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
            const AArch64VirtualReg lhs32 =
                promote_float16_to_float32(machine_block, lhs_reg, function);
            const AArch64VirtualReg rhs32 =
                promote_float16_to_float32(machine_block, rhs_reg, function);
            const AArch64VirtualReg result32 =
                function.create_virtual_reg(AArch64VirtualRegKind::Float32);
            switch (binary.get_binary_opcode()) {
            case CoreIrBinaryOpcode::Add:
                opcode = "fadd";
                break;
            case CoreIrBinaryOpcode::Sub:
                opcode = "fsub";
                break;
            case CoreIrBinaryOpcode::Mul:
                opcode = "fmul";
                break;
            case CoreIrBinaryOpcode::SDiv:
            case CoreIrBinaryOpcode::UDiv:
                opcode = "fdiv";
                break;
            default:
                add_backend_error(
                    diagnostic_engine,
                    "unsupported _Float16 binary opcode in the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                opcode, {def_vreg_operand(result32), use_vreg_operand(lhs32),
                         use_vreg_operand(rhs32)}));
            demote_float32_to_float16(machine_block, result32, dst_reg);
            return true;
        }

        switch (binary.get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            opcode = "fadd";
            break;
        case CoreIrBinaryOpcode::Sub:
            opcode = "fsub";
            break;
        case CoreIrBinaryOpcode::Mul:
            opcode = "fmul";
            break;
        case CoreIrBinaryOpcode::SDiv:
        case CoreIrBinaryOpcode::UDiv:
            opcode = "fdiv";
            break;
        case CoreIrBinaryOpcode::And:
        case CoreIrBinaryOpcode::Or:
        case CoreIrBinaryOpcode::Xor:
        case CoreIrBinaryOpcode::Shl:
        case CoreIrBinaryOpcode::LShr:
        case CoreIrBinaryOpcode::AShr:
        case CoreIrBinaryOpcode::SRem:
        case CoreIrBinaryOpcode::URem:
            add_backend_error(
                diagnostic_engine,
                "unsupported floating-point binary opcode in the AArch64 native backend");
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            opcode, {def_vreg_operand(dst_reg), use_vreg_operand(lhs_reg),
                     use_vreg_operand(rhs_reg)}));
        return true;
    }

    switch (binary.get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        opcode = "add";
        break;
    case CoreIrBinaryOpcode::Sub:
        opcode = "sub";
        break;
    case CoreIrBinaryOpcode::Mul:
        opcode = "mul";
        break;
    case CoreIrBinaryOpcode::SDiv:
        opcode = "sdiv";
        break;
    case CoreIrBinaryOpcode::UDiv:
        opcode = "udiv";
        break;
    case CoreIrBinaryOpcode::And:
        opcode = "and";
        break;
    case CoreIrBinaryOpcode::Or:
        opcode = "orr";
        break;
    case CoreIrBinaryOpcode::Xor:
        opcode = "eor";
        break;
    case CoreIrBinaryOpcode::Shl:
        opcode = "lsl";
        break;
    case CoreIrBinaryOpcode::LShr:
        opcode = "lsr";
        break;
    case CoreIrBinaryOpcode::AShr:
        opcode = "asr";
        break;
    }

    const auto *integer_type = as_integer_type(binary.get_type());
    const bool is_narrow_integer =
        integer_type != nullptr && integer_type->get_bit_width() < 32;
    const bool is_non_native_integer =
        integer_type != nullptr && integer_type->get_bit_width() < 64 &&
        integer_type->get_bit_width() != 32;
    AArch64VirtualReg lhs_operand_reg = lhs_reg;
    AArch64VirtualReg rhs_operand_reg = rhs_reg;
    if (is_narrow_integer &&
        (binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::UDiv ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::SRem ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::URem)) {
        lhs_operand_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        rhs_operand_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        machine_block.append_instruction(
            AArch64MachineInstr("mov", {def_vreg_operand(lhs_operand_reg),
                                        use_vreg_operand(lhs_reg)}));
        machine_block.append_instruction(
            AArch64MachineInstr("mov", {def_vreg_operand(rhs_operand_reg),
                                        use_vreg_operand(rhs_reg)}));
        const bool is_signed_op =
            binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv ||
            binary.get_binary_opcode() == CoreIrBinaryOpcode::SRem;
        if (is_signed_op) {
            apply_sign_extend_to_virtual_reg(machine_block, lhs_operand_reg,
                                             binary.get_type(), binary.get_type());
            apply_sign_extend_to_virtual_reg(machine_block, rhs_operand_reg,
                                             binary.get_type(), binary.get_type());
        } else {
            apply_zero_extend_to_virtual_reg(machine_block, lhs_operand_reg,
                                             binary.get_type(), binary.get_type());
            apply_zero_extend_to_virtual_reg(machine_block, rhs_operand_reg,
                                             binary.get_type(), binary.get_type());
        }
    }
    if (binary.get_binary_opcode() == CoreIrBinaryOpcode::SRem ||
        binary.get_binary_opcode() == CoreIrBinaryOpcode::URem) {
        const bool ok = emit_integer_remainder(machine_block,
                                               binary.get_binary_opcode(), binary,
                                               lhs_operand_reg, rhs_operand_reg,
                                               dst_reg, function);
        if (ok && is_narrow_integer) {
            apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
        }
        return ok;
    }
    if (is_non_native_integer &&
        (binary.get_binary_opcode() == CoreIrBinaryOpcode::Shl ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::LShr ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::AShr)) {
        machine_block.append_instruction(
            AArch64MachineInstr("mov", {def_vreg_operand(dst_reg),
                                        use_vreg_operand(lhs_reg)}));
        switch (binary.get_binary_opcode()) {
        case CoreIrBinaryOpcode::Shl:
            apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
            break;
        case CoreIrBinaryOpcode::LShr:
            apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                             binary.get_type(), binary.get_type());
            break;
        case CoreIrBinaryOpcode::AShr:
            apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                             binary.get_type(), binary.get_type());
            break;
        default:
            break;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            opcode, {def_vreg_operand(dst_reg), use_vreg_operand(dst_reg),
                     use_vreg_operand(rhs_reg)}));
        if (binary.get_binary_opcode() != CoreIrBinaryOpcode::AShr) {
            apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
        }
        return true;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        opcode, {def_vreg_operand(dst_reg), use_vreg_operand(lhs_operand_reg),
                 use_vreg_operand(rhs_operand_reg)}));
    if (is_narrow_integer &&
        (binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv ||
         binary.get_binary_opcode() == CoreIrBinaryOpcode::UDiv)) {
        apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
    }
    return true;
}

} // namespace sysycc
