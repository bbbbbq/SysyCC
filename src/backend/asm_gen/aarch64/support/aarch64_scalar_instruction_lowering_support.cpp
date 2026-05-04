#include "backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_cast_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_compare_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_unary_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

#include <cstdint>
#include <vector>

namespace sysycc {

namespace {

const CoreIrConstantFloat &float128_zero_constant() {
    static CoreIrFloatType float128_type(CoreIrFloatKind::Float128);
    static CoreIrConstantFloat zero_constant(&float128_type, "0.0");
    return zero_constant;
}

std::optional<std::uint64_t>
try_get_nonnegative_integer_immediate_value(const CoreIrValue *value) {
    if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
        return 0;
    }
    const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
    if (constant_int == nullptr) {
        return std::nullopt;
    }
    return constant_int->get_value();
}

bool can_encode_add_sub_immediate(std::uint64_t value) {
    return value <= 0xfffU || ((value & 0xfffU) == 0 && value <= 0xfff000U);
}

bool can_encode_compare_immediate(std::uint64_t value) { return value <= 0xfffU; }

bool can_encode_shift_immediate(std::uint64_t value, bool use_64bit) {
    return value < (use_64bit ? 64U : 32U);
}

void emit_integer_constant(AArch64MachineBlock &machine_block,
                           const AArch64VirtualReg &dst_reg,
                           std::uint64_t value) {
    const bool use_64bit = dst_reg.get_use_64bit();
    value &= use_64bit ? ~0ULL : 0xFFFFFFFFULL;
    if (value == 0) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand(dst_reg), zero_register_operand(use_64bit)}));
        return;
    }
    const unsigned pieces = use_64bit ? 4U : 2U;
    bool emitted_first_piece = false;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (!emitted_first_piece) {
            machine_block.append_instruction(AArch64MachineInstr(
                "movz", {def_vreg_operand(dst_reg),
                         AArch64MachineOperand::immediate("#" +
                                                          std::to_string(imm16)),
                         shift_operand("lsl", piece * 16U)}));
            emitted_first_piece = true;
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "movk", {def_vreg_operand(dst_reg), use_vreg_operand(dst_reg),
                     AArch64MachineOperand::immediate("#" +
                                                      std::to_string(imm16)),
                     shift_operand("lsl", piece * 16U)}));
    }
}

std::uint64_t bitmask_for_width(unsigned width) {
    return width >= 64U ? ~0ULL : ((1ULL << width) - 1ULL);
}

std::uint64_t rotate_right_masked(std::uint64_t value, unsigned amount,
                                  unsigned width) {
    const unsigned normalized = amount % width;
    const std::uint64_t mask = bitmask_for_width(width);
    value &= mask;
    if (normalized == 0) {
        return value;
    }
    if (width == 64U) {
        return ((value >> normalized) | (value << (64U - normalized))) & mask;
    }
    return ((value >> normalized) | (value << (width - normalized))) & mask;
}

std::uint64_t replicate_element_pattern(std::uint64_t element, unsigned element_width,
                                        unsigned reg_width) {
    const std::uint64_t masked_element = element & bitmask_for_width(element_width);
    if (element_width == reg_width) {
        return masked_element;
    }
    std::uint64_t value = 0;
    for (unsigned offset = 0; offset < reg_width; offset += element_width) {
        value |= masked_element << offset;
    }
    return value & bitmask_for_width(reg_width);
}

bool can_encode_logical_immediate(std::uint64_t value, unsigned reg_width) {
    const std::uint64_t mask = bitmask_for_width(reg_width);
    value &= mask;
    if (value == 0 || value == mask) {
        return false;
    }

    for (unsigned element_width = 2; element_width <= reg_width; element_width <<= 1U) {
        const std::uint64_t element_mask = bitmask_for_width(element_width);
        const std::uint64_t element = value & element_mask;
        if (replicate_element_pattern(element, element_width, reg_width) != value) {
            continue;
        }
        for (unsigned ones = 1; ones < element_width; ++ones) {
            const std::uint64_t one_run = bitmask_for_width(ones);
            for (unsigned rotation = 0; rotation < element_width; ++rotation) {
                if (rotate_right_masked(one_run, rotation, element_width) ==
                    element) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool is_zero_general_value(const CoreIrValue *value) {
    if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
        return true;
    }
    const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
    return constant_int != nullptr && constant_int->get_value() == 0;
}

bool is_compare_only_used_by_select(const CoreIrCompareInst &compare,
                                    const CoreIrSelectInst &select) {
    const auto &uses = compare.get_uses();
    return uses.size() == 1 && uses.front().get_user() == &select &&
           uses.front().get_operand_index() == 0;
}

bool is_compare_only_used_by_cond_jump(const CoreIrCompareInst &compare) {
    const auto &uses = compare.get_uses();
    return uses.size() == 1 && uses.front().get_operand_index() == 0 &&
           dynamic_cast<const CoreIrCondJumpInst *>(uses.front().get_user()) !=
               nullptr;
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                "AArch64 native backend: " + message);
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

bool compare_uses_foldable_tst_mask(const CoreIrCompareInst &compare,
                                    const CoreIrBinaryInst *&and_binary,
                                    std::uint64_t &mask_value) {
    if ((compare.get_predicate() != CoreIrComparePredicate::Equal &&
         compare.get_predicate() != CoreIrComparePredicate::NotEqual) ||
        !is_zero_general_value(compare.get_rhs())) {
        return false;
    }
    and_binary = dynamic_cast<const CoreIrBinaryInst *>(compare.get_lhs());
    if (and_binary == nullptr ||
        and_binary->get_binary_opcode() != CoreIrBinaryOpcode::And) {
        return false;
    }

    std::optional<std::uint64_t> candidate_mask =
        try_get_nonnegative_integer_immediate_value(and_binary->get_rhs());
    if (!candidate_mask.has_value()) {
        candidate_mask =
            try_get_nonnegative_integer_immediate_value(and_binary->get_lhs());
    }
    if (!candidate_mask.has_value()) {
        return false;
    }
    mask_value = *candidate_mask;
    return true;
}

std::optional<unsigned> get_constant_vector_lane_index(const CoreIrValue *value) {
    const std::optional<std::uint64_t> immediate =
        try_get_nonnegative_integer_immediate_value(value);
    if (!immediate.has_value() || *immediate >= 4) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*immediate);
}

void emit_v4i32_copy(AArch64MachineBlock &machine_block,
                     const AArch64VirtualReg &dst_reg,
                     const AArch64VirtualReg &src_reg) {
    if (dst_reg.get_id() == src_reg.get_id()) {
        return;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {AArch64MachineOperand::def_vector_reg(dst_reg, 16, 'b'),
                AArch64MachineOperand::use_vector_reg(src_reg, 16, 'b')}));
}

std::optional<const char *> v4i32_binary_mnemonic(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
        return "add";
    case CoreIrBinaryOpcode::Sub:
        return "sub";
    case CoreIrBinaryOpcode::Mul:
        return "mul";
    case CoreIrBinaryOpcode::Shl:
        return "ushl";
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<const char *> v4i32_bitwise_mnemonic(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::And:
        return "and";
    case CoreIrBinaryOpcode::Or:
        return "orr";
    case CoreIrBinaryOpcode::Xor:
        return "eor";
    default:
        return std::nullopt;
    }
}

std::optional<const char *> v4i32_scalar_div_mnemonic(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::SRem:
        return "sdiv";
    case CoreIrBinaryOpcode::UDiv:
    case CoreIrBinaryOpcode::URem:
        return "udiv";
    default:
        return std::nullopt;
    }
}

bool emit_v4i32_lane_divrem(AArch64MachineBlock &machine_block,
                            CoreIrBinaryOpcode opcode,
                            const AArch64VirtualReg &dst_reg,
                            const AArch64VirtualReg &lhs_reg,
                            const AArch64VirtualReg &rhs_reg,
                            AArch64MachineFunction &function) {
    const std::optional<const char *> div_mnemonic =
        v4i32_scalar_div_mnemonic(opcode);
    if (!div_mnemonic.has_value()) {
        return false;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "movi", {AArch64MachineOperand::def_vector_reg(dst_reg, 2, 'd'),
                 AArch64MachineOperand::immediate("#0")}));
    const bool needs_remainder = opcode == CoreIrBinaryOpcode::SRem ||
                                 opcode == CoreIrBinaryOpcode::URem;
    for (unsigned lane = 0; lane < 4; ++lane) {
        const AArch64VirtualReg lhs_lane =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const AArch64VirtualReg rhs_lane =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const AArch64VirtualReg quotient =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand_as(lhs_lane, false),
                    AArch64MachineOperand::use_vector_lane(lhs_reg, 's', lane)}));
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand_as(rhs_lane, false),
                    AArch64MachineOperand::use_vector_lane(rhs_reg, 's', lane)}));
        machine_block.append_instruction(AArch64MachineInstr(
            *div_mnemonic,
            {def_vreg_operand_as(quotient, false),
             use_vreg_operand_as(lhs_lane, false),
             use_vreg_operand_as(rhs_lane, false)}));
        AArch64VirtualReg result_lane = quotient;
        if (needs_remainder) {
            result_lane =
                function.create_virtual_reg(AArch64VirtualRegKind::General32);
            machine_block.append_instruction(AArch64MachineInstr(
                "msub",
                {def_vreg_operand_as(result_lane, false),
                 use_vreg_operand_as(quotient, false),
                 use_vreg_operand_as(rhs_lane, false),
                 use_vreg_operand_as(lhs_lane, false)}));
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {AArch64MachineOperand::def_vector_lane(dst_reg, 's', lane),
                    use_vreg_operand_as(result_lane, false)}));
    }
    return true;
}

} // namespace

bool emit_binary_instruction(AArch64MachineBlock &machine_block,
                             AArch64ScalarLoweringContext &context,
                             const CoreIrBinaryInst &binary,
                             AArch64MachineFunction &function) {
    if (binary.get_binary_opcode() == CoreIrBinaryOpcode::And &&
        binary.get_uses().size() == 1 &&
        binary.get_uses().front().get_operand_index() == 0) {
        if (const auto *compare =
                dynamic_cast<const CoreIrCompareInst *>(
                    binary.get_uses().front().get_user());
            compare != nullptr) {
            const CoreIrBinaryInst *folded_and = nullptr;
            std::uint64_t mask_value = 0;
            if (compare_uses_foldable_tst_mask(*compare, folded_and, mask_value) &&
                folded_and == &binary) {
                const bool compare_is_fused =
                    (compare->get_uses().size() == 1 &&
                     ((dynamic_cast<const CoreIrSelectInst *>(
                           compare->get_uses().front().get_user()) != nullptr) ||
                      (dynamic_cast<const CoreIrCondJumpInst *>(
                           compare->get_uses().front().get_user()) != nullptr)));
                if (compare_is_fused) {
                    return true;
                }
            }
        }
    }

    AArch64VirtualReg dst_reg;
    if (!context.require_canonical_vreg(&binary, dst_reg)) {
        return false;
    }

    if (is_i32x4_vector_type(binary.get_type())) {
        const std::optional<const char *> mnemonic =
            v4i32_binary_mnemonic(binary.get_binary_opcode());
        AArch64VirtualReg lhs_reg;
        AArch64VirtualReg rhs_reg;
        if (!context.ensure_value_in_vreg(machine_block, binary.get_lhs(), lhs_reg) ||
            !context.ensure_value_in_vreg(machine_block, binary.get_rhs(), rhs_reg)) {
            return false;
        }
        if (mnemonic.has_value()) {
            machine_block.append_instruction(AArch64MachineInstr(
                *mnemonic,
                {AArch64MachineOperand::def_vector_reg(dst_reg, 4, 's'),
                 AArch64MachineOperand::use_vector_reg(lhs_reg, 4, 's'),
                 AArch64MachineOperand::use_vector_reg(rhs_reg, 4, 's')}));
            return true;
        }
        if (const std::optional<const char *> bitwise_mnemonic =
                v4i32_bitwise_mnemonic(binary.get_binary_opcode());
            bitwise_mnemonic.has_value()) {
            machine_block.append_instruction(AArch64MachineInstr(
                *bitwise_mnemonic,
                {AArch64MachineOperand::def_vector_reg(dst_reg, 16, 'b'),
                 AArch64MachineOperand::use_vector_reg(lhs_reg, 16, 'b'),
                 AArch64MachineOperand::use_vector_reg(rhs_reg, 16, 'b')}));
            return true;
        }
        if (emit_v4i32_lane_divrem(machine_block, binary.get_binary_opcode(),
                                   dst_reg, lhs_reg, rhs_reg, function)) {
            return true;
        }
        add_backend_error(context.diagnostic_engine(),
                          "unsupported <4 x i32> binary opcode");
        return false;
    }

    if (!is_float_type(binary.get_type())) {
        const auto try_emit_add_sub_immediate =
            [&](const CoreIrValue *reg_value, const CoreIrValue *imm_value,
                const char *opcode) -> std::optional<bool> {
            const auto immediate =
                try_get_nonnegative_integer_immediate_value(imm_value);
            if (!immediate.has_value() ||
                !can_encode_add_sub_immediate(*immediate)) {
                return std::nullopt;
            }
            AArch64VirtualReg reg;
            if (!context.ensure_value_in_vreg(machine_block, reg_value, reg)) {
                return true;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                opcode,
                {def_vreg_operand(dst_reg), use_vreg_operand(reg),
                 AArch64MachineOperand::immediate("#" +
                                                  std::to_string(*immediate))}));
            return true;
        };
        const auto try_emit_logical_immediate =
            [&](const CoreIrValue *reg_value, const CoreIrValue *imm_value,
                const char *opcode) -> std::optional<bool> {
            const auto immediate =
                try_get_nonnegative_integer_immediate_value(imm_value);
            if (!immediate.has_value() ||
                !can_encode_logical_immediate(*immediate,
                                              dst_reg.get_use_64bit() ? 64U : 32U)) {
                return std::nullopt;
            }
            AArch64VirtualReg reg;
            if (!context.ensure_value_in_vreg(machine_block, reg_value, reg)) {
                return true;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                opcode,
                {def_vreg_operand(dst_reg), use_vreg_operand(reg),
                 AArch64MachineOperand::immediate("#" +
                                                  std::to_string(*immediate))}));
            return true;
        };
        const auto try_emit_mul_by_shifted_add =
            [&](const CoreIrValue *reg_value, const CoreIrValue *imm_value)
                -> std::optional<bool> {
            const auto immediate =
                try_get_nonnegative_integer_immediate_value(imm_value);
            if (!immediate.has_value() || *immediate != 3) {
                return std::nullopt;
            }
            AArch64VirtualReg reg;
            if (!context.ensure_value_in_vreg(machine_block, reg_value, reg)) {
                return true;
            }
            if (!reg.is_general()) {
                return std::nullopt;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "add",
                {def_vreg_operand(dst_reg), use_vreg_operand(reg),
                 use_vreg_operand(reg),
                 AArch64MachineOperand::shift(AArch64ShiftKind::Lsl, 1)}));
            return true;
        };
        const auto try_emit_shift_immediate =
            [&](const CoreIrValue *reg_value, const CoreIrValue *imm_value,
                const char *opcode) -> std::optional<bool> {
            const auto immediate =
                try_get_nonnegative_integer_immediate_value(imm_value);
            if (!immediate.has_value() ||
                !can_encode_shift_immediate(*immediate, dst_reg.get_use_64bit())) {
                return std::nullopt;
            }
            AArch64VirtualReg reg;
            if (!context.ensure_value_in_vreg(machine_block, reg_value, reg)) {
                return true;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                opcode,
                {def_vreg_operand(dst_reg), use_vreg_operand(reg),
                 AArch64MachineOperand::immediate("#" +
                                                  std::to_string(*immediate))}));
            return true;
        };
        const auto try_emit_zero_minus_immediate =
            [&]() -> std::optional<bool> {
            if (!is_zero_general_value(binary.get_lhs())) {
                return std::nullopt;
            }
            const auto immediate =
                try_get_nonnegative_integer_immediate_value(binary.get_rhs());
            if (!immediate.has_value()) {
                return std::nullopt;
            }
            emit_integer_constant(machine_block, dst_reg,
                                  static_cast<std::uint64_t>(0) - *immediate);
            return true;
        };

        switch (binary.get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            if (const auto emitted =
                    try_emit_add_sub_immediate(binary.get_lhs(),
                                               binary.get_rhs(), "add");
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_add_sub_immediate(binary.get_rhs(),
                                               binary.get_lhs(), "add");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::Sub:
            if (const auto emitted = try_emit_zero_minus_immediate();
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_add_sub_immediate(binary.get_lhs(),
                                               binary.get_rhs(), "sub");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::Mul:
            if (const auto emitted =
                    try_emit_mul_by_shifted_add(binary.get_lhs(),
                                                binary.get_rhs());
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_mul_by_shifted_add(binary.get_rhs(),
                                                binary.get_lhs());
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::Shl:
            if (const auto emitted =
                    try_emit_shift_immediate(binary.get_lhs(), binary.get_rhs(),
                                             "lsl");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::LShr:
            if (const auto emitted =
                    try_emit_shift_immediate(binary.get_lhs(), binary.get_rhs(),
                                             "lsr");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::AShr:
            if (const auto emitted =
                    try_emit_shift_immediate(binary.get_lhs(), binary.get_rhs(),
                                             "asr");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::And:
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_lhs(),
                                               binary.get_rhs(), "and");
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_rhs(),
                                               binary.get_lhs(), "and");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::Or:
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_lhs(),
                                               binary.get_rhs(), "orr");
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_rhs(),
                                               binary.get_lhs(), "orr");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        case CoreIrBinaryOpcode::Xor:
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_lhs(),
                                               binary.get_rhs(), "eor");
                emitted.has_value()) {
                return *emitted;
            }
            if (const auto emitted =
                    try_emit_logical_immediate(binary.get_rhs(),
                                               binary.get_lhs(), "eor");
                emitted.has_value()) {
                return *emitted;
            }
            break;
        default:
            break;
        }
    }

    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    if (!context.ensure_value_in_vreg(machine_block, binary.get_lhs(), lhs_reg) ||
        !context.ensure_value_in_vreg(machine_block, binary.get_rhs(), rhs_reg)) {
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
    if (compare.get_uses().size() == 1) {
        const CoreIrUse &use = compare.get_uses().front();
        if (use.get_operand_index() == 0 &&
            dynamic_cast<const CoreIrSelectInst *>(use.get_user()) != nullptr) {
            return true;
        }
    }
    AArch64VirtualReg dst_reg;
    if (!context.require_canonical_vreg(&compare, dst_reg)) {
        return false;
    }

    if (!is_float_type(compare.get_lhs()->get_type())) {
        const auto rhs_immediate =
            try_get_nonnegative_integer_immediate_value(compare.get_rhs());
        if (rhs_immediate.has_value() &&
            can_encode_compare_immediate(*rhs_immediate)) {
            AArch64VirtualReg lhs_reg;
            if (!context.ensure_value_in_vreg(machine_block, compare.get_lhs(),
                                              lhs_reg)) {
                return false;
            }
            return emit_non_float128_compare_immediate(
                machine_block, compare, lhs_reg,
                static_cast<long long>(*rhs_immediate), dst_reg, function);
        }
    }

    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    if (!context.ensure_value_in_vreg(machine_block, compare.get_lhs(), lhs_reg) ||
        !context.ensure_value_in_vreg(machine_block, compare.get_rhs(), rhs_reg)) {
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
    AArch64VirtualReg dst_reg;
    if (!context.require_canonical_vreg(&select, dst_reg)) {
        return false;
    }

    if (dst_reg.is_floating_point()) {
        return false;
    }

    AArch64MachineOperand true_operand = AArch64MachineOperand::zero_register(
        dst_reg.get_use_64bit());
    if (!is_zero_general_value(select.get_true_value())) {
        AArch64VirtualReg true_reg;
        if (!context.ensure_value_in_vreg(machine_block, select.get_true_value(),
                                          true_reg)) {
            return false;
        }
        true_operand = use_vreg_operand(true_reg);
    }

    AArch64MachineOperand false_operand = AArch64MachineOperand::zero_register(
        dst_reg.get_use_64bit());
    if (!is_zero_general_value(select.get_false_value())) {
        AArch64VirtualReg false_reg;
        if (!context.ensure_value_in_vreg(machine_block, select.get_false_value(),
                                          false_reg)) {
            return false;
        }
        false_operand = use_vreg_operand(false_reg);
    }

    if (const auto *compare =
            dynamic_cast<const CoreIrCompareInst *>(select.get_condition());
        compare != nullptr && is_compare_only_used_by_select(*compare, select) &&
        !is_float_type(compare->get_lhs()->get_type())) {
        if (compare->get_predicate() == CoreIrComparePredicate::Equal ||
            compare->get_predicate() == CoreIrComparePredicate::NotEqual) {
            if (is_zero_general_value(compare->get_rhs())) {
                if (const auto *and_binary =
                        dynamic_cast<const CoreIrBinaryInst *>(compare->get_lhs());
                    and_binary != nullptr &&
                    and_binary->get_binary_opcode() == CoreIrBinaryOpcode::And) {
                    const CoreIrValue *tested_value = nullptr;
                    std::optional<std::uint64_t> mask_value =
                        try_get_nonnegative_integer_immediate_value(
                            and_binary->get_rhs());
                    if (mask_value.has_value()) {
                        tested_value = and_binary->get_lhs();
                    } else {
                        mask_value = try_get_nonnegative_integer_immediate_value(
                            and_binary->get_lhs());
                        tested_value = mask_value.has_value() ? and_binary->get_rhs()
                                                              : nullptr;
                    }
                    if (tested_value != nullptr && mask_value.has_value() &&
                        can_encode_logical_immediate(*mask_value,
                                                     dst_reg.get_use_64bit() ? 64U
                                                                             : 32U)) {
                        AArch64VirtualReg tested_reg;
                        if (!context.ensure_value_in_vreg(machine_block, tested_value,
                                                          tested_reg)) {
                            return false;
                        }
                        machine_block.append_instruction(AArch64MachineInstr(
                            "tst",
                            {use_vreg_operand(tested_reg),
                             AArch64MachineOperand::immediate(
                                 "#" + std::to_string(*mask_value))}));
                        machine_block.append_instruction(AArch64MachineInstr(
                            "csel", {def_vreg_operand(dst_reg), std::move(true_operand),
                                     std::move(false_operand),
                                     condition_code_operand(integer_condition_code(
                                         compare->get_predicate()))}));
                        return true;
                    }
                }
            }
        }

        AArch64VirtualReg lhs_reg;
        if (!context.ensure_value_in_vreg(machine_block, compare->get_lhs(),
                                          lhs_reg)) {
            return false;
        }
        if (const auto rhs_immediate =
                try_get_nonnegative_integer_immediate_value(compare->get_rhs());
            rhs_immediate.has_value() &&
            can_encode_compare_immediate(*rhs_immediate)) {
            machine_block.append_instruction(AArch64MachineInstr(
                "cmp", {use_vreg_operand(lhs_reg),
                        AArch64MachineOperand::immediate(
                            "#" + std::to_string(*rhs_immediate))}));
        } else {
            AArch64VirtualReg rhs_reg;
            if (!context.ensure_value_in_vreg(machine_block, compare->get_rhs(),
                                              rhs_reg)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "cmp", {use_vreg_operand(lhs_reg), use_vreg_operand(rhs_reg)}));
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "csel", {def_vreg_operand(dst_reg), std::move(true_operand),
                     std::move(false_operand),
                     condition_code_operand(
                         integer_condition_code(compare->get_predicate()))}));
        return true;
    }

    AArch64VirtualReg condition_reg;
    if (!context.ensure_value_in_vreg(machine_block, select.get_condition(),
                                      condition_reg)) {
        return false;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "cmp", {use_vreg_operand(condition_reg),
                AArch64MachineOperand::immediate("#0")}));
    machine_block.append_instruction(AArch64MachineInstr(
        "csel", {def_vreg_operand(dst_reg), std::move(true_operand),
                 std::move(false_operand), condition_code_operand("ne")}));
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

bool emit_extract_element_instruction(AArch64MachineBlock &machine_block,
                                      AArch64ScalarLoweringContext &context,
                                      const CoreIrExtractElementInst &extract,
                                      AArch64MachineFunction &function) {
    (void)function;
    if (!is_i32x4_vector_type(extract.get_vector_value()->get_type())) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported vector extract type in AArch64 native backend");
        return false;
    }
    const std::optional<unsigned> lane =
        get_constant_vector_lane_index(extract.get_index());
    if (!lane.has_value()) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported dynamic <4 x i32> extractelement index");
        return false;
    }

    AArch64VirtualReg vector_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, extract.get_vector_value(),
                                      vector_reg) ||
        !context.require_canonical_vreg(&extract, dst_reg)) {
        return false;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand_as(dst_reg, false),
                AArch64MachineOperand::use_vector_lane(vector_reg, 's', *lane)}));
    return true;
}

bool emit_insert_element_instruction(AArch64MachineBlock &machine_block,
                                     AArch64ScalarLoweringContext &context,
                                     const CoreIrInsertElementInst &insert,
                                     AArch64MachineFunction &function) {
    (void)function;
    if (!is_i32x4_vector_type(insert.get_type())) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported vector insert type in AArch64 native backend");
        return false;
    }
    const std::optional<unsigned> lane =
        get_constant_vector_lane_index(insert.get_index());
    if (!lane.has_value()) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported dynamic <4 x i32> insertelement index");
        return false;
    }

    AArch64VirtualReg source_reg;
    AArch64VirtualReg element_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, insert.get_vector_value(),
                                      source_reg) ||
        !context.ensure_value_in_vreg(machine_block, insert.get_element_value(),
                                      element_reg) ||
        !context.require_canonical_vreg(&insert, dst_reg)) {
        return false;
    }
    emit_v4i32_copy(machine_block, dst_reg, source_reg);
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {AArch64MachineOperand::def_vector_lane(dst_reg, 's', *lane),
                use_vreg_operand_as(element_reg, false)}));
    return true;
}

bool emit_shuffle_vector_instruction(AArch64MachineBlock &machine_block,
                                     AArch64ScalarLoweringContext &context,
                                     const CoreIrShuffleVectorInst &shuffle,
                                     AArch64MachineFunction &function) {
    (void)function;
    if (!is_i32x4_vector_type(shuffle.get_type()) ||
        !is_i32x4_vector_type(shuffle.get_lhs()->get_type()) ||
        shuffle.get_mask_count() != 4) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported <4 x i32> shufflevector shape");
        return false;
    }

    std::vector<unsigned> lanes;
    lanes.reserve(4);
    for (std::size_t index = 0; index < shuffle.get_mask_count(); ++index) {
        const std::optional<unsigned> lane =
            get_constant_vector_lane_index(shuffle.get_mask_value(index));
        if (!lane.has_value()) {
            add_backend_error(context.diagnostic_engine(),
                              "unsupported <4 x i32> shufflevector mask");
            return false;
        }
        lanes.push_back(*lane);
    }

    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, shuffle.get_lhs(), lhs_reg) ||
        !context.require_canonical_vreg(&shuffle, dst_reg)) {
        return false;
    }

    if (lanes == std::vector<unsigned>{0, 1, 2, 3}) {
        emit_v4i32_copy(machine_block, dst_reg, lhs_reg);
        return true;
    }
    const bool is_splat =
        std::all_of(lanes.begin(), lanes.end(),
                    [&lanes](unsigned lane) { return lane == lanes.front(); });
    if (is_splat) {
        machine_block.append_instruction(AArch64MachineInstr(
            "dup", {AArch64MachineOperand::def_vector_reg(dst_reg, 4, 's'),
                    AArch64MachineOperand::use_vector_lane(lhs_reg, 's',
                                                           lanes.front())}));
        return true;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "movi", {AArch64MachineOperand::def_vector_reg(dst_reg, 2, 'd'),
                 AArch64MachineOperand::immediate("#0")}));
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {AArch64MachineOperand::def_vector_lane(
                        dst_reg, 's', static_cast<unsigned>(lane)),
                    AArch64MachineOperand::use_vector_lane(lhs_reg, 's',
                                                           lanes[lane])}));
    }
    return true;
}

bool emit_vector_reduce_add_instruction(AArch64MachineBlock &machine_block,
                                        AArch64ScalarLoweringContext &context,
                                        const CoreIrVectorReduceAddInst &reduce,
                                        AArch64MachineFunction &function) {
    if (!is_i32x4_vector_type(reduce.get_vector_value()->get_type())) {
        add_backend_error(context.diagnostic_engine(),
                          "unsupported vector reduce.add operand type");
        return false;
    }

    AArch64VirtualReg vector_reg;
    AArch64VirtualReg dst_reg;
    if (!context.ensure_value_in_vreg(machine_block, reduce.get_vector_value(),
                                      vector_reg) ||
        !context.require_canonical_vreg(&reduce, dst_reg)) {
        return false;
    }

    const AArch64VirtualReg reduced_scalar =
        function.create_virtual_reg(AArch64VirtualRegKind::Float32);
    machine_block.append_instruction(AArch64MachineInstr(
        "addv", {def_vreg_operand(reduced_scalar),
                 AArch64MachineOperand::use_vector_reg(vector_reg, 4, 's')}));

    const AArch64VirtualReg reduced_integer =
        function.create_virtual_reg(AArch64VirtualRegKind::General32);
    machine_block.append_instruction(AArch64MachineInstr(
        "fmov", {def_vreg_operand_as(reduced_integer, false),
                 use_vreg_operand(reduced_scalar)}));

    if (CoreIrValue *start_value = reduce.get_start_value();
        start_value != nullptr) {
        AArch64VirtualReg start_reg;
        if (!context.ensure_value_in_vreg(machine_block, start_value, start_reg)) {
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "add", {def_vreg_operand_as(dst_reg, false),
                    use_vreg_operand_as(start_reg, false),
                    use_vreg_operand_as(reduced_integer, false)}));
        return true;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand_as(dst_reg, false),
                use_vreg_operand_as(reduced_integer, false)}));
    return true;
}

} // namespace sysycc
