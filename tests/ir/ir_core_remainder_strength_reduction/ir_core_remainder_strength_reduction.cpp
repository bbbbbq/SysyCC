#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/remainder_strength_reduction/core_ir_remainder_strength_reduction_pass.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

CompilerContext make_compiler_context(std::unique_ptr<CoreIrContext> context,
                                      CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    return compiler_context;
}

void test_reduces_large_signed_remainder_with_guarded_fast_path() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_remainder_strength_reduction");
    auto *function =
        module->create_function<CoreIrFunction>("f", function_type, false);
    auto *x = function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *large_divisor =
        context->create_constant<CoreIrConstantInt>(i32_type, 1000000007u);

    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", x, five);
    auto *rem = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SRem, i32_type, "rem", sum, large_divisor);
    auto *result = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "result", rem, one);
    entry->create_instruction<CoreIrReturnInst>(void_type, result);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrRemainderStrengthReductionPass pass;
    PassResult run_result = pass.Run(compiler_context);
    assert(run_result.ok);
    assert(run_result.core_ir_effects.has_value());
    assert(run_result.core_ir_effects->changed_functions.count(function) == 1);

    assert(function->get_basic_blocks().size() == 4);

    CoreIrBasicBlock *guard_block = nullptr;
    CoreIrBasicBlock *fast_block = nullptr;
    CoreIrBasicBlock *slow_block = nullptr;
    CoreIrBasicBlock *continuation_block = nullptr;
    for (const auto &block_ptr : function->get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        if (block->get_name() == "entry") {
            guard_block = block;
        } else if (block->get_name().find(".srem.fast") != std::string::npos) {
            fast_block = block;
        } else if (block->get_name().find(".srem.slow") != std::string::npos) {
            slow_block = block;
        } else if (block->get_name().find(".srem.cont") != std::string::npos) {
            continuation_block = block;
        }
    }

    assert(guard_block != nullptr);
    assert(fast_block != nullptr);
    assert(slow_block != nullptr);
    assert(continuation_block != nullptr);

    auto *guard_term = dynamic_cast<CoreIrCondJumpInst *>(
        guard_block->get_instructions().back().get());
    assert(guard_term != nullptr);
    assert(guard_term->get_true_block() == fast_block);
    assert(guard_term->get_false_block() == slow_block);

    bool saw_fast_select = false;
    for (const auto &instruction_ptr : fast_block->get_instructions()) {
        if (dynamic_cast<CoreIrSelectInst *>(instruction_ptr.get()) != nullptr) {
            saw_fast_select = true;
            break;
        }
    }
    assert(saw_fast_select);

    bool saw_slow_srem = false;
    for (const auto &instruction_ptr : slow_block->get_instructions()) {
        auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction_ptr.get());
        if (binary != nullptr &&
            binary->get_binary_opcode() == CoreIrBinaryOpcode::SRem) {
            saw_slow_srem = true;
            break;
        }
    }
    assert(saw_slow_srem);

    auto *merge_phi = dynamic_cast<CoreIrPhiInst *>(
        continuation_block->get_instructions().front().get());
    assert(merge_phi != nullptr);
    assert(merge_phi->get_name() == "rem");
    assert(merge_phi->get_incoming_count() == 2);

    auto *continued_result = dynamic_cast<CoreIrBinaryInst *>(
        continuation_block->get_instructions()[1].get());
    assert(continued_result != nullptr);
    assert(continued_result->get_lhs() == merge_phi);
}

void test_skips_small_divisors() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_remainder_strength_reduction_small");
    auto *function =
        module->create_function<CoreIrFunction>("f", function_type, false);
    auto *x = function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *small_divisor =
        context->create_constant<CoreIrConstantInt>(i32_type, 97);

    auto *rem = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SRem, i32_type, "rem", x, small_divisor);
    entry->create_instruction<CoreIrReturnInst>(void_type, rem);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrRemainderStrengthReductionPass pass;
    PassResult run_result = pass.Run(compiler_context);
    assert(run_result.ok);
    assert(!run_result.core_ir_effects.has_value() ||
           !run_result.core_ir_effects->has_changes());
    assert(function->get_basic_blocks().size() == 1);
    assert(dynamic_cast<CoreIrReturnInst *>(
               entry->get_instructions().back().get()) != nullptr);
}

} // namespace

int main() {
    test_reduces_large_signed_remainder_with_guarded_fast_path();
    test_skips_small_divisors();
    return 0;
}
