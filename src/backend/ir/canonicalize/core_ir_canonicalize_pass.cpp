#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

// 这些不变式会被后续的 Core IR pass 直接依赖：
// - 每个 CondJump 条件都应显式变成 i1 SSA 值
// - 整数 / 指针 / 浮点的“真假判断”会在这里提前物化
// - 包裹布尔值的 compare/cast 形状会尽量折叠成单层比较

std::size_t g_canonicalize_temp_counter = 0;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

std::string next_canonicalize_name(const std::string &prefix) {
    return prefix + std::to_string(g_canonicalize_temp_counter++);
}

// 下面这组工具函数负责做类型、常量和地址形状判断，
// 让后面的规范化规则可以只关注“是否可安全改写”。
const CoreIrIntegerType *as_integer_type(const CoreIrType *type) {
    return dynamic_cast<const CoreIrIntegerType *>(type);
}

const CoreIrConstantInt *as_integer_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

bool is_zero_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    return constant != nullptr && constant->get_value() == 0;
}

bool is_one_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    return constant != nullptr && constant->get_value() == 1;
}

bool is_all_ones_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    const auto *integer_type =
        constant == nullptr ? nullptr : as_integer_type(constant->get_type());
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    const std::uint64_t all_ones =
        bit_width >= 64 ? std::numeric_limits<std::uint64_t>::max()
                        : ((std::uint64_t{1} << bit_width) - 1);
    return constant->get_value() == all_ones;
}

std::optional<std::size_t> get_integer_bit_width(const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return std::nullopt;
    }
    return integer_type->get_bit_width();
}

const CoreIrType *get_pointer_pointee_type(const CoreIrValue *value) {
    if (value == nullptr) {
        return nullptr;
    }
    const auto *pointer_type =
        dynamic_cast<const CoreIrPointerType *>(value->get_type());
    return pointer_type == nullptr ? nullptr : pointer_type->get_pointee_type();
}

const CoreIrType *get_gep_result_pointee_type(const CoreIrGetElementPtrInst &gep) {
    const auto *pointer_type =
        dynamic_cast<const CoreIrPointerType *>(gep.get_type());
    return pointer_type == nullptr ? nullptr : pointer_type->get_pointee_type();
}

bool is_trivial_zero_index_gep(const CoreIrGetElementPtrInst &gep) {
    return gep.get_index_count() == 1 && is_zero_integer_constant(gep.get_index(0)) &&
           gep.get_base() != nullptr && gep.get_base()->get_type() == gep.get_type();
}

CoreIrValue *unwrap_trivial_zero_index_geps(CoreIrValue *value) {
    CoreIrValue *current = value;
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(current);
    while (gep != nullptr && is_trivial_zero_index_gep(*gep)) {
        current = gep->get_base();
        gep = dynamic_cast<CoreIrGetElementPtrInst *>(current);
    }
    return current;
}

const CoreIrType *get_selected_gep_pointee_type(const CoreIrGetElementPtrInst &gep) {
    const CoreIrType *current_type = get_pointer_pointee_type(gep.get_base());
    if (current_type == nullptr) {
        return nullptr;
    }

    std::size_t start_index = 0;
    if (is_trivial_zero_index_gep(gep) ||
        (gep.get_index_count() > 1 && is_zero_integer_constant(gep.get_index(0)))) {
        start_index = 1;
    }

    for (std::size_t index = start_index; index < gep.get_index_count(); ++index) {
        if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
            array_type != nullptr) {
            current_type = array_type->get_element_type();
            continue;
        }
        if (const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(current_type);
            struct_type != nullptr) {
            if (start_index == 0 && index == 0) {
                return nullptr;
            }
            const auto *index_constant = as_integer_constant(gep.get_index(index));
            if (index_constant == nullptr) {
                return nullptr;
            }
            const std::uint64_t field_index = index_constant->get_value();
            if (field_index >= struct_type->get_element_types().size()) {
                return nullptr;
            }
            current_type = struct_type->get_element_types()[field_index];
            continue;
        }
        return nullptr;
    }

    return current_type;
}

bool can_flatten_structural_gep(const CoreIrGetElementPtrInst &gep) {
    return get_selected_gep_pointee_type(gep) == get_gep_result_pointee_type(gep);
}

bool collect_structural_gep_chain(const CoreIrGetElementPtrInst &gep,
                                  CoreIrValue *&root_base,
                                  std::vector<CoreIrValue *> &indices) {
    if (!can_flatten_structural_gep(gep)) {
        return false;
    }

    if (auto *inner_gep = dynamic_cast<CoreIrGetElementPtrInst *>(gep.get_base());
        inner_gep != nullptr && collect_structural_gep_chain(*inner_gep, root_base, indices)) {
        const bool drop_outer_leading_zero = is_zero_integer_constant(gep.get_index(0));
        const std::size_t outer_start_index = drop_outer_leading_zero ? 1 : 0;
        for (std::size_t index = outer_start_index; index < gep.get_index_count();
             ++index) {
            indices.push_back(gep.get_index(index));
        }
        return true;
    }

    root_base = unwrap_trivial_zero_index_geps(gep.get_base());
    for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
        indices.push_back(gep.get_index(index));
    }
    return true;
}

bool is_supported_integer_cast_kind(CoreIrCastKind cast_kind) {
    return cast_kind == CoreIrCastKind::SignExtend ||
           cast_kind == CoreIrCastKind::ZeroExtend ||
           cast_kind == CoreIrCastKind::Truncate;
}

bool preserves_integer_truthiness(CoreIrCastKind cast_kind) {
    return cast_kind == CoreIrCastKind::SignExtend ||
           cast_kind == CoreIrCastKind::ZeroExtend;
}

bool is_float_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Float;
}

bool is_pointer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Pointer;
}

CoreIrInstruction *insert_instruction_before(
    CoreIrBasicBlock &block, CoreIrInstruction *anchor,
    std::unique_ptr<CoreIrInstruction> instruction) {
    if (instruction == nullptr) {
        return nullptr;
    }

    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [anchor](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == anchor;
        });
    instruction->set_parent(&block);
    CoreIrInstruction *instruction_ptr = instruction.get();
    instructions.insert(it, std::move(instruction));
    return instruction_ptr;
}

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

CoreIrInstruction *replace_instruction(CoreIrBasicBlock &block,
                                       CoreIrInstruction *instruction,
                                       std::unique_ptr<CoreIrInstruction> replacement) {
    if (instruction == nullptr || replacement == nullptr) {
        return nullptr;
    }

    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return nullptr;
    }

    replacement->set_parent(&block);
    CoreIrInstruction *replacement_ptr = replacement.get();
    instruction->replace_all_uses_with(replacement_ptr);
    instruction->detach_operands();
    *it = std::move(replacement);
    return replacement_ptr;
}

bool erase_instruction_if_dead(CoreIrBasicBlock &block,
                               CoreIrInstruction *instruction) {
    if (instruction == nullptr || instruction->get_has_side_effect() ||
        !instruction->get_uses().empty()) {
        return false;
    }
    return erase_instruction(block, instruction);
}

CoreIrComparePredicate invert_compare_predicate(
    CoreIrComparePredicate predicate) {
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

CoreIrCompareInst *create_branch_compare(CoreIrContext &context,
                                         CoreIrBasicBlock &block,
                                         CoreIrInstruction *anchor,
                                         CoreIrComparePredicate predicate,
                                         CoreIrValue *lhs,
                                         CoreIrValue *rhs,
                                         const SourceSpan &source_span) {
    const auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    auto instruction = std::make_unique<CoreIrCompareInst>(
        predicate, i1_type, next_canonicalize_name("canon.cond."), lhs, rhs);
    instruction->set_source_span(source_span);
    return static_cast<CoreIrCompareInst *>(
        insert_instruction_before(block, anchor, std::move(instruction)));
}

CoreIrCompareInst *replace_compare_instruction(
    CoreIrBasicBlock &block, CoreIrCompareInst &compare,
    CoreIrComparePredicate predicate, CoreIrValue *lhs, CoreIrValue *rhs) {
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

std::optional<BooleanCompareMatch>
match_boolean_compare(CoreIrValue *value) {
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

CoreIrCompareInst *canonicalize_branch_condition_value(CoreIrContext &context,
                                                       CoreIrBasicBlock &block,
                                                       CoreIrInstruction *anchor,
                                                       CoreIrValue *condition,
                                                       const SourceSpan &source_span,
                                                       bool &changed) {
    if (std::optional<BooleanCompareMatch> match =
            match_boolean_compare(condition);
        match.has_value()) {
        auto *compare = match->compare;
        const auto *compare_type =
            compare == nullptr ? nullptr : as_integer_type(compare->get_type());
        if (compare != nullptr && compare_type != nullptr &&
            compare_type->get_bit_width() == 1 && !match->invert &&
            compare == condition) {
            return compare;
        }
        changed = true;
        return create_branch_compare(
            context, block, anchor,
            match->invert ? invert_compare_predicate(compare->get_predicate())
                          : compare->get_predicate(),
            compare->get_lhs(), compare->get_rhs(), source_span);
    }

    if (auto *cast = dynamic_cast<CoreIrCastInst *>(condition); cast != nullptr) {
        CoreIrCompareInst *replacement = create_truthiness_compare(
            context, block, anchor, CoreIrComparePredicate::NotEqual, cast,
            source_span);
        if (replacement != nullptr) {
            changed = true;
            return replacement;
        }
        return nullptr;
    }

    auto *unary = dynamic_cast<CoreIrUnaryInst *>(condition);
    if (unary != nullptr &&
        unary->get_unary_opcode() == CoreIrUnaryOpcode::LogicalNot) {
        if (auto *operand_compare =
                dynamic_cast<CoreIrCompareInst *>(unary->get_operand());
            operand_compare != nullptr) {
            changed = true;
            return create_branch_compare(
                context, block, anchor,
                invert_compare_predicate(operand_compare->get_predicate()),
                operand_compare->get_lhs(), operand_compare->get_rhs(),
                source_span);
        }
        CoreIrCompareInst *replacement = create_truthiness_compare(
            context, block, anchor, CoreIrComparePredicate::Equal,
            unary->get_operand(), source_span);
        if (replacement != nullptr) {
            changed = true;
            return replacement;
        }
    }

    if (condition != nullptr) {
        const auto *condition_type = condition->get_type();
        if ((as_integer_type(condition_type) != nullptr &&
             static_cast<const CoreIrIntegerType *>(condition_type)->get_bit_width() !=
                 1) ||
            is_pointer_type(condition_type) || is_float_type(condition_type)) {
            changed = true;
            return create_truthiness_compare(context, block, anchor,
                                             CoreIrComparePredicate::NotEqual,
                                             condition, source_span);
        }
    }

    return nullptr;
}


bool canonicalize_branch_condition(CoreIrContext &context,
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
    CoreIrCompareInst *replacement = canonicalize_branch_condition_value(
        context, block, cond_jump, cond_jump->get_condition(),
        cond_jump->get_source_span(), changed);
    if (replacement != nullptr && replacement != cond_jump->get_condition()) {
        cond_jump->set_operand(0, replacement);
        changed = true;
    }

    if (changed && old_condition_instruction != nullptr) {
        erase_instruction_if_dead(block, old_condition_instruction);
    }

    return changed;
}

bool canonicalize_integer_cast(CoreIrBasicBlock &block, CoreIrCastInst &cast) {
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
        const auto *operand_integer_type = as_integer_type(operand->get_type());
        const auto *cast_integer_type = as_integer_type(cast.get_type());
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

    const auto inner_operand_width =
        get_integer_bit_width(inner_operand->get_type());
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

void erase_dead_address_chain(CoreIrBasicBlock &block, CoreIrValue *value) {
    CoreIrInstruction *instruction = dynamic_cast<CoreIrInstruction *>(value);
    while (instruction != nullptr && !instruction->get_has_side_effect() &&
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

bool canonicalize_gep(CoreIrBasicBlock &block, CoreIrGetElementPtrInst &gep) {
    if (!is_trivial_zero_index_gep(gep)) {
        return false;
    }
    CoreIrValue *base = gep.get_base();
    gep.replace_all_uses_with(base);
    erase_instruction(block, &gep);
    return true;
}

bool canonicalize_nested_gep(CoreIrBasicBlock &block, CoreIrGetElementPtrInst &gep) {
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

    auto replacement = std::make_unique<CoreIrGetElementPtrInst>(
        gep.get_type(), gep.get_name(), root_base, merged_indices);
    replacement->set_source_span(gep.get_source_span());
    CoreIrInstruction *replacement_ptr =
        replace_instruction(block, &gep, std::move(replacement));
    if (replacement_ptr == nullptr) {
        return false;
    }
    erase_instruction_if_dead(block, inner_gep);
    return true;
}

bool canonicalize_commutative_binary(CoreIrBasicBlock &block,
                                     CoreIrBinaryInst &binary) {
    CoreIrValue *lhs = binary.get_lhs();
    CoreIrValue *rhs = binary.get_rhs();
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    switch (binary.get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
    case CoreIrBinaryOpcode::Mul:
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
        if (as_integer_constant(lhs) != nullptr && as_integer_constant(rhs) == nullptr) {
            binary.set_operand(0, rhs);
            binary.set_operand(1, lhs);
            return true;
        }
        return false;
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

bool canonicalize_binary_identity(CoreIrContext &context,
                                  CoreIrBasicBlock &block,
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
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
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
    case CoreIrBinaryOpcode::Shl:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        if (is_zero_integer_constant(rhs)) {
            replacement = lhs;
        }
        break;
    }

    if (replacement == nullptr) {
        return false;
    }

    binary.replace_all_uses_with(replacement);
    erase_instruction(block, &binary);
    return true;
}

bool canonicalize_compare_boolean_wrapper(CoreIrBasicBlock &block,
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

CoreIrComparePredicate flip_compare_predicate(
    CoreIrComparePredicate predicate) {
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

bool canonicalize_compare_orientation(CoreIrBasicBlock &block,
                                      CoreIrCompareInst &compare) {
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

bool canonicalize_stackslot_load(CoreIrBasicBlock &block, CoreIrLoadInst &load) {
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

bool canonicalize_stackslot_store(CoreIrBasicBlock &block,
                                  CoreIrStoreInst &store) {
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

bool canonicalize_nonterminator_instructions(CoreIrContext &context,
                                             CoreIrBasicBlock &block) {
    // 逐条扫非终结指令，必要时会删掉当前节点或替换成更规范的形状。
    bool changed = false;
    auto &instructions = block.get_instructions();
    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }
        if (instruction->get_is_terminator()) {
            ++index;
            continue;
        }

        const std::size_t old_size = instructions.size();
        if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
            binary != nullptr) {
            if (canonicalize_commutative_binary(block, *binary) ||
                canonicalize_binary_identity(context, block, *binary)) {
                changed = true;
            }
        } else if (auto *cast = dynamic_cast<CoreIrCastInst *>(instruction);
            cast != nullptr) {
            if (canonicalize_integer_cast(block, *cast)) {
                changed = true;
            }
        } else if (auto *compare = dynamic_cast<CoreIrCompareInst *>(instruction);
                   compare != nullptr) {
            if (canonicalize_compare_boolean_wrapper(block, *compare) ||
                canonicalize_compare_orientation(block, *compare)) {
                changed = true;
            }
        } else if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
                   gep != nullptr) {
            if (canonicalize_gep(block, *gep) ||
                canonicalize_nested_gep(block, *gep)) {
                changed = true;
            }
        } else if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
                   load != nullptr) {
            if (canonicalize_stackslot_load(block, *load)) {
                changed = true;
            }
        } else if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
                   store != nullptr) {
            if (canonicalize_stackslot_store(block, *store)) {
                changed = true;
            }
        }

        if (instructions.size() < old_size) {
            continue;
        }
        ++index;
    }
    return changed;
}

std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *>
collect_trampoline_blocks(CoreIrFunction &function) {
    std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *> trampoline_blocks;
    if (function.get_basic_blocks().empty()) {
        return trampoline_blocks;
    }

    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
    for (const auto &block : function.get_basic_blocks()) {
        if (block.get() == nullptr || block.get() == entry_block) {
            continue;
        }
        if (block->get_instructions().size() != 1) {
            continue;
        }
        auto *jump =
            dynamic_cast<CoreIrJumpInst *>(block->get_instructions().front().get());
        if (jump == nullptr || jump->get_target_block() == nullptr ||
            jump->get_target_block() == block.get()) {
            continue;
        }
        trampoline_blocks.emplace(block.get(), jump->get_target_block());
    }
    return trampoline_blocks;
}

CoreIrBasicBlock *resolve_trampoline_target(
    CoreIrBasicBlock *block,
    const std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *> &trampoline_blocks) {
    std::unordered_set<CoreIrBasicBlock *> seen;
    CoreIrBasicBlock *current = block;
    while (current != nullptr) {
        auto it = trampoline_blocks.find(current);
        if (it == trampoline_blocks.end()) {
            return current;
        }
        if (!seen.insert(current).second) {
            return current;
        }
        current = it->second;
    }
    return current;
}

bool remove_trampoline_blocks(CoreIrFunction &function) {
    auto trampoline_blocks = collect_trampoline_blocks(function);
    if (trampoline_blocks.empty()) {
        return false;
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        auto &instructions = block->get_instructions();
        if (instructions.empty()) {
            continue;
        }
        CoreIrInstruction *terminator = instructions.back().get();
        if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator);
            jump != nullptr) {
            CoreIrBasicBlock *new_target =
                resolve_trampoline_target(jump->get_target_block(), trampoline_blocks);
            jump->set_target_block(new_target);
        } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
                   cond_jump != nullptr) {
            cond_jump->set_true_block(resolve_trampoline_target(
                cond_jump->get_true_block(), trampoline_blocks));
            cond_jump->set_false_block(resolve_trampoline_target(
                cond_jump->get_false_block(), trampoline_blocks));
        }
    }

    auto &blocks = function.get_basic_blocks();
    blocks.erase(
        std::remove_if(blocks.begin(), blocks.end(),
                       [&trampoline_blocks](const std::unique_ptr<CoreIrBasicBlock> &block) {
                           if (block == nullptr) {
                               return true;
                           }
                           if (trampoline_blocks.find(block.get()) ==
                               trampoline_blocks.end()) {
                               return false;
                           }
                           for (const auto &instruction : block->get_instructions()) {
                               instruction->detach_operands();
                           }
                           return true;
                       }),
        blocks.end());
    return true;
}

std::unordered_map<CoreIrBasicBlock *, std::size_t>
collect_predecessor_counts(CoreIrFunction &function) {
    std::unordered_map<CoreIrBasicBlock *, std::size_t> predecessor_counts;
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr) {
            predecessor_counts.emplace(block.get(), 0);
        }
    }
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        CoreIrInstruction *terminator = block->get_instructions().back().get();
        if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator);
            jump != nullptr && jump->get_target_block() != nullptr) {
            predecessor_counts[jump->get_target_block()]++;
        } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
                   cond_jump != nullptr) {
            if (cond_jump->get_true_block() != nullptr) {
                predecessor_counts[cond_jump->get_true_block()]++;
            }
            if (cond_jump->get_false_block() != nullptr) {
                predecessor_counts[cond_jump->get_false_block()]++;
            }
        }
    }
    return predecessor_counts;
}

bool simplify_redundant_cond_jumps(CoreIrFunction &function) {
    bool changed = false;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(
            block->get_instructions().back().get());
        if (cond_jump == nullptr ||
            cond_jump->get_true_block() != cond_jump->get_false_block()) {
            continue;
        }
        CoreIrBasicBlock *target = cond_jump->get_true_block();
        cond_jump->detach_operands();
        auto replacement = std::make_unique<CoreIrJumpInst>(
            cond_jump->get_type(), target);
        replacement->set_source_span(cond_jump->get_source_span());
        replacement->set_parent(block.get());
        block->get_instructions().back() = std::move(replacement);
        changed = true;
    }
    return changed;
}

bool simplify_constant_cond_jumps(CoreIrFunction &function) {
    bool changed = false;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(
            block->get_instructions().back().get());
        if (cond_jump == nullptr) {
            continue;
        }
        const auto *constant = as_integer_constant(cond_jump->get_condition());
        if (constant == nullptr) {
            continue;
        }
        CoreIrBasicBlock *target =
            constant->get_value() == 0 ? cond_jump->get_false_block()
                                       : cond_jump->get_true_block();
        cond_jump->detach_operands();
        auto replacement =
            std::make_unique<CoreIrJumpInst>(cond_jump->get_type(), target);
        replacement->set_source_span(cond_jump->get_source_span());
        replacement->set_parent(block.get());
        block->get_instructions().back() = std::move(replacement);
        changed = true;
    }
    return changed;
}

bool remove_unreachable_blocks(CoreIrFunction &function) {
    if (function.get_basic_blocks().empty()) {
        return false;
    }

    auto predecessor_counts = collect_predecessor_counts(function);
    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
    auto &blocks = function.get_basic_blocks();
    const auto new_end = std::remove_if(
        blocks.begin(), blocks.end(),
        [&predecessor_counts, entry_block](
            const std::unique_ptr<CoreIrBasicBlock> &block) {
            if (block == nullptr) {
                return true;
            }
            if (block.get() == entry_block) {
                return false;
            }
            auto it = predecessor_counts.find(block.get());
            if (it == predecessor_counts.end() || it->second != 0) {
                return false;
            }
            for (const auto &instruction : block->get_instructions()) {
                if (instruction != nullptr) {
                    instruction->detach_operands();
                }
            }
            return true;
        });
    if (new_end == blocks.end()) {
        return false;
    }
    blocks.erase(new_end, blocks.end());
    return true;
}

bool merge_linear_blocks(CoreIrFunction &function) {
    // 单前驱、单后继的线性块可以合并，减少 CFG 噪音。
    auto predecessor_counts = collect_predecessor_counts(function);
    if (function.get_basic_blocks().empty()) {
        return false;
    }
    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
        // 这一轮做完后可能又产生新的可规范化机会，所以需要多轮迭代到稳定。
    auto &blocks = function.get_basic_blocks();

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        CoreIrBasicBlock *block = blocks[index].get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *jump =
            dynamic_cast<CoreIrJumpInst *>(block->get_instructions().back().get());
        if (jump == nullptr) {
            continue;
        }
        CoreIrBasicBlock *successor = jump->get_target_block();
        if (successor == nullptr || successor == block || successor == entry_block ||
            predecessor_counts[successor] != 1) {
            continue;
        }

        auto successor_it = std::find_if(
            blocks.begin(), blocks.end(),
            [successor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
                return candidate.get() == successor;
            });
        if (successor_it == blocks.end()) {
            continue;
        }

        jump->detach_operands();
        block->get_instructions().pop_back();
        auto &successor_instructions = successor->get_instructions();
        for (auto &instruction : successor_instructions) {
            instruction->set_parent(block);
            block->get_instructions().push_back(std::move(instruction));
        }
        successor_instructions.clear();
        blocks.erase(successor_it);
        return true;
    }

    return false;
}

} // namespace

PassKind CoreIrCanonicalizePass::Kind() const {
    return PassKind::CoreIrCanonicalize;
}

const char *CoreIrCanonicalizePass::Name() const {
    return "CoreIrCanonicalizePass";
}

PassResult CoreIrCanonicalizePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrModule *module = build_result->get_module();
    if (core_ir_context == nullptr || module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &function : module->get_functions()) {
            for (const auto &block : function->get_basic_blocks()) {
                changed =
                    canonicalize_nonterminator_instructions(*core_ir_context, *block) ||
                    changed;
                changed = canonicalize_branch_condition(*core_ir_context, *block) ||
                          changed;
            }
            changed = simplify_constant_cond_jumps(*function) || changed;
            changed = simplify_redundant_cond_jumps(*function) || changed;
            changed = remove_trampoline_blocks(*function) || changed;
            changed = remove_unreachable_blocks(*function) || changed;
            changed = merge_linear_blocks(*function) || changed;
        }
    }
    return PassResult::Success();
}

} // namespace sysycc
