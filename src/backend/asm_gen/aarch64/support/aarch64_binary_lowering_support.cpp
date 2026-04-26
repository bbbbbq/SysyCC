#include "backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

struct SignedDivisionMagic {
    std::int64_t multiplier = 0;
    unsigned shift = 0;
};

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                "AArch64 native backend: " + message);
}

bool materialize_general_immediate(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &target_reg,
                                   std::uint64_t value, bool use_64bit) {
    const unsigned pieces = use_64bit ? 4U : 2U;
    bool emitted_any = false;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (!emitted_any) {
            machine_block.append_instruction(AArch64MachineInstr(
                "movz", {def_vreg_operand_as(target_reg, use_64bit),
                         AArch64MachineOperand::immediate("#" +
                                                          std::to_string(imm16)),
                         shift_operand("lsl", piece * 16U)}));
            emitted_any = true;
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "movk", {def_vreg_operand_as(target_reg, use_64bit),
                     use_vreg_operand_as(target_reg, use_64bit),
                     AArch64MachineOperand::immediate("#" +
                                                      std::to_string(imm16)),
                     shift_operand("lsl", piece * 16U)}));
    }
    if (!emitted_any) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand_as(target_reg, use_64bit),
                    zero_register_operand(use_64bit)}));
    }
    return true;
}

std::string make_hoisted_integer_constant_key(AArch64VirtualRegKind kind,
                                              std::uint64_t value) {
    return std::to_string(static_cast<unsigned>(kind)) + "|" +
           std::to_string(value);
}

bool materialize_hoisted_general_immediate(AArch64MachineFunction &function,
                                           AArch64VirtualRegKind kind,
                                           std::uint64_t value,
                                           AArch64VirtualReg &out) {
    const std::string cache_key = make_hoisted_integer_constant_key(kind, value);
    if (const auto cached = function.get_hoisted_integer_constant_vreg(cache_key);
        cached.has_value()) {
        out = *cached;
        return true;
    }
    if (function.get_blocks().empty()) {
        return false;
    }

    out = function.create_virtual_reg(kind);
    AArch64MachineBlock scratch("__const_hoist");
    if (!materialize_general_immediate(
            scratch, out, value,
            kind == AArch64VirtualRegKind::General64)) {
        return false;
    }

    std::vector<AArch64MachineInstr> &entry_instructions =
        function.get_blocks().front().get_instructions();
    const auto insertion_point =
        std::find_if(entry_instructions.begin(), entry_instructions.end(),
                     [](const AArch64MachineInstr &instruction) {
                         return instruction.get_opcode_descriptor().is_branch ||
                                instruction.get_mnemonic() == "ret";
                     });
    entry_instructions.insert(
        insertion_point, std::make_move_iterator(scratch.get_instructions().begin()),
        std::make_move_iterator(scratch.get_instructions().end()));
    function.set_hoisted_integer_constant_vreg(cache_key, out);
    return true;
}

std::optional<std::int32_t>
try_get_signed_i32_divisor(const CoreIrValue *value, const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
    if (integer_type == nullptr || constant_int == nullptr ||
        integer_type->get_bit_width() != 32) {
        return std::nullopt;
    }
    const std::int32_t signed_value =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(
            constant_int->get_value() & 0xffffffffULL));
    if (signed_value == 0 ||
        signed_value == std::numeric_limits<std::int32_t>::min()) {
        return std::nullopt;
    }
    return signed_value;
}

SignedDivisionMagic compute_signed_division_magic(std::int32_t divisor) {
    constexpr std::uint64_t two31 = 0x80000000ULL;
    const std::uint64_t ad =
        static_cast<std::uint64_t>(divisor < 0 ? -static_cast<std::int64_t>(divisor)
                                               : divisor);
    const std::uint64_t t =
        two31 + static_cast<std::uint32_t>(static_cast<std::uint32_t>(divisor) >> 31U);
    const std::uint64_t anc = t - 1U - (t % ad);
    std::uint64_t p = 31U;
    std::uint64_t q1 = two31 / anc;
    std::uint64_t r1 = two31 - q1 * anc;
    std::uint64_t q2 = two31 / ad;
    std::uint64_t r2 = two31 - q2 * ad;
    std::uint64_t delta = 0;
    do {
        ++p;
        q1 <<= 1U;
        r1 <<= 1U;
        if (r1 >= anc) {
            ++q1;
            r1 -= anc;
        }
        q2 <<= 1U;
        r2 <<= 1U;
        if (r2 >= ad) {
            ++q2;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    std::int64_t multiplier = static_cast<std::int64_t>(q2 + 1U);
    if (divisor < 0) {
        multiplier = -multiplier;
    }
    return SignedDivisionMagic{multiplier, static_cast<unsigned>(p - 32U)};
}

bool is_positive_power_of_two(std::int32_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

unsigned log2_power_of_two(std::int32_t value) {
    return static_cast<unsigned>(
        __builtin_ctz(static_cast<unsigned>(value)));
}

bool emit_signed_power2_remainder(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &lhs_reg,
                                  const AArch64VirtualReg &dst_reg,
                                  std::int32_t divisor_abs,
                                  AArch64MachineFunction &function) {
    if (!is_positive_power_of_two(divisor_abs)) {
        return false;
    }
    if (divisor_abs == 1) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand_as(dst_reg, false), zero_register_operand(false)}));
        return true;
    }

    const unsigned shift = log2_power_of_two(divisor_abs);
    const std::uint32_t mask = static_cast<std::uint32_t>(divisor_abs - 1);
    const AArch64VirtualReg sign =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg bias =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg adjusted =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg quotient =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg product =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);

    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(sign, false), use_vreg_operand_as(lhs_reg, false),
                AArch64MachineOperand::immediate("#31")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "and", {def_vreg_operand_as(bias, false), use_vreg_operand_as(sign, false),
                AArch64MachineOperand::immediate("#" + std::to_string(mask))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand_as(adjusted, false),
                use_vreg_operand_as(lhs_reg, false),
                use_vreg_operand_as(bias, false)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(quotient, false),
                use_vreg_operand_as(adjusted, false),
                AArch64MachineOperand::immediate("#" + std::to_string(shift))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "lsl", {def_vreg_operand_as(product, false),
                use_vreg_operand_as(quotient, false),
                AArch64MachineOperand::immediate("#" + std::to_string(shift))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "sub", {def_vreg_operand_as(dst_reg, false),
                use_vreg_operand_as(lhs_reg, false),
                use_vreg_operand_as(product, false)}));
    return true;
}

bool emit_signed_power2_division(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &lhs_reg,
                                 const AArch64VirtualReg &dst_reg,
                                 std::int32_t divisor_value,
                                 AArch64MachineFunction &function) {
    const std::int32_t divisor_abs = static_cast<std::int32_t>(
        divisor_value < 0 ? -static_cast<std::int64_t>(divisor_value)
                          : divisor_value);
    if (!is_positive_power_of_two(divisor_abs)) {
        return false;
    }
    if (divisor_abs == 1) {
        if (divisor_value > 0) {
            machine_block.append_instruction(AArch64MachineInstr(
                "mov", {def_vreg_operand_as(dst_reg, false),
                        use_vreg_operand_as(lhs_reg, false)}));
        } else {
            machine_block.append_instruction(AArch64MachineInstr(
                "sub", {def_vreg_operand_as(dst_reg, false),
                        zero_register_operand(false),
                        use_vreg_operand_as(lhs_reg, false)}));
        }
        return true;
    }

    const unsigned shift = log2_power_of_two(divisor_abs);
    const std::uint32_t mask = static_cast<std::uint32_t>(divisor_abs - 1);
    const AArch64VirtualReg sign =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg bias =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg adjusted =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);

    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(sign, false), use_vreg_operand_as(lhs_reg, false),
                AArch64MachineOperand::immediate("#31")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "and", {def_vreg_operand_as(bias, false), use_vreg_operand_as(sign, false),
                AArch64MachineOperand::immediate("#" + std::to_string(mask))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand_as(adjusted, false),
                use_vreg_operand_as(lhs_reg, false),
                use_vreg_operand_as(bias, false)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(dst_reg, false),
                use_vreg_operand_as(adjusted, false),
                AArch64MachineOperand::immediate("#" + std::to_string(shift))}));
    if (divisor_value < 0) {
        machine_block.append_instruction(AArch64MachineInstr(
            "sub", {def_vreg_operand_as(dst_reg, false),
                    zero_register_operand(false),
                    use_vreg_operand_as(dst_reg, false)}));
    }
    return true;
}

bool emit_signed_positive_constant_division(AArch64MachineBlock &machine_block,
                                            const AArch64VirtualReg &lhs_reg,
                                            const AArch64VirtualReg &dst_reg,
                                            std::int32_t divisor_value,
                                            AArch64MachineFunction &function) {
    if (divisor_value <= 0 || is_positive_power_of_two(divisor_value)) {
        return false;
    }
    const SignedDivisionMagic magic =
        compute_signed_division_magic(divisor_value);
    if (magic.multiplier <= 0 ||
        magic.multiplier >
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }

    AArch64VirtualReg magic32 =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg product64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);
    const AArch64VirtualReg quotient64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);
    const AArch64VirtualReg sign64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);

    if (!materialize_hoisted_general_immediate(
            function, AArch64VirtualRegKind::General32,
            static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(magic.multiplier)),
            magic32)) {
        return false;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "smull", {def_vreg_operand_as(product64, true),
                  use_vreg_operand_as(lhs_reg, false),
                  use_vreg_operand_as(magic32, false)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(quotient64, true),
                use_vreg_operand_as(product64, true),
                AArch64MachineOperand::immediate(
                    "#" + std::to_string(32U + magic.shift))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "lsr", {def_vreg_operand_as(sign64, true),
                use_vreg_operand_as(product64, true),
                AArch64MachineOperand::immediate("#63")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand_as(quotient64, true),
                use_vreg_operand_as(quotient64, true),
                use_vreg_operand_as(sign64, true)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand_as(dst_reg, false),
                use_vreg_operand_as(quotient64, false)}));
    return true;
}

bool emit_signed_constant_remainder(AArch64MachineBlock &machine_block,
                                    const CoreIrBinaryInst &binary,
                                    const AArch64VirtualReg &lhs_reg,
                                    const AArch64VirtualReg &dst_reg,
                                    std::int32_t divisor_value,
                                    std::int32_t divisor_abs,
                                    AArch64MachineFunction &function) {
    const SignedDivisionMagic magic = compute_signed_division_magic(divisor_abs);
    if (magic.multiplier <= 0 ||
        magic.multiplier >
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }

    AArch64VirtualReg magic32 =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    const AArch64VirtualReg sign64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);
    const AArch64VirtualReg product64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);
    const AArch64VirtualReg quotient64 =
        function.create_virtual_reg(AArch64VirtualRegKind::General64);
    AArch64VirtualReg divisor32 =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);

    if (!materialize_hoisted_general_immediate(
            function, AArch64VirtualRegKind::General32,
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(magic.multiplier)),
            magic32)) {
        return false;
    }
    if (!materialize_hoisted_general_immediate(
            function, AArch64VirtualRegKind::General32,
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(divisor_value)),
            divisor32)) {
        return false;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "smull", {def_vreg_operand_as(product64, true), use_vreg_operand_as(lhs_reg, false),
                  use_vreg_operand_as(magic32, false)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "asr", {def_vreg_operand_as(quotient64, true),
                use_vreg_operand_as(product64, true),
                AArch64MachineOperand::immediate(
                    "#" + std::to_string(32U + magic.shift))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand_as(sign64, true), use_vreg_operand_as(product64, true)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "lsr", {def_vreg_operand_as(sign64, true), use_vreg_operand_as(sign64, true),
                AArch64MachineOperand::immediate("#63")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand_as(quotient64, true), use_vreg_operand_as(quotient64, true),
                use_vreg_operand_as(sign64, true)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "msub", {def_vreg_operand(dst_reg), use_vreg_operand_as(quotient64, false),
                 use_vreg_operand(divisor32), use_vreg_operand(lhs_reg)}));
    return true;
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
        "msub", {def_vreg_operand(dst_reg), use_vreg_operand(quotient_reg),
                 use_vreg_operand(rhs_reg), use_vreg_operand(dst_reg)}));
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
        if (binary.get_binary_opcode() == CoreIrBinaryOpcode::SRem) {
            if (const auto divisor =
                    try_get_signed_i32_divisor(binary.get_rhs(), binary.get_type());
                divisor.has_value()) {
                const std::int32_t divisor_abs = static_cast<std::int32_t>(
                    *divisor < 0 ? -static_cast<std::int64_t>(*divisor) : *divisor);
                if (emit_signed_power2_remainder(machine_block, lhs_operand_reg,
                                                 dst_reg, divisor_abs, function)) {
                    if (is_narrow_integer) {
                        apply_truncate_to_virtual_reg(machine_block, dst_reg,
                                                      binary.get_type());
                    }
                    return true;
                }
                const bool ok = emit_signed_constant_remainder(
                    machine_block, binary, lhs_operand_reg, dst_reg, *divisor,
                    divisor_abs, function);
                if (ok && is_narrow_integer) {
                    apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
                }
                if (ok) {
                    return true;
                }
            }
        }
        const bool ok = emit_integer_remainder(machine_block,
                                               binary.get_binary_opcode(), binary,
                                               lhs_operand_reg, rhs_operand_reg,
                                               dst_reg, function);
        if (ok && is_narrow_integer) {
            apply_truncate_to_virtual_reg(machine_block, dst_reg, binary.get_type());
        }
        return ok;
    }
    if (binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv) {
        if (const auto divisor =
                try_get_signed_i32_divisor(binary.get_rhs(), binary.get_type());
            divisor.has_value()) {
            if (emit_signed_power2_division(machine_block, lhs_operand_reg, dst_reg,
                                            *divisor, function) ||
                emit_signed_positive_constant_division(
                    machine_block, lhs_operand_reg, dst_reg, *divisor, function)) {
                return true;
            }
        }
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
