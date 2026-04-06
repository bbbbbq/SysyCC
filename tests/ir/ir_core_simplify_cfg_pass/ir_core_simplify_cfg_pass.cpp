#include <cassert>
#include <memory>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

void run_pass(CompilerContext &compiler_context) {
    CoreIrSimplifyCfgPass simplify_cfg_pass;
    assert(simplify_cfg_pass.Run(compiler_context).ok);
}

void assert_instruction_parents(const CoreIrFunction &function) {
    for (const auto &block : function.get_basic_blocks()) {
        assert(block != nullptr);
        for (const auto &instruction : block->get_instructions()) {
            assert(instruction != nullptr);
            assert(instruction->get_parent() == block.get());
        }
    }
}

void test_removes_trampoline_blocks() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *void_function_type = context->create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_simplify_cfg_trampoline");
    auto *function =
        module->create_function<CoreIrFunction>("main", void_function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *trampoline =
        function->create_basic_block<CoreIrBasicBlock>("trampoline");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    entry->create_instruction<CoreIrJumpInst>(void_type, trampoline);
    trampoline->create_instruction<CoreIrJumpInst>(void_type, exit);
    exit->create_instruction<CoreIrReturnInst>(void_type);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(function->get_basic_blocks().size() == 1);
    assert(dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get()) !=
           nullptr);
}

void test_cfg_second_stage_rules() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *void_function_type = context->create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_simplify_cfg_phase2");

    auto *same_target_function = module->create_function<CoreIrFunction>(
        "same_target_cond", void_function_type, false);
    auto *entry =
        same_target_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *target =
        same_target_function->create_basic_block<CoreIrBasicBlock>("target");
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, target, target);
    target->create_instruction<CoreIrReturnInst>(void_type);

    auto *trampoline_chain_function = module->create_function<CoreIrFunction>(
        "trampoline_chain", void_function_type, false);
    auto *chain_entry =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *cond_target = trampoline_chain_function->create_basic_block<CoreIrBasicBlock>(
        "cond_target");
    auto *trampoline_a =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("trampoline_a");
    auto *trampoline_b =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("trampoline_b");
    auto *chain_exit =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *chain_cond = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    chain_entry->create_instruction<CoreIrCondJumpInst>(void_type, chain_cond,
                                                        cond_target, trampoline_a);
    cond_target->create_instruction<CoreIrJumpInst>(void_type, trampoline_b);
    trampoline_a->create_instruction<CoreIrJumpInst>(void_type, trampoline_b);
    trampoline_b->create_instruction<CoreIrJumpInst>(void_type, chain_exit);
    chain_exit->create_instruction<CoreIrReturnInst>(void_type);

    auto *linear_function = module->create_function<CoreIrFunction>(
        "linear_merge", void_function_type, false);
    auto *linear_entry =
        linear_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *middle =
        linear_function->create_basic_block<CoreIrBasicBlock>("middle");
    auto *zero32 = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    linear_entry->create_instruction<CoreIrJumpInst>(void_type, middle);
    middle->create_instruction<CoreIrReturnInst>(void_type, zero32);

    auto *constant_cond_function = module->create_function<CoreIrFunction>(
        "constant_cond", void_function_type, false);
    auto *constant_entry =
        constant_cond_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *constant_true = constant_cond_function->create_basic_block<CoreIrBasicBlock>(
        "true_block");
    auto *constant_false =
        constant_cond_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *true_value = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *false_value = context->create_constant<CoreIrConstantInt>(i32_type, 9);
    auto *zero1 = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    constant_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, zero1, constant_true, constant_false);
    constant_true->create_instruction<CoreIrReturnInst>(void_type, true_value);
    constant_false->create_instruction<CoreIrReturnInst>(void_type, false_value);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(dynamic_cast<CoreIrJumpInst *>(entry->get_instructions().back().get()) ==
           nullptr);
    assert(dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get()) !=
           nullptr);
    assert(trampoline_chain_function->get_basic_blocks().size() == 1);
    auto *chain_entry_terminator = trampoline_chain_function->get_basic_blocks()
                                       .front()
                                       ->get_instructions()
                                       .back()
                                       .get();
    assert(dynamic_cast<CoreIrReturnInst *>(chain_entry_terminator) != nullptr);
    assert(linear_function->get_basic_blocks().size() == 1);
    assert(constant_cond_function->get_basic_blocks().size() == 1);
    auto *constant_return = dynamic_cast<CoreIrReturnInst *>(
        constant_cond_function->get_basic_blocks().front()->get_instructions().back().get());
    assert(constant_return != nullptr);
    auto *constant_result =
        dynamic_cast<CoreIrConstantInt *>(constant_return->get_return_value());
    assert(constant_result != nullptr);
    assert(constant_result->get_value() == 9);
}

void test_cfg_reaches_fixed_point_and_preserves_parents() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_simplify_cfg_fixed_point");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");
    auto *live = function->create_basic_block<CoreIrBasicBlock>("live");
    auto *trampoline =
        function->create_basic_block<CoreIrBasicBlock>("trampoline");
    auto *sink = function->create_basic_block<CoreIrBasicBlock>("sink");
    auto *orphan = function->create_basic_block<CoreIrBasicBlock>("orphan");
    auto *zero = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, zero, dead, live);
    dead->create_instruction<CoreIrJumpInst>(void_type, trampoline);
    live->create_instruction<CoreIrCondJumpInst>(void_type, one, sink, sink);
    trampoline->create_instruction<CoreIrJumpInst>(void_type, sink);
    sink->create_instruction<CoreIrReturnInst>(void_type, seven);
    orphan->create_instruction<CoreIrReturnInst>(void_type, nine);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    CoreIrRawPrinter printer;
    const std::string after_first = printer.print_module(*module);
    assert(function->get_basic_blocks().size() == 1);
    assert_instruction_parents(*function);
    auto *return_inst = dynamic_cast<CoreIrReturnInst *>(
        function->get_basic_blocks().front()->get_instructions().back().get());
    assert(return_inst != nullptr);
    auto *return_value =
        dynamic_cast<CoreIrConstantInt *>(return_inst->get_return_value());
    assert(return_value != nullptr);
    assert(return_value->get_value() == 7);

    run_pass(compiler_context);

    const std::string after_second = printer.print_module(*module);
    assert(after_first == after_second);
    assert_instruction_parents(*function);
}

} // namespace

int main() {
    test_removes_trampoline_blocks();
    test_cfg_second_stage_rules();
    test_cfg_reaches_fixed_point_and_preserves_parents();
    return 0;
}
