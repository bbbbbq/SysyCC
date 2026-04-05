#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

const CoreIrConstantInt *as_int_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

std::size_t get_integer_bit_width(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    return integer_type == nullptr ? 0 : integer_type->get_bit_width();
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
    const std::size_t bit_width = get_integer_bit_width(constant->get_type());
    if (bit_width == 0) {
        return false;
    }
    const std::uint64_t value = constant->get_value() & get_integer_mask(bit_width);
    return !has_sign_bit(value, bit_width);
}

bool is_safe_nonnegative_integer_value(const CoreIrType *type, std::uint64_t value) {
    const std::size_t bit_width = get_integer_bit_width(type);
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

CoreIrValue *try_fold_binary(CoreIrContext &context, const CoreIrBinaryInst &inst) {
    const auto *lhs = as_int_constant(inst.get_lhs());
    const auto *rhs = as_int_constant(inst.get_rhs());
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
    const auto *operand = as_int_constant(inst.get_operand());
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

CoreIrValue *try_fold_compare(CoreIrContext &context,
                              const CoreIrCompareInst &inst) {
    const auto *lhs = as_int_constant(inst.get_lhs());
    const auto *rhs = as_int_constant(inst.get_rhs());
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
    const auto *operand = as_int_constant(inst.get_operand());
    if (operand == nullptr || !is_safe_nonnegative_integer_constant(operand)) {
        return nullptr;
    }

    switch (inst.get_cast_kind()) {
    case CoreIrCastKind::SignExtend:
    case CoreIrCastKind::ZeroExtend:
    case CoreIrCastKind::Truncate:
        if (!is_safe_nonnegative_integer_value(inst.get_type(), operand->get_value())) {
            return nullptr;
        }
        return create_int_constant(context, inst.get_type(), operand->get_value());
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

bool simplify_constant_conditional_branch(CoreIrBasicBlock &block,
                                          const CoreIrType *void_type) {
    auto &instructions = block.get_instructions();
    if (instructions.empty()) {
        return false;
    }
    auto *cond_jump =
        dynamic_cast<CoreIrCondJumpInst *>(instructions.back().get());
    if (cond_jump == nullptr) {
        return false;
    }
    const auto *condition = as_int_constant(cond_jump->get_condition());
    if (condition == nullptr) {
        return false;
    }

    CoreIrBasicBlock *target =
        condition->get_value() != 0 ? cond_jump->get_true_block()
                                    : cond_jump->get_false_block();
    cond_jump->detach_operands();
    auto replacement = std::make_unique<CoreIrJumpInst>(void_type, target);
    replacement->set_parent(&block);
    instructions.back() = std::move(replacement);
    return true;
}

} // namespace

PassKind CoreIrConstFoldPass::Kind() const {
    return PassKind::CoreIrConstFold;
}

const char *CoreIrConstFoldPass::Name() const {
    return "CoreIrConstFoldPass";
}

PassResult CoreIrConstFoldPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrModule *module = build_result->get_module();
    if (core_ir_context == nullptr || module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    const auto *void_type = core_ir_context->create_type<CoreIrVoidType>();
    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            for (const auto &instruction : block->get_instructions()) {
                CoreIrValue *replacement = nullptr;
                if (const auto *binary =
                        dynamic_cast<CoreIrBinaryInst *>(instruction.get());
                    binary != nullptr) {
                    replacement = try_fold_binary(*core_ir_context, *binary);
                } else if (const auto *unary =
                               dynamic_cast<CoreIrUnaryInst *>(instruction.get());
                           unary != nullptr) {
                    replacement = try_fold_unary(*core_ir_context, *unary);
                } else if (const auto *compare =
                               dynamic_cast<CoreIrCompareInst *>(instruction.get());
                           compare != nullptr) {
                    replacement = try_fold_compare(*core_ir_context, *compare);
                } else if (const auto *cast =
                               dynamic_cast<CoreIrCastInst *>(instruction.get());
                           cast != nullptr) {
                    replacement = try_fold_cast(*core_ir_context, *cast);
                }

                if (replacement != nullptr) {
                    instruction->replace_all_uses_with(replacement);
                    function_changed = true;
                }
            }
            function_changed =
                simplify_constant_conditional_branch(*block, void_type) ||
                function_changed;
        }
        if (function_changed) {
            build_result->invalidate_core_ir_analyses(*function);
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
