#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/if_conversion/core_ir_if_conversion_pass.hpp"
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

void test_if_converts_pure_loop_diamond() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_if_conversion");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *else_block = function->create_basic_block<CoreIrBasicBlock>("else");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *header_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, header_cmp, body,
                                                   exit);
    auto *body_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "body.cmp", iv, two);
    body->create_instruction<CoreIrCondJumpInst>(void_type, body_cmp,
                                                 then_block, else_block);
    auto *true_value = then_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "true.v", iv, one);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *false_value = else_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "false.v", iv, one);
    else_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *merged = latch->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    auto *next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", merged, one);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(latch, next);
    merged->add_incoming(then_block, true_value);
    merged->add_incoming(else_block, false_value);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIfConversionPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *body_jump =
        dynamic_cast<CoreIrJumpInst *>(body->get_instructions().back().get());
    assert(body_jump != nullptr);
    assert(body_jump->get_target_block() == latch);

    bool saw_select = false;
    for (const auto &instruction_ptr : body->get_instructions()) {
        if (dynamic_cast<CoreIrSelectInst *>(instruction_ptr.get()) !=
            nullptr) {
            saw_select = true;
            break;
        }
    }
    assert(saw_select);

    assert(dynamic_cast<CoreIrPhiInst *>(
               latch->get_instructions().front().get()) == nullptr);
}

void test_if_converts_triangle_merge() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_if_conversion_triangle");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *side = function->create_basic_block<CoreIrBasicBlock>("side");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *header_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, header_cmp, body,
                                                   exit);

    auto *body_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "body.cmp", iv, two);
    body->create_instruction<CoreIrCondJumpInst>(void_type, body_cmp, merge,
                                                 side);

    auto *side_value = side->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "side.v", iv, one);
    side->create_instruction<CoreIrJumpInst>(void_type, merge);

    auto *merged = merge->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    auto *next = merge->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", merged, one);
    merge->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(merge, next);
    merged->add_incoming(body, iv);
    merged->add_incoming(side, side_value);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIfConversionPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *body_jump =
        dynamic_cast<CoreIrJumpInst *>(body->get_instructions().back().get());
    assert(body_jump != nullptr);
    assert(body_jump->get_target_block() == merge);

    bool saw_select = false;
    for (const auto &instruction_ptr : body->get_instructions()) {
        if (dynamic_cast<CoreIrSelectInst *>(instruction_ptr.get()) !=
            nullptr) {
            saw_select = true;
            break;
        }
    }
    assert(saw_select);
    assert(dynamic_cast<CoreIrPhiInst *>(
               merge->get_instructions().front().get()) == nullptr);
}

void test_rejects_effectful_diamond() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_if_conversion_effectful");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("slot", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *else_block = function->create_basic_block<CoreIrBasicBlock>("else");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *header_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, header_cmp, body,
                                                   exit);
    auto *body_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "body.cmp", iv, one);
    body->create_instruction<CoreIrCondJumpInst>(void_type, body_cmp,
                                                 then_block, else_block);
    then_block->create_instruction<CoreIrStoreInst>(void_type, one, slot);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *false_value = else_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "false.v", iv, one);
    else_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *merged = latch->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    auto *next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", merged, one);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(latch, next);
    merged->add_incoming(then_block, one);
    merged->add_incoming(else_block, false_value);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIfConversionPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *body_branch = dynamic_cast<CoreIrCondJumpInst *>(
        body->get_instructions().back().get());
    assert(body_branch != nullptr);
}

void test_if_converts_same_address_store_diamond() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_if_conversion_store");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *else_block = function->create_basic_block<CoreIrBasicBlock>("else");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("slot", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *header_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, header_cmp, body,
                                                   exit);
    auto *slot_addr =
        body->create_instruction<CoreIrAddressOfStackSlotInst>(ptr_type, "slot.addr", slot);
    auto *body_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "body.cmp", iv, two);
    body->create_instruction<CoreIrCondJumpInst>(void_type, body_cmp,
                                                 then_block, else_block);
    auto *true_value = then_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "true.v", iv, one);
    then_block->create_instruction<CoreIrStoreInst>(void_type, true_value,
                                                    slot_addr);
    then_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *false_value = else_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "false.v", iv, one);
    else_block->create_instruction<CoreIrStoreInst>(void_type, false_value,
                                                    slot_addr);
    else_block->create_instruction<CoreIrJumpInst>(void_type, latch);
    auto *next = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(latch, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIfConversionPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *body_jump =
        dynamic_cast<CoreIrJumpInst *>(body->get_instructions().back().get());
    assert(body_jump != nullptr);
    assert(body_jump->get_target_block() == latch);

    std::size_t store_count = 0;
    bool saw_select = false;
    for (const auto &instruction_ptr : body->get_instructions()) {
        if (dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get()) != nullptr) {
            ++store_count;
        }
        if (dynamic_cast<CoreIrSelectInst *>(instruction_ptr.get()) != nullptr) {
            saw_select = true;
        }
    }
    assert(store_count == 1);
    assert(saw_select);
}

void test_if_converts_short_circuit_bool() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_if_conversion_short");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *rhs = function->create_basic_block<CoreIrBasicBlock>("rhs");
    auto *false_block = function->create_basic_block<CoreIrBasicBlock>("false");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *header_cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, header_cmp, body,
                                                   exit);
    auto *outer_cmp = body->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "outer.cmp", iv, two);
    body->create_instruction<CoreIrCondJumpInst>(void_type, outer_cmp, rhs,
                                                 false_block);
    auto *inner_cmp = rhs->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "inner.cmp", iv, one);
    rhs->create_instruction<CoreIrCondJumpInst>(void_type, inner_cmp, merge,
                                                false_block);
    false_block->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "short.phi");
    auto *next = merge->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    merge->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(merge, next);
    phi->add_incoming(rhs, one);
    phi->add_incoming(false_block, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIfConversionPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *body_jump =
        dynamic_cast<CoreIrJumpInst *>(body->get_instructions().back().get());
    assert(body_jump != nullptr);
    assert(body_jump->get_target_block() == merge);

    bool saw_select = false;
    for (const auto &instruction_ptr : body->get_instructions()) {
        if (dynamic_cast<CoreIrSelectInst *>(instruction_ptr.get()) != nullptr) {
            saw_select = true;
        }
    }
    assert(saw_select);
    assert(dynamic_cast<CoreIrPhiInst *>(merge->get_instructions().front().get()) ==
           nullptr);
}

} // namespace

int main() {
    test_if_converts_pure_loop_diamond();
    test_if_converts_triangle_merge();
    test_rejects_effectful_diamond();
    test_if_converts_same_address_store_diamond();
    test_if_converts_short_circuit_bool();
    return 0;
}
