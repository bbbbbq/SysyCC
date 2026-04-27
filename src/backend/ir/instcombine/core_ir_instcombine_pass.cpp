#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using namespace detail;

std::size_t g_instcombine_temp_counter = 0;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

std::string next_instcombine_name(const std::string &prefix) {
    return prefix + std::to_string(g_instcombine_temp_counter++);
}

bool erase_instruction_if_dead(CoreIrBasicBlock &block,
                               CoreIrInstruction *instruction) {
    if (instruction == nullptr ||
        !get_core_ir_instruction_effect(*instruction).is_pure_value ||
        !instruction->get_uses().empty()) {
        return false;
    }
    return erase_instruction(block, instruction);
}

void erase_dead_instruction_tree(CoreIrValue *value) {
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr ||
        !get_core_ir_instruction_effect(*instruction).is_pure_value ||
        !instruction->get_uses().empty()) {
        return;
    }

    std::vector<CoreIrValue *> operands = instruction->get_operands();
    CoreIrBasicBlock *parent = instruction->get_parent();
    if (parent == nullptr || !erase_instruction(*parent, instruction)) {
        return;
    }

    for (CoreIrValue *operand : operands) {
        erase_dead_instruction_tree(operand);
    }
}

void erase_dead_address_chain(CoreIrBasicBlock &block, CoreIrValue *value) {
    CoreIrInstruction *instruction = dynamic_cast<CoreIrInstruction *>(value);
    while (instruction != nullptr &&
           get_core_ir_instruction_effect(*instruction).is_pure_value &&
           instruction->get_uses().empty()) {
        CoreIrValue *next = nullptr;
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
            gep != nullptr) {
            next = gep->get_base();
        }
        if (!erase_instruction(block, instruction)) {
            break;
        }
        instruction = dynamic_cast<CoreIrInstruction *>(next);
    }
}

CoreIrComparePredicate invert_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return CoreIrComparePredicate::NotEqual;
    case CoreIrComparePredicate::NotEqual:
        return CoreIrComparePredicate::Equal;
    case CoreIrComparePredicate::SignedLess:
        return CoreIrComparePredicate::SignedGreaterEqual;
    case CoreIrComparePredicate::SignedLessEqual:
        return CoreIrComparePredicate::SignedGreater;
    case CoreIrComparePredicate::SignedGreater:
        return CoreIrComparePredicate::SignedLessEqual;
    case CoreIrComparePredicate::SignedGreaterEqual:
        return CoreIrComparePredicate::SignedLess;
    case CoreIrComparePredicate::UnsignedLess:
        return CoreIrComparePredicate::UnsignedGreaterEqual;
    case CoreIrComparePredicate::UnsignedLessEqual:
        return CoreIrComparePredicate::UnsignedGreater;
    case CoreIrComparePredicate::UnsignedGreater:
        return CoreIrComparePredicate::UnsignedLessEqual;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return CoreIrComparePredicate::UnsignedLess;
    }
    return predicate;
}

CoreIrComparePredicate flip_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
    case CoreIrComparePredicate::NotEqual:
        return predicate;
    case CoreIrComparePredicate::SignedLess:
        return CoreIrComparePredicate::SignedGreater;
    case CoreIrComparePredicate::SignedLessEqual:
        return CoreIrComparePredicate::SignedGreaterEqual;
    case CoreIrComparePredicate::SignedGreater:
        return CoreIrComparePredicate::SignedLess;
    case CoreIrComparePredicate::SignedGreaterEqual:
        return CoreIrComparePredicate::SignedLessEqual;
    case CoreIrComparePredicate::UnsignedLess:
        return CoreIrComparePredicate::UnsignedGreater;
    case CoreIrComparePredicate::UnsignedLessEqual:
        return CoreIrComparePredicate::UnsignedGreaterEqual;
    case CoreIrComparePredicate::UnsignedGreater:
        return CoreIrComparePredicate::UnsignedLess;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return CoreIrComparePredicate::UnsignedLessEqual;
    }
    return predicate;
}

CoreIrCompareInst *create_branch_compare(CoreIrContext &context,
                                         CoreIrBasicBlock &block,
                                         CoreIrInstruction *anchor,
                                         CoreIrComparePredicate predicate,
                                         CoreIrValue *lhs, CoreIrValue *rhs,
                                         const SourceSpan &source_span) {
    const auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    auto instruction = std::make_unique<CoreIrCompareInst>(
        predicate, i1_type, next_instcombine_name("instcombine.cond."), lhs, rhs);
    instruction->set_source_span(source_span);
    return static_cast<CoreIrCompareInst *>(
        insert_instruction_before(block, anchor, std::move(instruction)));
}

CoreIrCompareInst *replace_compare_instruction(CoreIrBasicBlock &block,
                                               CoreIrCompareInst &compare,
                                               CoreIrComparePredicate predicate,
                                               CoreIrValue *lhs,
                                               CoreIrValue *rhs) {
    auto replacement = std::make_unique<CoreIrCompareInst>(
        predicate, compare.get_type(), compare.get_name(), lhs, rhs);
    replacement->set_source_span(compare.get_source_span());
    return static_cast<CoreIrCompareInst *>(
        replace_instruction(block, &compare, std::move(replacement)));
}

CoreIrCompareInst *create_integer_zero_compare(CoreIrContext &context,
                                               CoreIrBasicBlock &block,
                                               CoreIrInstruction *anchor,
                                               CoreIrComparePredicate predicate,
                                               CoreIrValue *value,
                                               const SourceSpan &source_span) {
    if (value == nullptr || as_integer_type(value->get_type()) == nullptr) {
        return nullptr;
    }
    CoreIrValue *zero =
        context.create_constant<CoreIrConstantInt>(value->get_type(), 0);
    return create_branch_compare(context, block, anchor, predicate, value, zero,
                                 source_span);
}

CoreIrCompareInst *create_pointer_null_compare(CoreIrContext &context,
                                               CoreIrBasicBlock &block,
                                               CoreIrInstruction *anchor,
                                               CoreIrComparePredicate predicate,
                                               CoreIrValue *value,
                                               const SourceSpan &source_span) {
    if (value == nullptr || !is_pointer_type(value->get_type())) {
        return nullptr;
    }
    CoreIrValue *null =
        context.create_constant<CoreIrConstantNull>(value->get_type());
    return create_branch_compare(context, block, anchor, predicate, value, null,
                                 source_span);
}

CoreIrCompareInst *create_float_zero_compare(CoreIrContext &context,
                                             CoreIrBasicBlock &block,
                                             CoreIrInstruction *anchor,
                                             CoreIrComparePredicate predicate,
                                             CoreIrValue *value,
                                             const SourceSpan &source_span) {
    if (value == nullptr || !is_float_type(value->get_type())) {
        return nullptr;
    }
    CoreIrValue *zero =
        context.create_constant<CoreIrConstantFloat>(value->get_type(), "0.0");
    return create_branch_compare(context, block, anchor, predicate, value, zero,
                                 source_span);
}

CoreIrCompareInst *create_truthiness_compare(CoreIrContext &context,
                                             CoreIrBasicBlock &block,
                                             CoreIrInstruction *anchor,
                                             CoreIrComparePredicate predicate,
                                             CoreIrValue *value,
                                             const SourceSpan &source_span) {
    if (value == nullptr || value->get_type() == nullptr) {
        return nullptr;
    }
    if (as_integer_type(value->get_type()) != nullptr) {
        return create_integer_zero_compare(context, block, anchor, predicate, value,
                                           source_span);
    }
    if (is_pointer_type(value->get_type())) {
        return create_pointer_null_compare(context, block, anchor, predicate, value,
                                           source_span);
    }
    if (is_float_type(value->get_type())) {
        return create_float_zero_compare(context, block, anchor, predicate, value,
                                         source_span);
    }
    return nullptr;
}

struct BooleanCompareMatch {
    CoreIrCompareInst *compare = nullptr;
    bool invert = false;
};

std::optional<BooleanCompareMatch> match_boolean_compare(CoreIrValue *value) {
    if (value == nullptr) {
        return std::nullopt;
    }

    if (auto *cast = dynamic_cast<CoreIrCastInst *>(value); cast != nullptr) {
        if (!preserves_integer_truthiness(cast->get_cast_kind())) {
            return std::nullopt;
        }
        return match_boolean_compare(cast->get_operand());
    }

    if (auto *unary = dynamic_cast<CoreIrUnaryInst *>(value); unary != nullptr) {
        if (unary->get_unary_opcode() != CoreIrUnaryOpcode::LogicalNot) {
            return std::nullopt;
        }
        auto inner = match_boolean_compare(unary->get_operand());
        if (!inner.has_value()) {
            return std::nullopt;
        }
        inner->invert = !inner->invert;
        return inner;
    }

    auto *compare = dynamic_cast<CoreIrCompareInst *>(value);
    if (compare == nullptr) {
        return std::nullopt;
    }

    const auto predicate = compare->get_predicate();
    if ((predicate == CoreIrComparePredicate::Equal ||
         predicate == CoreIrComparePredicate::NotEqual) &&
        (is_zero_integer_constant(compare->get_lhs()) ||
         is_zero_integer_constant(compare->get_rhs()))) {
        CoreIrValue *other_side =
            is_zero_integer_constant(compare->get_lhs()) ? compare->get_rhs()
                                                         : compare->get_lhs();
        auto inner = match_boolean_compare(other_side);
        if (inner.has_value()) {
            if (predicate == CoreIrComparePredicate::Equal) {
                inner->invert = !inner->invert;
            }
            return inner;
        }
    }

    return BooleanCompareMatch{compare, false};
}

std::uint64_t get_integer_mask(std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    if (bit_width >= 64) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << bit_width) - 1;
}

bool has_sign_bit(std::uint64_t value, std::size_t bit_width) {
    if (bit_width == 0) {
        return false;
    }
    if (bit_width >= 64) {
        return (value & (std::uint64_t{1} << 63)) != 0;
    }
    return (value & (std::uint64_t{1} << (bit_width - 1))) != 0;
}

bool is_safe_nonnegative_integer_constant(const CoreIrConstantInt *constant) {
    if (constant == nullptr) {
        return false;
    }
    const std::size_t bit_width =
        get_integer_bit_width(constant->get_type()).value_or(0);
    if (bit_width == 0) {
        return false;
    }
    const std::uint64_t value = constant->get_value() & get_integer_mask(bit_width);
    return !has_sign_bit(value, bit_width);
}

bool is_safe_nonnegative_integer_value(const CoreIrType *type, std::uint64_t value) {
    const std::size_t bit_width = get_integer_bit_width(type).value_or(0);
    if (bit_width == 0) {
        return false;
    }
    value &= get_integer_mask(bit_width);
    return !has_sign_bit(value, bit_width);
}

CoreIrConstantInt *create_int_constant(CoreIrContext &context,
                                       const CoreIrType *type,
                                       std::uint64_t value) {
    return context.create_constant<CoreIrConstantInt>(type, value);
}

bool is_integer_constant_value(CoreIrValue *value, std::uint64_t expected) {
    const auto *constant = as_integer_constant(value);
    return constant != nullptr && constant->get_value() == expected;
}

CoreIrValue *match_signed_half_div_expansion(CoreIrValue *value) {
    auto *quotient = dynamic_cast<CoreIrBinaryInst *>(value);
    if (quotient == nullptr ||
        quotient->get_binary_opcode() != CoreIrBinaryOpcode::AShr ||
        !is_integer_constant_value(quotient->get_rhs(), 1)) {
        return nullptr;
    }

    auto *biased = dynamic_cast<CoreIrBinaryInst *>(quotient->get_lhs());
    if (biased == nullptr || biased->get_binary_opcode() != CoreIrBinaryOpcode::Add) {
        return nullptr;
    }

    auto *lhs_sign = dynamic_cast<CoreIrBinaryInst *>(biased->get_lhs());
    auto *rhs_sign = dynamic_cast<CoreIrBinaryInst *>(biased->get_rhs());
    CoreIrBinaryInst *sign = nullptr;
    CoreIrValue *dividend = nullptr;
    if (lhs_sign != nullptr) {
        sign = lhs_sign;
        dividend = biased->get_rhs();
    } else if (rhs_sign != nullptr) {
        sign = rhs_sign;
        dividend = biased->get_lhs();
    } else {
        return nullptr;
    }

    const std::optional<std::size_t> bit_width =
        get_integer_bit_width(dividend->get_type());
    if (!bit_width.has_value() || *bit_width == 0 ||
        sign->get_binary_opcode() != CoreIrBinaryOpcode::LShr ||
        sign->get_lhs() != dividend ||
        !is_integer_constant_value(sign->get_rhs(), *bit_width - 1)) {
        return nullptr;
    }

    return dividend;
}

CoreIrValue *match_signed_half_rem_expansion(CoreIrValue *value) {
    auto *remainder = dynamic_cast<CoreIrBinaryInst *>(value);
    if (remainder == nullptr ||
        remainder->get_binary_opcode() != CoreIrBinaryOpcode::Sub) {
        return nullptr;
    }

    auto *doubled = dynamic_cast<CoreIrBinaryInst *>(remainder->get_rhs());
    if (doubled == nullptr || doubled->get_binary_opcode() != CoreIrBinaryOpcode::Shl ||
        !is_integer_constant_value(doubled->get_rhs(), 1)) {
        return nullptr;
    }

    CoreIrValue *dividend = match_signed_half_div_expansion(doubled->get_lhs());
    if (dividend == nullptr || remainder->get_lhs() != dividend) {
        return nullptr;
    }
    return dividend;
}

CoreIrValue *match_parity_test_source(CoreIrValue *value) {
    if (auto *srem = dynamic_cast<CoreIrBinaryInst *>(value);
        srem != nullptr && srem->get_binary_opcode() == CoreIrBinaryOpcode::SRem &&
        is_integer_constant_value(srem->get_rhs(), 2)) {
        return srem->get_lhs();
    }
    return match_signed_half_rem_expansion(value);
}

CoreIrValue *match_even_odd_branch_source(CoreIrValue *value) {
    if (CoreIrValue *source = match_parity_test_source(value); source != nullptr) {
        return source;
    }

    auto *masked = dynamic_cast<CoreIrBinaryInst *>(value);
    if (masked == nullptr || masked->get_binary_opcode() != CoreIrBinaryOpcode::And) {
        return nullptr;
    }

    if (is_integer_constant_value(masked->get_lhs(), 1)) {
        return masked->get_rhs();
    }
    if (is_integer_constant_value(masked->get_rhs(), 1)) {
        return masked->get_lhs();
    }
    return nullptr;
}

std::vector<CoreIrBasicBlock *> collect_successors(const CoreIrBasicBlock &block) {
    std::vector<CoreIrBasicBlock *> successors;
    if (block.get_instructions().empty()) {
        return successors;
    }

    auto append_unique = [&successors](CoreIrBasicBlock *successor) {
        if (successor == nullptr ||
            std::find(successors.begin(), successors.end(), successor) !=
                successors.end()) {
            return;
        }
        successors.push_back(successor);
    };

    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        append_unique(jump->get_target_block());
    } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
               cond_jump != nullptr) {
        append_unique(cond_jump->get_true_block());
        append_unique(cond_jump->get_false_block());
    }

    return successors;
}

CoreIrBasicBlock *find_unique_predecessor(CoreIrBasicBlock &block) {
    CoreIrFunction *function = block.get_parent();
    if (function == nullptr) {
        return nullptr;
    }

    CoreIrBasicBlock *predecessor = nullptr;
    for (const auto &candidate_ptr : function->get_basic_blocks()) {
        CoreIrBasicBlock *candidate = candidate_ptr.get();
        if (candidate == nullptr || candidate == &block) {
            continue;
        }
        for (CoreIrBasicBlock *successor : collect_successors(*candidate)) {
            if (successor != &block) {
                continue;
            }
            if (predecessor != nullptr) {
                return nullptr;
            }
            predecessor = candidate;
        }
    }

    return predecessor;
}

bool block_is_even_successor_for_value(CoreIrBasicBlock &block, CoreIrValue *value) {
    CoreIrBasicBlock *predecessor = find_unique_predecessor(block);
    if (predecessor == nullptr || predecessor->get_instructions().empty()) {
        return false;
    }

    auto *cond_jump =
        dynamic_cast<CoreIrCondJumpInst *>(predecessor->get_instructions().back().get());
    if (cond_jump == nullptr) {
        return false;
    }

    auto *compare = dynamic_cast<CoreIrCompareInst *>(cond_jump->get_condition());
    if (compare == nullptr) {
        return false;
    }

    const CoreIrComparePredicate predicate = compare->get_predicate();
    if ((predicate != CoreIrComparePredicate::Equal &&
         predicate != CoreIrComparePredicate::NotEqual) ||
        !(is_zero_integer_constant(compare->get_lhs()) ||
          is_zero_integer_constant(compare->get_rhs()))) {
        return false;
    }

    CoreIrValue *parity_value =
        is_zero_integer_constant(compare->get_lhs()) ? compare->get_rhs()
                                                     : compare->get_lhs();
    CoreIrValue *source = match_even_odd_branch_source(parity_value);
    if (source != value) {
        return false;
    }

    const bool true_successor_is_even = predicate == CoreIrComparePredicate::Equal;
    if (cond_jump->get_true_block() == &block) {
        return true_successor_is_even;
    }
    if (cond_jump->get_false_block() == &block) {
        return !true_successor_is_even;
    }
    return false;
}

CoreIrValue *materialize_signed_div_by_two(CoreIrContext &context,
                                           CoreIrBasicBlock &block,
                                           CoreIrInstruction *anchor,
                                           CoreIrValue *value,
                                           const SourceSpan &source_span) {
    const std::optional<std::size_t> bit_width = get_integer_bit_width(value->get_type());
    if (!bit_width.has_value() || *bit_width == 0) {
        return nullptr;
    }

    CoreIrValue *shift_amount =
        create_int_constant(context, value->get_type(), *bit_width - 1);
    auto sign = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::LShr, value->get_type(),
        next_instcombine_name("instcombine.half.sign."), value, shift_amount);
    sign->set_source_span(source_span);
    CoreIrInstruction *sign_inst =
        insert_instruction_before(block, anchor, std::move(sign));
    if (sign_inst == nullptr) {
        return nullptr;
    }

    auto biased = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, value->get_type(),
        next_instcombine_name("instcombine.half.bias."), value, sign_inst);
    biased->set_source_span(source_span);
    CoreIrInstruction *biased_inst =
        insert_instruction_before(block, anchor, std::move(biased));
    if (biased_inst == nullptr) {
        return nullptr;
    }

    CoreIrValue *one = create_int_constant(context, value->get_type(), 1);
    auto quotient = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::AShr, value->get_type(),
        next_instcombine_name("instcombine.half.quot."), biased_inst, one);
    quotient->set_source_span(source_span);
    return insert_instruction_before(block, anchor, std::move(quotient));
}

CoreIrValue *materialize_signed_rem_by_two(CoreIrContext &context,
                                           CoreIrBasicBlock &block,
                                           CoreIrInstruction *anchor,
                                           CoreIrValue *value,
                                           const SourceSpan &source_span) {
    CoreIrValue *quotient =
        materialize_signed_div_by_two(context, block, anchor, value, source_span);
    if (quotient == nullptr) {
        return nullptr;
    }

    CoreIrValue *one = create_int_constant(context, value->get_type(), 1);
    auto doubled = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Shl, value->get_type(),
        next_instcombine_name("instcombine.half.twice."), quotient, one);
    doubled->set_source_span(source_span);
    CoreIrInstruction *doubled_inst =
        insert_instruction_before(block, anchor, std::move(doubled));
    if (doubled_inst == nullptr) {
        return nullptr;
    }

    auto remainder = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, value->get_type(),
        next_instcombine_name("instcombine.half.rem."), value, doubled_inst);
    remainder->set_source_span(source_span);
    return insert_instruction_before(block, anchor, std::move(remainder));
}

CoreIrValue *try_fold_phi(const CoreIrPhiInst &inst) {
    if (inst.get_incoming_count() == 0) {
        return nullptr;
    }

    CoreIrValue *replacement = nullptr;
    for (std::size_t index = 0; index < inst.get_incoming_count(); ++index) {
        CoreIrValue *incoming_value = inst.get_incoming_value(index);
        if (incoming_value == nullptr) {
            return nullptr;
        }
        if (replacement == nullptr) {
            replacement = incoming_value;
            continue;
        }
        if (replacement != incoming_value) {
            return nullptr;
        }
    }
    return replacement;
}

bool binary_opcode_is_commutative(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
    case CoreIrBinaryOpcode::Mul:
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
        return true;
    default:
        return false;
    }
}

CoreIrInstruction *find_first_non_phi_instruction(CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction != nullptr &&
            dynamic_cast<CoreIrPhiInst *>(instruction.get()) == nullptr) {
            return instruction.get();
        }
    }
    return nullptr;
}

bool value_is_available_for_phi_replacement(const CoreIrBasicBlock &block,
                                            const CoreIrPhiInst &phi,
                                            CoreIrValue *value) {
    if (value == nullptr || value == &phi) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    if (instruction->get_parent() != &block) {
        return true;
    }
    return dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr;
}

bool values_are_equivalent_for_phi_binop_hoist(const CoreIrValue *lhs,
                                               const CoreIrValue *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    std::string lhs_key;
    std::string rhs_key;
    append_value_key(lhs_key, lhs);
    append_value_key(rhs_key, rhs);
    return lhs_key == rhs_key;
}

bool phi_has_same_block_phi_users(const CoreIrPhiInst &phi) {
    for (const CoreIrUse &use : phi.get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user != nullptr && user->get_parent() == phi.get_parent() &&
            dynamic_cast<CoreIrPhiInst *>(user) != nullptr) {
            return true;
        }
    }
    return false;
}

struct IdenticalIncomingBinaryMatch {
    CoreIrBinaryOpcode opcode = CoreIrBinaryOpcode::Add;
    CoreIrValue *lhs = nullptr;
    CoreIrValue *rhs = nullptr;
    SourceSpan source_span;
    std::vector<CoreIrBinaryInst *> incoming_binaries;
};

std::optional<IdenticalIncomingBinaryMatch> match_identical_incoming_binary_phi(
    CoreIrBasicBlock &block, const CoreIrPhiInst &phi) {
    if (phi.get_incoming_count() < 2 || phi_has_same_block_phi_users(phi)) {
        return std::nullopt;
    }

    std::optional<IdenticalIncomingBinaryMatch> match;
    for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
        CoreIrBasicBlock *incoming_block = phi.get_incoming_block(index);
        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(phi.get_incoming_value(index));
        if (incoming_block == nullptr || binary == nullptr ||
            binary->get_parent() != incoming_block) {
            return std::nullopt;
        }

        if (!match.has_value()) {
            match = IdenticalIncomingBinaryMatch{
                binary->get_binary_opcode(), binary->get_lhs(), binary->get_rhs(),
                binary->get_source_span(), {binary}};
            continue;
        }

        if (match->opcode != binary->get_binary_opcode()) {
            return std::nullopt;
        }

        const bool same_operands =
            values_are_equivalent_for_phi_binop_hoist(match->lhs, binary->get_lhs()) &&
            values_are_equivalent_for_phi_binop_hoist(match->rhs, binary->get_rhs());
        const bool swapped_operands =
            binary_opcode_is_commutative(match->opcode) &&
            values_are_equivalent_for_phi_binop_hoist(match->lhs, binary->get_rhs()) &&
            values_are_equivalent_for_phi_binop_hoist(match->rhs, binary->get_lhs());
        if (!same_operands && !swapped_operands) {
            return std::nullopt;
        }

        match->incoming_binaries.push_back(binary);
    }

    if (!match.has_value() ||
        !value_is_available_for_phi_replacement(block, phi, match->lhs) ||
        !value_is_available_for_phi_replacement(block, phi, match->rhs)) {
        return std::nullopt;
    }

    return match;
}

bool simplify_phi_of_identical_incoming_binaries(CoreIrBasicBlock &block,
                                                 CoreIrPhiInst &phi) {
    std::optional<IdenticalIncomingBinaryMatch> match =
        match_identical_incoming_binary_phi(block, phi);
    if (!match.has_value()) {
        return false;
    }

    auto replacement = std::make_unique<CoreIrBinaryInst>(
        match->opcode, phi.get_type(),
        phi.get_name().empty() ? next_instcombine_name("instcombine.phi.bin.")
                               : phi.get_name(),
        match->lhs, match->rhs);
    replacement->set_source_span(match->source_span);
    CoreIrInstruction *anchor = find_first_non_phi_instruction(block);
    CoreIrInstruction *replacement_inst =
        insert_instruction_before(block, anchor, std::move(replacement));
    if (replacement_inst == nullptr) {
        return false;
    }

    phi.replace_all_uses_with(replacement_inst);
    if (!erase_instruction(block, &phi)) {
        return false;
    }

    std::unordered_set<CoreIrBinaryInst *> unique_binaries;
    for (CoreIrBinaryInst *binary : match->incoming_binaries) {
        if (binary != nullptr && unique_binaries.insert(binary).second) {
            erase_dead_instruction_tree(binary);
        }
    }
    return true;
}

CoreIrValue *try_fold_binary(CoreIrContext &context, const CoreIrBinaryInst &inst) {
    const auto *lhs = as_integer_constant(inst.get_lhs());
    const auto *rhs = as_integer_constant(inst.get_rhs());
    if (lhs == nullptr || rhs == nullptr ||
        !is_safe_nonnegative_integer_constant(lhs) ||
        !is_safe_nonnegative_integer_constant(rhs)) {
        return nullptr;
    }

    const std::uint64_t lhs_value = lhs->get_value();
    const std::uint64_t rhs_value = rhs->get_value();
    std::uint64_t result = 0;
    switch (inst.get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        result = lhs_value + rhs_value;
        break;
    case CoreIrBinaryOpcode::Sub:
        result = lhs_value - rhs_value;
        break;
    case CoreIrBinaryOpcode::Mul:
        result = lhs_value * rhs_value;
        break;
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
        if (rhs_value == 0) {
            return nullptr;
        }
        result = lhs_value / rhs_value;
        break;
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
        if (rhs_value == 0) {
            return nullptr;
        }
        result = lhs_value % rhs_value;
        break;
    case CoreIrBinaryOpcode::And:
        result = lhs_value & rhs_value;
        break;
    case CoreIrBinaryOpcode::Or:
        result = lhs_value | rhs_value;
        break;
    case CoreIrBinaryOpcode::Xor:
        result = lhs_value ^ rhs_value;
        break;
    case CoreIrBinaryOpcode::Shl:
        result = lhs_value << rhs_value;
        break;
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        result = lhs_value >> rhs_value;
        break;
    }

    if (!is_safe_nonnegative_integer_value(inst.get_type(), result)) {
        return nullptr;
    }
    return create_int_constant(context, inst.get_type(), result);
}

CoreIrValue *try_fold_unary(CoreIrContext &context, const CoreIrUnaryInst &inst) {
    const auto *operand = as_integer_constant(inst.get_operand());
    if (operand == nullptr || !is_safe_nonnegative_integer_constant(operand)) {
        return nullptr;
    }

    std::uint64_t result = 0;
    switch (inst.get_unary_opcode()) {
    case CoreIrUnaryOpcode::Negate:
    case CoreIrUnaryOpcode::BitwiseNot:
        return nullptr;
    case CoreIrUnaryOpcode::LogicalNot:
        result = operand->get_value() == 0 ? 1 : 0;
        break;
    }
    if (!is_safe_nonnegative_integer_value(inst.get_type(), result)) {
        return nullptr;
    }
    return create_int_constant(context, inst.get_type(), result);
}

CoreIrValue *try_fold_compare(CoreIrContext &context, const CoreIrCompareInst &inst) {
    const auto *lhs = as_integer_constant(inst.get_lhs());
    const auto *rhs = as_integer_constant(inst.get_rhs());
    if (lhs == nullptr || rhs == nullptr ||
        !is_safe_nonnegative_integer_constant(lhs) ||
        !is_safe_nonnegative_integer_constant(rhs)) {
        return nullptr;
    }

    const std::uint64_t lhs_value = lhs->get_value();
    const std::uint64_t rhs_value = rhs->get_value();
    bool result = false;
    switch (inst.get_predicate()) {
    case CoreIrComparePredicate::Equal:
        result = lhs_value == rhs_value;
        break;
    case CoreIrComparePredicate::NotEqual:
        result = lhs_value != rhs_value;
        break;
    case CoreIrComparePredicate::SignedLess:
        result = static_cast<std::int64_t>(lhs_value) <
                 static_cast<std::int64_t>(rhs_value);
        break;
    case CoreIrComparePredicate::SignedLessEqual:
        result = static_cast<std::int64_t>(lhs_value) <=
                 static_cast<std::int64_t>(rhs_value);
        break;
    case CoreIrComparePredicate::SignedGreater:
        result = static_cast<std::int64_t>(lhs_value) >
                 static_cast<std::int64_t>(rhs_value);
        break;
    case CoreIrComparePredicate::SignedGreaterEqual:
        result = static_cast<std::int64_t>(lhs_value) >=
                 static_cast<std::int64_t>(rhs_value);
        break;
    case CoreIrComparePredicate::UnsignedLess:
        result = lhs_value < rhs_value;
        break;
    case CoreIrComparePredicate::UnsignedLessEqual:
        result = lhs_value <= rhs_value;
        break;
    case CoreIrComparePredicate::UnsignedGreater:
        result = lhs_value > rhs_value;
        break;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        result = lhs_value >= rhs_value;
        break;
    }
    return create_int_constant(context, inst.get_type(), result ? 1 : 0);
}

CoreIrValue *try_fold_cast(CoreIrContext &context, const CoreIrCastInst &inst) {
    const auto *operand = as_integer_constant(inst.get_operand());
    if (operand == nullptr) {
        return nullptr;
    }

    switch (inst.get_cast_kind()) {
    case CoreIrCastKind::SignExtend: {
        const auto operand_width = get_integer_bit_width(inst.get_operand()->get_type());
        if (!operand_width.has_value()) {
            return nullptr;
        }
        const std::uint64_t truncated =
            operand->get_value() & get_integer_mask(*operand_width);
        // CoreIrConstantInt still stores raw bits, so keep negative narrow-int
        // results in IR form until the constant model becomes signed-aware.
        if (has_sign_bit(truncated, *operand_width)) {
            return nullptr;
        }
        return create_int_constant(context, inst.get_type(), truncated);
    }
    case CoreIrCastKind::ZeroExtend: {
        const auto operand_width = get_integer_bit_width(inst.get_operand()->get_type());
        if (!operand_width.has_value()) {
            return nullptr;
        }
        return create_int_constant(
            context, inst.get_type(),
            operand->get_value() & get_integer_mask(*operand_width));
    }
    case CoreIrCastKind::Truncate: {
        const auto cast_width = get_integer_bit_width(inst.get_type());
        if (!cast_width.has_value()) {
            return nullptr;
        }
        const std::uint64_t truncated =
            operand->get_value() & get_integer_mask(*cast_width);
        // Avoid folding signed-negative truncated literals into the current
        // raw-bit constant representation.
        if (has_sign_bit(truncated, *cast_width)) {
            return nullptr;
        }
        return create_int_constant(context, inst.get_type(), truncated);
    }
    case CoreIrCastKind::SignedIntToFloat:
    case CoreIrCastKind::UnsignedIntToFloat:
    case CoreIrCastKind::FloatToSignedInt:
    case CoreIrCastKind::FloatToUnsignedInt:
    case CoreIrCastKind::FloatExtend:
    case CoreIrCastKind::FloatTruncate:
    case CoreIrCastKind::PtrToInt:
    case CoreIrCastKind::IntToPtr:
        return nullptr;
    }
    return nullptr;
}

bool is_commutative_binary(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
    case CoreIrBinaryOpcode::Mul:
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
        return true;
    case CoreIrBinaryOpcode::Sub:
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
    case CoreIrBinaryOpcode::Shl:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        return false;
    }
    return false;
}

bool simplify_commutative_binary(CoreIrBinaryInst &binary) {
    CoreIrValue *lhs = binary.get_lhs();
    CoreIrValue *rhs = binary.get_rhs();
    if (lhs == nullptr || rhs == nullptr || !is_commutative_binary(binary.get_binary_opcode())) {
        return false;
    }
    if (as_integer_constant(lhs) != nullptr && as_integer_constant(rhs) == nullptr) {
        binary.set_operand(0, rhs);
        binary.set_operand(1, lhs);
        return true;
    }
    return false;
}

bool simplify_binary_identity(CoreIrContext &context, CoreIrBasicBlock &block,
                              CoreIrBinaryInst &binary) {
    CoreIrValue *lhs = binary.get_lhs();
    CoreIrValue *rhs = binary.get_rhs();
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    CoreIrValue *replacement = nullptr;
    const bool is_integer_result = as_integer_type(binary.get_type()) != nullptr;
    switch (binary.get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_zero_integer_constant(lhs)) {
            replacement = rhs;
        }
        break;
    case CoreIrBinaryOpcode::Sub:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_integer_result && lhs == rhs) {
            replacement =
                context.create_constant<CoreIrConstantInt>(binary.get_type(), 0);
        }
        break;
    case CoreIrBinaryOpcode::Mul:
        if (is_zero_integer_constant(rhs)) {
            replacement = rhs;
        } else if (is_zero_integer_constant(lhs)) {
            replacement = lhs;
        } else if (is_one_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_one_integer_constant(lhs)) {
            replacement = rhs;
        }
        break;
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
        if (is_one_integer_constant(rhs)) {
            replacement = lhs;
        }
        break;
    case CoreIrBinaryOpcode::And:
        if (is_zero_integer_constant(rhs)) {
            replacement = rhs;
        } else if (is_zero_integer_constant(lhs)) {
            replacement = lhs;
        } else if (lhs == rhs) {
            replacement = lhs;
        } else if (is_all_ones_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_all_ones_integer_constant(lhs)) {
            replacement = rhs;
        }
        break;
    case CoreIrBinaryOpcode::Or:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_zero_integer_constant(lhs)) {
            replacement = rhs;
        } else if (lhs == rhs) {
            replacement = lhs;
        } else if (is_all_ones_integer_constant(rhs)) {
            replacement = rhs;
        } else if (is_all_ones_integer_constant(lhs)) {
            replacement = lhs;
        }
        break;
    case CoreIrBinaryOpcode::Xor:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        } else if (is_zero_integer_constant(lhs)) {
            replacement = rhs;
        } else if (lhs == rhs) {
            replacement =
                context.create_constant<CoreIrConstantInt>(binary.get_type(), 0);
        }
        break;
    case CoreIrBinaryOpcode::Shl:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        }
        break;
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
        break;
    }

    if (replacement == nullptr) {
        return false;
    }

    binary.replace_all_uses_with(replacement);
    erase_instruction(block, &binary);
    return true;
}

bool simplify_signed_half_div_rem(CoreIrContext &context,
                                  CoreIrBasicBlock &block,
                                  CoreIrBinaryInst &binary) {
    if ((binary.get_binary_opcode() != CoreIrBinaryOpcode::SDiv &&
         binary.get_binary_opcode() != CoreIrBinaryOpcode::SRem) ||
        !is_integer_constant_value(binary.get_rhs(), 2)) {
        return false;
    }

    if (binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv &&
        block_is_even_successor_for_value(block, binary.get_lhs())) {
        CoreIrValue *one = create_int_constant(context, binary.get_type(), 1);
        auto shift = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::AShr, binary.get_type(),
            next_instcombine_name("instcombine.half.even."), binary.get_lhs(), one);
        shift->set_source_span(binary.get_source_span());
        CoreIrInstruction *replacement =
            insert_instruction_before(block, &binary, std::move(shift));
        if (replacement == nullptr) {
            return false;
        }
        binary.replace_all_uses_with(replacement);
        erase_instruction(block, &binary);
        return true;
    }

    CoreIrValue *replacement =
        binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv
            ? materialize_signed_div_by_two(context, block, &binary,
                                            binary.get_lhs(),
                                            binary.get_source_span())
            : materialize_signed_rem_by_two(context, block, &binary,
                                            binary.get_lhs(),
                                            binary.get_source_span());
    if (replacement == nullptr) {
        return false;
    }

    binary.replace_all_uses_with(replacement);
    erase_instruction(block, &binary);
    return true;
}

bool simplify_even_branch_signed_half_expansion(CoreIrContext &context,
                                                CoreIrBasicBlock &block,
                                                CoreIrBinaryInst &binary) {
    if (binary.get_binary_opcode() != CoreIrBinaryOpcode::AShr ||
        !is_integer_constant_value(binary.get_rhs(), 1)) {
        return false;
    }

    CoreIrValue *source = match_signed_half_div_expansion(&binary);
    if (source == nullptr || binary.get_lhs() == source ||
        !block_is_even_successor_for_value(block, source)) {
        return false;
    }

    CoreIrValue *one = create_int_constant(context, binary.get_type(), 1);
    auto shift = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::AShr, binary.get_type(),
        next_instcombine_name("instcombine.half.even."), source, one);
    shift->set_source_span(binary.get_source_span());
    CoreIrInstruction *replacement =
        insert_instruction_before(block, &binary, std::move(shift));
    if (replacement == nullptr) {
        return false;
    }

    CoreIrValue *old_lhs = binary.get_lhs();
    binary.replace_all_uses_with(replacement);
    erase_instruction(block, &binary);
    erase_dead_instruction_tree(old_lhs);
    return true;
}

bool simplify_identity_cast(CoreIrBasicBlock &block, CoreIrCastInst &cast) {
    CoreIrValue *operand = cast.get_operand();
    if (operand == nullptr || operand->get_type() != cast.get_type()) {
        return false;
    }
    cast.replace_all_uses_with(operand);
    erase_instruction(block, &cast);
    return true;
}

bool simplify_integer_cast(CoreIrBasicBlock &block, CoreIrCastInst &cast) {
    CoreIrValue *operand = cast.get_operand();
    if (operand == nullptr) {
        return false;
    }

    const auto cast_kind = cast.get_cast_kind();
    if (!is_supported_integer_cast_kind(cast_kind)) {
        return false;
    }

    const auto operand_width = get_integer_bit_width(operand->get_type());
    const auto cast_width = get_integer_bit_width(cast.get_type());
    if (!operand_width.has_value() || !cast_width.has_value()) {
        return false;
    }

    if (*operand_width == *cast_width) {
        const auto *operand_integer_type =
            dynamic_cast<const CoreIrIntegerType *>(operand->get_type());
        const auto *cast_integer_type =
            dynamic_cast<const CoreIrIntegerType *>(cast.get_type());
        if (operand_integer_type == nullptr || cast_integer_type == nullptr ||
            operand_integer_type->get_is_signed() ==
                cast_integer_type->get_is_signed()) {
            cast.replace_all_uses_with(operand);
            erase_instruction(block, &cast);
            return true;
        }
        return false;
    }

    auto *inner_cast = dynamic_cast<CoreIrCastInst *>(operand);
    if (inner_cast == nullptr) {
        return false;
    }

    const auto inner_kind = inner_cast->get_cast_kind();
    if (!is_supported_integer_cast_kind(inner_kind)) {
        return false;
    }

    CoreIrValue *inner_operand = inner_cast->get_operand();
    if (inner_operand == nullptr) {
        return false;
    }

    const auto inner_operand_width = get_integer_bit_width(inner_operand->get_type());
    const auto inner_result_width = get_integer_bit_width(inner_cast->get_type());
    if (!inner_operand_width.has_value() || !inner_result_width.has_value()) {
        return false;
    }

    if (cast_kind == inner_kind &&
        ((cast_kind == CoreIrCastKind::Truncate &&
          *inner_result_width > *cast_width) ||
         (cast_kind != CoreIrCastKind::Truncate &&
          *inner_result_width < *cast_width))) {
        cast.set_operand(0, inner_operand);
        erase_instruction_if_dead(block, inner_cast);
        return true;
    }

    if (cast_kind == CoreIrCastKind::Truncate &&
        (inner_kind == CoreIrCastKind::SignExtend ||
         inner_kind == CoreIrCastKind::ZeroExtend) &&
        *cast_width == *inner_operand_width) {
        cast.replace_all_uses_with(inner_operand);
        erase_instruction(block, &cast);
        erase_instruction_if_dead(block, inner_cast);
        return true;
    }

    return false;
}

bool simplify_compare_boolean_wrapper(CoreIrBasicBlock &block,
                                      CoreIrCompareInst &compare) {
    const CoreIrComparePredicate predicate = compare.get_predicate();
    if ((predicate != CoreIrComparePredicate::Equal &&
         predicate != CoreIrComparePredicate::NotEqual) ||
        !(is_zero_integer_constant(compare.get_lhs()) ||
          is_zero_integer_constant(compare.get_rhs()))) {
        return false;
    }

    CoreIrValue *other_side =
        is_zero_integer_constant(compare.get_lhs()) ? compare.get_rhs()
                                                    : compare.get_lhs();
    auto match = match_boolean_compare(other_side);
    if (!match.has_value() || match->compare == nullptr) {
        return false;
    }

    CoreIrComparePredicate replacement_predicate = match->compare->get_predicate();
    if ((predicate == CoreIrComparePredicate::Equal) ^ match->invert) {
        replacement_predicate = invert_compare_predicate(replacement_predicate);
    }

    CoreIrCompareInst *replacement = replace_compare_instruction(
        block, compare, replacement_predicate, match->compare->get_lhs(),
        match->compare->get_rhs());
    if (replacement == nullptr) {
        return false;
    }
    if (auto *inner_compare_instruction =
            dynamic_cast<CoreIrInstruction *>(match->compare);
        inner_compare_instruction != nullptr) {
        erase_instruction_if_dead(block, inner_compare_instruction);
    }
    return true;
}

bool simplify_compare_orientation(CoreIrCompareInst &compare) {
    if (dynamic_cast<const CoreIrConstant *>(compare.get_lhs()) == nullptr ||
        dynamic_cast<const CoreIrConstant *>(compare.get_rhs()) != nullptr) {
        return false;
    }

    CoreIrValue *original_lhs = compare.get_lhs();
    CoreIrValue *original_rhs = compare.get_rhs();
    compare.set_operand(0, original_rhs);
    compare.set_operand(1, original_lhs);
    compare.set_predicate(flip_compare_predicate(compare.get_predicate()));
    return true;
}

bool simplify_compare_signed_parity(CoreIrContext &context,
                                    CoreIrBasicBlock &block,
                                    CoreIrCompareInst &compare) {
    const CoreIrComparePredicate predicate = compare.get_predicate();
    if ((predicate != CoreIrComparePredicate::Equal &&
         predicate != CoreIrComparePredicate::NotEqual) ||
        !(is_zero_integer_constant(compare.get_lhs()) ||
          is_zero_integer_constant(compare.get_rhs()))) {
        return false;
    }

    CoreIrValue *parity_value =
        is_zero_integer_constant(compare.get_lhs()) ? compare.get_rhs()
                                                    : compare.get_lhs();
    CoreIrValue *source = match_parity_test_source(parity_value);
    if (source == nullptr) {
        return false;
    }

    CoreIrValue *one = create_int_constant(context, source->get_type(), 1);
    auto masked = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::And, source->get_type(),
        next_instcombine_name("instcombine.parity.mask."), source, one);
    masked->set_source_span(compare.get_source_span());
    CoreIrInstruction *masked_inst =
        insert_instruction_before(block, &compare, std::move(masked));
    if (masked_inst == nullptr) {
        return false;
    }

    CoreIrValue *zero = create_int_constant(context, source->get_type(), 0);
    CoreIrCompareInst *replacement = replace_compare_instruction(
        block, compare, predicate, masked_inst, zero);
    if (replacement == nullptr) {
        return false;
    }
    erase_dead_instruction_tree(parity_value);
    return true;
}

bool simplify_gep(CoreIrBasicBlock &block, CoreIrGetElementPtrInst &gep) {
    if (!is_trivial_zero_index_gep(gep)) {
        return false;
    }
    CoreIrValue *base = gep.get_base();
    gep.replace_all_uses_with(base);
    erase_instruction(block, &gep);
    return true;
}

bool simplify_nested_gep(CoreIrBasicBlock &block, CoreIrGetElementPtrInst &gep) {
    auto *inner_gep = dynamic_cast<CoreIrGetElementPtrInst *>(gep.get_base());
    if (inner_gep == nullptr || gep.get_index_count() == 0) {
        return false;
    }

    std::vector<CoreIrValue *> merged_indices;
    CoreIrValue *root_base = nullptr;
    if (!collect_structural_gep_chain(gep, root_base, merged_indices) ||
        root_base == nullptr) {
        return false;
    }

    if (root_base == gep.get_base() &&
        merged_indices.size() == gep.get_index_count()) {
        bool identical = true;
        for (std::size_t index = 0; index < merged_indices.size(); ++index) {
            if (merged_indices[index] != gep.get_index(index)) {
                identical = false;
                break;
            }
        }
        if (identical) {
            return false;
        }
    }

    const CoreIrType *source_pointee_type =
        get_structural_gep_source_pointee_type(gep);
    auto replacement = std::make_unique<CoreIrGetElementPtrInst>(
        gep.get_type(), gep.get_name(), root_base, merged_indices,
        source_pointee_type);
    replacement->set_source_span(gep.get_source_span());
    CoreIrInstruction *replacement_ptr =
        replace_instruction(block, &gep, std::move(replacement));
    if (replacement_ptr == nullptr) {
        return false;
    }
    erase_instruction_if_dead(block, inner_gep);
    return true;
}

bool simplify_stackslot_load(CoreIrBasicBlock &block, CoreIrLoadInst &load) {
    CoreIrValue *original_address = load.get_address();
    auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(
        unwrap_trivial_zero_index_geps(original_address));
    if (address == nullptr || address->get_stack_slot() == nullptr) {
        return false;
    }

    auto replacement = std::make_unique<CoreIrLoadInst>(
        load.get_type(), load.get_name(), address->get_stack_slot());
    replacement->set_source_span(load.get_source_span());
    CoreIrInstruction *replacement_ptr =
        replace_instruction(block, &load, std::move(replacement));
    if (replacement_ptr == nullptr) {
        return false;
    }
    erase_dead_address_chain(block, original_address);
    return true;
}

bool simplify_stackslot_store(CoreIrBasicBlock &block, CoreIrStoreInst &store) {
    CoreIrValue *original_address = store.get_address();
    auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(
        unwrap_trivial_zero_index_geps(original_address));
    if (address == nullptr || address->get_stack_slot() == nullptr) {
        return false;
    }

    auto replacement = std::make_unique<CoreIrStoreInst>(
        store.get_type(), store.get_value(), address->get_stack_slot());
    replacement->set_source_span(store.get_source_span());
    CoreIrInstruction *replacement_ptr =
        replace_instruction(block, &store, std::move(replacement));
    if (replacement_ptr == nullptr) {
        return false;
    }
    erase_dead_address_chain(block, original_address);
    return true;
}

bool simplify_conditional_branch_condition(CoreIrContext &context,
                                           CoreIrBasicBlock &block) {
    auto &instructions = block.get_instructions();
    if (instructions.empty()) {
        return false;
    }

    auto *cond_jump =
        dynamic_cast<CoreIrCondJumpInst *>(instructions.back().get());
    if (cond_jump == nullptr) {
        return false;
    }

    bool changed = false;
    CoreIrInstruction *old_condition_instruction =
        dynamic_cast<CoreIrInstruction *>(cond_jump->get_condition());
    CoreIrCompareInst *replacement = nullptr;

    if (std::optional<BooleanCompareMatch> match =
            match_boolean_compare(cond_jump->get_condition());
        match.has_value()) {
        auto *compare = match->compare;
        if (compare == nullptr) {
            return false;
        }
        const auto *compare_type =
            as_integer_type(compare->get_type());
        if (!(compare_type != nullptr && compare_type->get_bit_width() == 1 &&
              !match->invert && compare == cond_jump->get_condition())) {
            changed = true;
            replacement = create_branch_compare(
                context, block, cond_jump,
                match->invert ? invert_compare_predicate(compare->get_predicate())
                              : compare->get_predicate(),
                compare->get_lhs(), compare->get_rhs(),
                cond_jump->get_source_span());
        }
    } else if (auto *cast =
                   dynamic_cast<CoreIrCastInst *>(cond_jump->get_condition());
               cast != nullptr) {
        replacement = create_truthiness_compare(
            context, block, cond_jump, CoreIrComparePredicate::NotEqual, cast,
            cond_jump->get_source_span());
        changed = replacement != nullptr;
    } else if (auto *unary =
                   dynamic_cast<CoreIrUnaryInst *>(cond_jump->get_condition());
               unary != nullptr &&
               unary->get_unary_opcode() == CoreIrUnaryOpcode::LogicalNot) {
        if (auto *operand_compare =
                dynamic_cast<CoreIrCompareInst *>(unary->get_operand());
            operand_compare != nullptr) {
            replacement = create_branch_compare(
                context, block, cond_jump,
                invert_compare_predicate(operand_compare->get_predicate()),
                operand_compare->get_lhs(), operand_compare->get_rhs(),
                cond_jump->get_source_span());
            changed = replacement != nullptr;
        } else {
            replacement = create_truthiness_compare(
                context, block, cond_jump, CoreIrComparePredicate::Equal,
                unary->get_operand(), cond_jump->get_source_span());
            changed = replacement != nullptr;
        }
    } else if (CoreIrValue *condition = cond_jump->get_condition();
               condition != nullptr) {
        const auto *condition_type = condition->get_type();
        if ((as_integer_type(condition_type) != nullptr &&
             static_cast<const CoreIrIntegerType *>(condition_type)->get_bit_width() !=
                 1) ||
            is_pointer_type(condition_type) || is_float_type(condition_type)) {
            replacement = create_truthiness_compare(
                context, block, cond_jump, CoreIrComparePredicate::NotEqual,
                condition, cond_jump->get_source_span());
            changed = replacement != nullptr;
        }
    }

    if (replacement != nullptr && replacement != cond_jump->get_condition()) {
        cond_jump->set_operand(0, replacement);
        changed = true;
    }

    if (changed && old_condition_instruction != nullptr) {
        erase_instruction_if_dead(block, old_condition_instruction);
    }

    return changed;
}

std::vector<CoreIrInstruction *> collect_function_instructions(CoreIrFunction &function) {
    std::vector<CoreIrInstruction *> worklist;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction : block->get_instructions()) {
            if (instruction != nullptr) {
                worklist.push_back(instruction.get());
            }
        }
    }
    return worklist;
}

void refresh_live_instruction_set(
    CoreIrFunction &function,
    std::unordered_set<CoreIrInstruction *> &live_instructions) {
    std::size_t instruction_count = 0;
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr) {
            instruction_count += block->get_instructions().size();
        }
    }

    live_instructions.clear();
    live_instructions.reserve(instruction_count * 2 + 1);
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &candidate : block->get_instructions()) {
            if (candidate != nullptr) {
                live_instructions.insert(candidate.get());
            }
        }
    }
}

struct InstCombineWorklist {
    std::deque<CoreIrInstruction *> queue;
    std::unordered_set<CoreIrInstruction *> queued;
    CoreIrInstCombineStats stats;
};

void enqueue_instruction(InstCombineWorklist &worklist, CoreIrInstruction *instruction) {
    if (instruction == nullptr || !worklist.queued.insert(instruction).second) {
        return;
    }
    worklist.queue.push_back(instruction);
}

void enqueue_block_instructions(InstCombineWorklist &worklist,
                                CoreIrBasicBlock *block) {
    if (block == nullptr) {
        return;
    }
    for (const auto &instruction : block->get_instructions()) {
        enqueue_instruction(worklist, instruction.get());
    }
}

void enqueue_operand_definitions(
    InstCombineWorklist &worklist,
    const std::vector<CoreIrInstruction *> &operand_definitions) {
    for (CoreIrInstruction *operand_definition : operand_definitions) {
        enqueue_instruction(worklist, operand_definition);
    }
}

void enqueue_direct_users(InstCombineWorklist &worklist,
                          const std::vector<CoreIrUse> &uses) {
    for (const CoreIrUse &use : uses) {
        enqueue_instruction(worklist, use.get_user());
    }
}

void enqueue_impacted_instructions(
    InstCombineWorklist &worklist, CoreIrBasicBlock &block,
    const std::vector<CoreIrUse> &original_uses,
    const std::vector<CoreIrInstruction *> &original_operand_definitions) {
    enqueue_direct_users(worklist, original_uses);
    enqueue_operand_definitions(worklist, original_operand_definitions);
    enqueue_block_instructions(worklist, &block);
    if (!block.get_instructions().empty()) {
        enqueue_instruction(worklist, block.get_instructions().back().get());
    }
}

bool simplify_instruction(CoreIrContext &context, CoreIrBasicBlock &block,
                          CoreIrInstruction &instruction) {
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(&instruction); phi != nullptr) {
        if (CoreIrValue *replacement = try_fold_phi(*phi); replacement != nullptr) {
            phi->replace_all_uses_with(replacement);
            erase_instruction(block, phi);
            return true;
        }
        return simplify_phi_of_identical_incoming_binaries(block, *phi);
    }

    if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(&instruction); binary != nullptr) {
        if (CoreIrValue *replacement = try_fold_binary(context, *binary);
            replacement != nullptr) {
            binary->replace_all_uses_with(replacement);
            erase_instruction(block, binary);
            return true;
        }
        return simplify_even_branch_signed_half_expansion(context, block, *binary) ||
               simplify_commutative_binary(*binary) ||
               simplify_signed_half_div_rem(context, block, *binary) ||
               simplify_binary_identity(context, block, *binary);
    }

    if (auto *unary = dynamic_cast<CoreIrUnaryInst *>(&instruction); unary != nullptr) {
        if (CoreIrValue *replacement = try_fold_unary(context, *unary);
            replacement != nullptr) {
            unary->replace_all_uses_with(replacement);
            erase_instruction(block, unary);
            return true;
        }
        return false;
    }

    if (auto *cast = dynamic_cast<CoreIrCastInst *>(&instruction); cast != nullptr) {
        if (CoreIrValue *replacement = try_fold_cast(context, *cast);
            replacement != nullptr) {
            cast->replace_all_uses_with(replacement);
            erase_instruction(block, cast);
            return true;
        }
        return simplify_identity_cast(block, *cast) ||
               simplify_integer_cast(block, *cast);
    }

    if (auto *compare = dynamic_cast<CoreIrCompareInst *>(&instruction);
        compare != nullptr) {
        if (CoreIrValue *replacement = try_fold_compare(context, *compare);
            replacement != nullptr) {
            compare->replace_all_uses_with(replacement);
            erase_instruction(block, compare);
            return true;
        }
        if (compare->get_lhs() == compare->get_rhs()) {
            CoreIrValue *replacement = context.create_constant<CoreIrConstantInt>(
                compare->get_type(),
                compare->get_predicate() == CoreIrComparePredicate::Equal ||
                        compare->get_predicate() ==
                            CoreIrComparePredicate::SignedLessEqual ||
                        compare->get_predicate() ==
                            CoreIrComparePredicate::SignedGreaterEqual ||
                        compare->get_predicate() ==
                            CoreIrComparePredicate::UnsignedLessEqual ||
                        compare->get_predicate() ==
                            CoreIrComparePredicate::UnsignedGreaterEqual
                    ? 1
                    : 0);
            compare->replace_all_uses_with(replacement);
            erase_instruction(block, compare);
            return true;
        }
        return simplify_compare_boolean_wrapper(block, *compare) ||
               simplify_compare_signed_parity(context, block, *compare) ||
               simplify_compare_orientation(*compare);
    }

    if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(&instruction); gep != nullptr) {
        return simplify_gep(block, *gep) || simplify_nested_gep(block, *gep);
    }

    if (auto *load = dynamic_cast<CoreIrLoadInst *>(&instruction); load != nullptr) {
        return simplify_stackslot_load(block, *load);
    }

    if (auto *store = dynamic_cast<CoreIrStoreInst *>(&instruction); store != nullptr) {
        return simplify_stackslot_store(block, *store);
    }

    if (dynamic_cast<CoreIrCondJumpInst *>(&instruction) != nullptr) {
        return simplify_conditional_branch_condition(context, block);
    }

    return false;
}

bool run_instcombine_on_function(CoreIrContext &context, CoreIrFunction &function,
                                 CoreIrInstCombineStats *stats) {
    bool changed = false;
    InstCombineWorklist worklist;
    for (CoreIrInstruction *instruction : collect_function_instructions(function)) {
        enqueue_instruction(worklist, instruction);
    }
    std::unordered_set<CoreIrInstruction *> live_instructions;
    refresh_live_instruction_set(function, live_instructions);

    while (!worklist.queue.empty()) {
        CoreIrInstruction *instruction = worklist.queue.front();
        worklist.queue.pop_front();
        worklist.queued.erase(instruction);
        if (instruction == nullptr ||
            live_instructions.find(instruction) == live_instructions.end()) {
            continue;
        }
        ++worklist.stats.visited_instructions;
        CoreIrBasicBlock *block = instruction->get_parent();
        if (block == nullptr) {
            continue;
        }
        const std::vector<CoreIrUse> original_uses = instruction->get_uses();
        std::vector<CoreIrInstruction *> original_operand_definitions;
        original_operand_definitions.reserve(instruction->get_operands().size());
        for (CoreIrValue *operand : instruction->get_operands()) {
            original_operand_definitions.push_back(
                dynamic_cast<CoreIrInstruction *>(operand));
        }
        if (simplify_instruction(context, *block, *instruction)) {
            changed = true;
            ++worklist.stats.rewrites;
            refresh_live_instruction_set(function, live_instructions);
            enqueue_impacted_instructions(worklist, *block, original_uses,
                                          original_operand_definitions);
        }
    }
    if (stats != nullptr) {
        *stats = worklist.stats;
    }
    return changed;
}

} // namespace

PassKind CoreIrInstCombinePass::Kind() const {
    return PassKind::CoreIrInstCombine;
}

const char *CoreIrInstCombinePass::Name() const {
    return "CoreIrInstCombinePass";
}

PassResult CoreIrInstCombinePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrModule *module = build_result->get_module();
    if (core_ir_context == nullptr || module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    last_stats_ = CoreIrInstCombineStats{};
    for (const auto &function : module->get_functions()) {
        if (function != nullptr) {
            CoreIrInstCombineStats function_stats;
            const bool function_changed = run_instcombine_on_function(
                *core_ir_context, *function, &function_stats);
            last_stats_.visited_instructions += function_stats.visited_instructions;
            last_stats_.rewrites += function_stats.rewrites;
            if (function_changed) {
            effects.changed_functions.insert(function.get());
            }
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    return PassResult::Success(std::move(effects));
}

const CoreIrInstCombineStats &
get_instcombine_stats_for_testing(const CoreIrInstCombinePass &pass) {
    return pass.last_stats_;
}

} // namespace sysycc
