#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/simple_loop_unswitch/core_ir_simple_loop_unswitch_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
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

void test_unswitches_invariant_header_condition() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("simple_unswitch");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp", one, one);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrSimpleLoopUnswitchPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *preheader_branch = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(preheader_branch != nullptr);
    assert(preheader_branch->get_true_block() == body);
    assert(preheader_branch->get_false_block() == exit);

    auto *header_jump = dynamic_cast<CoreIrJumpInst *>(
        header->get_instructions().back().get());
    assert(header_jump != nullptr);
    assert(header_jump->get_target_block() == body);
}

void test_unswitches_loop_body_invariant_condition() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("simple_unswitch_body");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *counter_slot =
        function->create_stack_slot<CoreIrStackSlot>("counter", i32_type, 4);
    auto *flag_slot =
        function->create_stack_slot<CoreIrStackSlot>("flag", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, counter_slot);
    entry->create_instruction<CoreIrStoreInst>(void_type, one, flag_slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *counter_load = header->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.load", counter_slot);
    auto *counter_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "counter.cmp", counter_load,
        three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, counter_cmp, body, exit);
    auto *flag_load = body->create_instruction<CoreIrLoadInst>(
        i32_type, "flag.load", flag_slot);
    auto *flag_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "flag.cmp", flag_load, zero);
    body->create_instruction<CoreIrCondJumpInst>(void_type, flag_cmp, then_block,
                                                 latch);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *counter_reload = latch->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.reload", counter_slot);
    auto *counter_next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "counter.next", counter_reload, one);
    latch->create_instruction<CoreIrStoreInst>(void_type, counter_next, counter_slot);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrSimpleLoopUnswitchPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_branch = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_branch != nullptr);
    assert(entry_branch->get_true_block() != header);
    assert(entry_branch->get_false_block() != header);

    CoreIrBasicBlock *body_true_clone = nullptr;
    CoreIrBasicBlock *body_false_clone = nullptr;
    for (const auto &block_ptr : function->get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        if (block_ptr->get_name() == "body.unsw.true") {
            body_true_clone = block_ptr.get();
        } else if (block_ptr->get_name() == "body.unsw.false") {
            body_false_clone = block_ptr.get();
        }
    }
    assert(body_true_clone != nullptr);
    assert(body_false_clone != nullptr);

    auto *true_jump = dynamic_cast<CoreIrJumpInst *>(
        body_true_clone->get_instructions().back().get());
    auto *false_jump = dynamic_cast<CoreIrJumpInst *>(
        body_false_clone->get_instructions().back().get());
    assert(true_jump != nullptr);
    assert(false_jump != nullptr);
    assert(true_jump->get_target_block()->get_name() == "then.unsw.true");
    assert(false_jump->get_target_block()->get_name() == "latch.unsw.false");
}

void test_unswitches_loop_body_compare_chain_from_invariant_access_path() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array2_i32 = context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *ptr_array2_i32 = context->create_type<CoreIrPointerType>(array2_i32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("simple_unswitch_body_access_path");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *counter_slot =
        function->create_stack_slot<CoreIrStackSlot>("counter", i32_type, 4);
    auto *state_slot =
        function->create_stack_slot<CoreIrStackSlot>("state", array2_i32, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    auto *entry_state_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_i32, "state.addr", state_slot);
    auto *entry_flag_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "state.flag.addr", entry_state_addr,
        std::vector<CoreIrValue *>{zero, zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, zero, counter_slot);
    entry->create_instruction<CoreIrStoreInst>(void_type, one, entry_flag_addr);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *counter_load = header->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.load", counter_slot);
    auto *counter_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "counter.cmp", counter_load,
        three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, counter_cmp, body, exit);
    auto *loop_state_addr = body->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_i32, "state.addr.loop", state_slot);
    auto *loop_flag_addr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "state.flag.addr.loop", loop_state_addr,
        std::vector<CoreIrValue *>{zero, zero});
    auto *flag_load = body->create_instruction<CoreIrLoadInst>(
        i32_type, "flag.load", loop_flag_addr);
    auto *flag_sum = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "flag.sum", flag_load, one);
    auto *flag_active = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "flag.active", flag_load, zero);
    auto *flag_large = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "flag.large", flag_sum, one);
    auto *combined = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::And, i1_type, "flag.combined", flag_active,
        flag_large);
    body->create_instruction<CoreIrCondJumpInst>(void_type, combined, then_block,
                                                 latch);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *counter_reload = latch->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.reload", counter_slot);
    auto *counter_next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "counter.next", counter_reload, one);
    latch->create_instruction<CoreIrStoreInst>(void_type, counter_next, counter_slot);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrSimpleLoopUnswitchPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_branch = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_branch != nullptr);
    assert(entry_branch->get_true_block() != header);
    assert(entry_branch->get_false_block() != header);

    CoreIrBasicBlock *body_true_clone = nullptr;
    CoreIrBasicBlock *body_false_clone = nullptr;
    for (const auto &block_ptr : function->get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        if (block_ptr->get_name() == "body.unsw.true") {
            body_true_clone = block_ptr.get();
        } else if (block_ptr->get_name() == "body.unsw.false") {
            body_false_clone = block_ptr.get();
        }
    }
    assert(body_true_clone != nullptr);
    assert(body_false_clone != nullptr);

    auto *true_jump = dynamic_cast<CoreIrJumpInst *>(
        body_true_clone->get_instructions().back().get());
    auto *false_jump = dynamic_cast<CoreIrJumpInst *>(
        body_false_clone->get_instructions().back().get());
    assert(true_jump != nullptr);
    assert(false_jump != nullptr);
    assert(true_jump->get_target_block()->get_name() == "then.unsw.true");
    assert(false_jump->get_target_block()->get_name() == "latch.unsw.false");
}

void test_unswitches_outer_loop_even_with_nested_inner_loop_phi() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("simple_unswitch_nested_phi");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *counter_slot =
        function->create_stack_slot<CoreIrStackSlot>("counter", i32_type, 4);
    auto *flag_slot =
        function->create_stack_slot<CoreIrStackSlot>("flag", i32_type, 4);
    auto *sum_slot =
        function->create_stack_slot<CoreIrStackSlot>("sum", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header =
        function->create_basic_block<CoreIrBasicBlock>("outer.header");
    auto *outer_body = function->create_basic_block<CoreIrBasicBlock>("outer.body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *inner_header =
        function->create_basic_block<CoreIrBasicBlock>("inner.header");
    auto *inner_body = function->create_basic_block<CoreIrBasicBlock>("inner.body");
    auto *inner_exit = function->create_basic_block<CoreIrBasicBlock>("inner.exit");
    auto *outer_exit = function->create_basic_block<CoreIrBasicBlock>("outer.exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, counter_slot);
    entry->create_instruction<CoreIrStoreInst>(void_type, one, flag_slot);
    entry->create_instruction<CoreIrStoreInst>(void_type, zero, sum_slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, outer_header);
    auto *counter_load = outer_header->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.load", counter_slot);
    auto *counter_cmp = outer_header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "counter.cmp", counter_load,
        three);
    outer_header->create_instruction<CoreIrCondJumpInst>(void_type, counter_cmp,
                                                         outer_body, outer_exit);
    auto *flag_load = outer_body->create_instruction<CoreIrLoadInst>(
        i32_type, "flag.load", flag_slot);
    auto *flag_cmp = outer_body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "flag.cmp", flag_load, zero);
    outer_body->create_instruction<CoreIrCondJumpInst>(void_type, flag_cmp, then_block,
                                                       inner_exit);
    then_block->create_instruction<CoreIrJumpInst>(void_type, inner_header);
    auto *inner_iv =
        inner_header->create_instruction<CoreIrPhiInst>(i32_type, "inner.iv");
    auto *inner_cmp = inner_header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "inner.cmp", inner_iv, two);
    inner_header->create_instruction<CoreIrCondJumpInst>(void_type, inner_cmp,
                                                         inner_body, inner_exit);
    auto *sum_load = inner_body->create_instruction<CoreIrLoadInst>(
        i32_type, "sum.load", sum_slot);
    auto *sum_next = inner_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum.next", sum_load, one);
    inner_body->create_instruction<CoreIrStoreInst>(void_type, sum_next, sum_slot);
    auto *inner_next = inner_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "inner.next", inner_iv, one);
    inner_body->create_instruction<CoreIrJumpInst>(void_type, inner_header);
    auto *counter_reload = inner_exit->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.reload", counter_slot);
    auto *counter_next = inner_exit->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "counter.next", counter_reload, one);
    inner_exit->create_instruction<CoreIrStoreInst>(void_type, counter_next, counter_slot);
    inner_exit->create_instruction<CoreIrJumpInst>(void_type, outer_header);
    auto *sum_exit = outer_exit->create_instruction<CoreIrLoadInst>(
        i32_type, "sum.exit", sum_slot);
    outer_exit->create_instruction<CoreIrReturnInst>(void_type, sum_exit);
    inner_iv->add_incoming(then_block, zero);
    inner_iv->add_incoming(inner_body, inner_next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrSimpleLoopUnswitchPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_branch = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_branch != nullptr);
}

void test_stops_unswitch_after_depth_budget() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("simple_unswitch_depth_budget");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *counter_slot =
        function->create_stack_slot<CoreIrStackSlot>("counter", i32_type, 4);
    auto *flag_slot =
        function->create_stack_slot<CoreIrStackSlot>("flag", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>(
        "header.unsw.true.unsw.false");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, counter_slot);
    entry->create_instruction<CoreIrStoreInst>(void_type, one, flag_slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *counter_load = header->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.load", counter_slot);
    auto *counter_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "counter.cmp", counter_load,
        three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, counter_cmp, body, exit);
    auto *flag_load = body->create_instruction<CoreIrLoadInst>(
        i32_type, "flag.load", flag_slot);
    auto *flag_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "flag.cmp", flag_load, zero);
    body->create_instruction<CoreIrCondJumpInst>(void_type, flag_cmp, then_block,
                                                 latch);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *counter_reload = latch->create_instruction<CoreIrLoadInst>(
        i32_type, "counter.reload", counter_slot);
    auto *counter_next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "counter.next", counter_reload, one);
    latch->create_instruction<CoreIrStoreInst>(void_type, counter_next, counter_slot);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    const std::size_t block_count_before = function->get_basic_blocks().size();
    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrSimpleLoopUnswitchPass pass;
    assert(pass.Run(compiler_context).ok);
    assert(function->get_basic_blocks().size() == block_count_before);
}

} // namespace

int main() {
    test_unswitches_invariant_header_condition();
    test_unswitches_loop_body_invariant_condition();
    test_unswitches_loop_body_compare_chain_from_invariant_access_path();
    test_unswitches_outer_loop_even_with_nested_inner_loop_phi();
    test_stops_unswitch_after_depth_budget();
    return 0;
}
