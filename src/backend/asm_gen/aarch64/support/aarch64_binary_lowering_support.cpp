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
    machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                     use_vreg(lhs_reg));
    machine_block.append_instruction("mov " + def_vreg(quotient_reg) + ", " +
                                     use_vreg(lhs_reg));
    machine_block.append_instruction(
        std::string(opcode == CoreIrBinaryOpcode::SRem ? "sdiv " : "udiv ") +
        def_vreg(quotient_reg) + ", " + use_vreg(quotient_reg) + ", " +
        use_vreg(rhs_reg));
    machine_block.append_instruction("mul " + def_vreg(product_reg) + ", " +
                                     use_vreg(quotient_reg) + ", " +
                                     use_vreg(rhs_reg));
    machine_block.append_instruction("sub " + def_vreg(dst_reg) + ", " +
                                     use_vreg(dst_reg) + ", " +
                                     use_vreg(product_reg));
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
            machine_block.append_instruction(opcode + " " + def_vreg(result32) + ", " +
                                             use_vreg(lhs32) + ", " +
                                             use_vreg(rhs32));
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
        machine_block.append_instruction(opcode + " " + def_vreg(dst_reg) + ", " +
                                         use_vreg(lhs_reg) + ", " +
                                         use_vreg(rhs_reg));
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
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
        return emit_integer_remainder(machine_block, binary.get_binary_opcode(),
                                      binary, lhs_reg, rhs_reg, dst_reg, function);
    }

    machine_block.append_instruction(opcode + " " + def_vreg(dst_reg) + ", " +
                                     use_vreg(lhs_reg) + ", " +
                                     use_vreg(rhs_reg));
    return true;
}

} // namespace sysycc
