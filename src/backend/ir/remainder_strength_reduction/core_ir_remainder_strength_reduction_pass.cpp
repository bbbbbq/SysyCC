#include "backend/ir/remainder_strength_reduction/core_ir_remainder_strength_reduction_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

std::size_t g_remainder_sr_temp_counter = 0;

constexpr std::uint64_t kMinLargeDivisor = 1u << 24;

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

std::string next_remainder_sr_name(const std::string &prefix) {
    return prefix + std::to_string(g_remainder_sr_temp_counter++);
}

std::string make_unique_block_name(const CoreIrFunction &function,
                                   const std::string &base_name) {
    auto is_used = [&function](const std::string &name) {
        for (const auto &block_ptr : function.get_basic_blocks()) {
            if (block_ptr != nullptr && block_ptr->get_name() == name) {
                return true;
            }
        }
        return false;
    };

    if (!is_used(base_name)) {
        return base_name;
    }

    for (std::size_t index = 0;; ++index) {
        std::string candidate = base_name + "." + std::to_string(index);
        if (!is_used(candidate)) {
            return candidate;
        }
    }
}

CoreIrBasicBlock *insert_new_block_before(CoreIrFunction &function,
                                          CoreIrBasicBlock *anchor,
                                          std::unique_ptr<CoreIrBasicBlock> block) {
    if (anchor == nullptr || block == nullptr) {
        return nullptr;
    }
    block->set_parent(&function);
    CoreIrBasicBlock *block_ptr = block.get();
    auto &blocks = function.get_basic_blocks();
    auto it = std::find_if(blocks.begin(), blocks.end(),
                           [anchor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
                               return candidate.get() == anchor;
                           });
    blocks.insert(it, std::move(block));
    return block_ptr;
}

bool rewrite_phi_predecessor(CoreIrBasicBlock *successor,
                             CoreIrBasicBlock *old_predecessor,
                             CoreIrBasicBlock *new_predecessor) {
    if (successor == nullptr || old_predecessor == nullptr ||
        new_predecessor == nullptr || old_predecessor == new_predecessor) {
        return false;
    }

    bool changed = false;
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == old_predecessor) {
                phi->set_incoming_block(index, new_predecessor);
                changed = true;
            }
        }
    }
    return changed;
}

std::vector<CoreIrBasicBlock *> collect_successors(const CoreIrInstruction *terminator) {
    std::vector<CoreIrBasicBlock *> successors;
    if (terminator == nullptr) {
        return successors;
    }
    if (auto *jump = dynamic_cast<const CoreIrJumpInst *>(terminator); jump != nullptr) {
        if (jump->get_target_block() != nullptr) {
            successors.push_back(jump->get_target_block());
        }
        return successors;
    }

    auto *cond_jump = dynamic_cast<const CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return successors;
    }

    if (cond_jump->get_true_block() != nullptr) {
        successors.push_back(cond_jump->get_true_block());
    }
    if (cond_jump->get_false_block() != nullptr &&
        cond_jump->get_false_block() != cond_jump->get_true_block()) {
        successors.push_back(cond_jump->get_false_block());
    }
    return successors;
}

const CoreIrIntegerType *as_signed_integer_type(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    if (integer_type == nullptr || !integer_type->get_is_signed()) {
        return nullptr;
    }
    return integer_type;
}

const CoreIrConstantInt *as_integer_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

bool match_large_positive_signed_srem(const CoreIrBinaryInst &binary,
                                      std::uint64_t &divisor,
                                      std::uint64_t &fast_limit) {
    if (binary.get_binary_opcode() != CoreIrBinaryOpcode::SRem) {
        return false;
    }
    if (binary.get_name().rfind("srem.slow.rem.", 0) == 0) {
        return false;
    }
    if (const CoreIrBasicBlock *parent = binary.get_parent();
        parent != nullptr &&
        parent->get_name().find(".srem.slow") != std::string::npos) {
        return false;
    }

    const auto *integer_type = as_signed_integer_type(binary.get_type());
    const auto *divisor_constant = as_integer_constant(binary.get_rhs());
    if (integer_type == nullptr || divisor_constant == nullptr) {
        return false;
    }

    const std::size_t bit_width = integer_type->get_bit_width();
    if (bit_width == 0 || bit_width > 63) {
        return false;
    }

    const std::uint64_t signed_max =
        (std::uint64_t{1} << (bit_width - 1)) - 1;
    divisor = divisor_constant->get_value();
    if (divisor == 0 || divisor > signed_max || divisor < kMinLargeDivisor) {
        return false;
    }

    if (divisor > signed_max / 2) {
        return false;
    }

    fast_limit = divisor * 2;
    return fast_limit <= signed_max;
}

CoreIrConstantInt *create_int_constant(CoreIrContext &context,
                                       const CoreIrType *type,
                                       std::uint64_t value) {
    return context.create_constant<CoreIrConstantInt>(type, value);
}

bool expand_large_signed_srem(CoreIrContext &context, CoreIrFunction &function,
                              CoreIrBasicBlock &block, CoreIrBinaryInst &srem) {
    std::uint64_t divisor = 0;
    std::uint64_t fast_limit = 0;
    if (!match_large_positive_signed_srem(srem, divisor, fast_limit)) {
        return false;
    }

    auto &instructions = block.get_instructions();
    auto it = std::find_if(instructions.begin(), instructions.end(),
                           [&srem](const std::unique_ptr<CoreIrInstruction> &instruction) {
                               return instruction.get() == &srem;
                           });
    if (it == instructions.end()) {
        return false;
    }

    auto tail_begin = it + 1;
    if (tail_begin == instructions.end()) {
        return false;
    }

    CoreIrInstruction *old_terminator = instructions.back().get();
    if (old_terminator == nullptr || !old_terminator->get_is_terminator()) {
        return false;
    }

    std::vector<CoreIrBasicBlock *> old_successors = collect_successors(old_terminator);
    const SourceSpan source_span = srem.get_source_span();
    CoreIrValue *dividend = srem.get_lhs();
    const CoreIrType *value_type = srem.get_type();
    CoreIrContext *type_context = value_type != nullptr ? value_type->get_parent_context()
                                                        : nullptr;
    if (dividend == nullptr || value_type == nullptr || type_context == nullptr) {
        return false;
    }

    auto *void_type = type_context->create_type<CoreIrVoidType>();
    auto *i1_type = type_context->create_type<CoreIrIntegerType>(1);

    auto continuation = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, block.get_name() + ".srem.cont"));
    auto fast_block = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, block.get_name() + ".srem.fast"));
    auto slow_block = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, block.get_name() + ".srem.slow"));

    CoreIrBasicBlock *continuation_ptr =
        insert_new_block_before(function, &block, std::move(continuation));
    CoreIrBasicBlock *fast_block_ptr =
        insert_new_block_before(function, continuation_ptr, std::move(fast_block));
    CoreIrBasicBlock *slow_block_ptr =
        insert_new_block_before(function, continuation_ptr, std::move(slow_block));
    if (continuation_ptr == nullptr || fast_block_ptr == nullptr ||
        slow_block_ptr == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *successor : old_successors) {
        rewrite_phi_predecessor(successor, &block, continuation_ptr);
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> moved_tail;
    while (tail_begin != instructions.end()) {
        tail_begin->get()->set_parent(continuation_ptr);
        moved_tail.push_back(std::move(*tail_begin));
        tail_begin = instructions.erase(tail_begin);
    }

    std::unique_ptr<CoreIrInstruction> removed_srem = std::move(*it);
    instructions.erase(it);
    auto *removed_srem_ptr =
        static_cast<CoreIrBinaryInst *>(removed_srem.get());

    auto *fast_limit_value = create_int_constant(context, value_type, fast_limit);
    auto *fast_cmp = block.create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::UnsignedLess, i1_type,
        next_remainder_sr_name("srem.fast.range."), dividend, fast_limit_value);
    fast_cmp->set_source_span(source_span);

    auto *cond_jump = block.create_instruction<CoreIrCondJumpInst>(
        void_type, fast_cmp, fast_block_ptr, slow_block_ptr);
    cond_jump->set_source_span(source_span);

    auto *divisor_value = create_int_constant(context, value_type, divisor);
    auto *small_cmp = fast_block_ptr->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::UnsignedLess, i1_type,
        next_remainder_sr_name("srem.fast.small."), dividend, divisor_value);
    small_cmp->set_source_span(source_span);
    auto *sub = fast_block_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, value_type,
        next_remainder_sr_name("srem.fast.sub."), dividend, divisor_value);
    sub->set_source_span(source_span);
    auto *fast_result = fast_block_ptr->create_instruction<CoreIrSelectInst>(
        value_type, next_remainder_sr_name("srem.fast.sel."), small_cmp, dividend,
        sub);
    fast_result->set_source_span(source_span);
    auto *fast_jump =
        fast_block_ptr->create_instruction<CoreIrJumpInst>(void_type, continuation_ptr);
    fast_jump->set_source_span(source_span);

    auto *slow_result = slow_block_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SRem, value_type,
        next_remainder_sr_name("srem.slow.rem."), dividend, divisor_value);
    slow_result->set_source_span(source_span);
    auto *slow_jump =
        slow_block_ptr->create_instruction<CoreIrJumpInst>(void_type, continuation_ptr);
    slow_jump->set_source_span(source_span);

    auto *merge_phi = continuation_ptr->create_instruction<CoreIrPhiInst>(
        value_type, srem.get_name());
    merge_phi->set_source_span(source_span);
    merge_phi->add_incoming(fast_block_ptr, fast_result);
    merge_phi->add_incoming(slow_block_ptr, slow_result);

    for (auto &instruction : moved_tail) {
        continuation_ptr->append_instruction(std::move(instruction));
    }

    removed_srem_ptr->replace_all_uses_with(merge_phi);
    removed_srem_ptr->detach_operands();
    return true;
}

bool run_remainder_strength_reduction_on_function(CoreIrContext &context,
                                                  CoreIrFunction &function) {
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        auto &instructions = block->get_instructions();
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            auto *binary = dynamic_cast<CoreIrBinaryInst *>(instructions[index].get());
            if (binary == nullptr) {
                continue;
            }
            if (expand_large_signed_srem(context, function, *block, *binary)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

PassKind CoreIrRemainderStrengthReductionPass::Kind() const {
    return PassKind::CoreIrRemainderStrengthReduction;
}

const char *CoreIrRemainderStrengthReductionPass::Name() const {
    return "CoreIrRemainderStrengthReductionPass";
}

PassResult CoreIrRemainderStrengthReductionPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr || build_result->get_module() == nullptr ||
        build_result->get_context() == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : build_result->get_module()->get_functions()) {
        if (function == nullptr) {
            continue;
        }
        bool function_changed = false;
        while (run_remainder_strength_reduction_on_function(
            *build_result->get_context(), *function)) {
            function_changed = true;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
