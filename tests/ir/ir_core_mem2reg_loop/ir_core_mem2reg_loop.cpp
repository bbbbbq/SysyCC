#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

void test_simple_loop() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_mem2reg_loop");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);

    auto *header_load =
        header->create_instruction<CoreIrLoadInst>(i32_type, "header.value", slot);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "header.cmp", header_load,
        three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);

    auto *body_load =
        body->create_instruction<CoreIrLoadInst>(i32_type, "body.value", slot);
    auto *next = body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add,
                                                            i32_type, "body.next",
                                                            body_load, one);
    body->create_instruction<CoreIrStoreInst>(void_type, next, slot);
    body->create_instruction<CoreIrJumpInst>(void_type, header);

    auto *exit_load =
        exit->create_instruction<CoreIrLoadInst>(i32_type, "exit.value", slot);
    exit->create_instruction<CoreIrReturnInst>(void_type, exit_load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrMem2RegPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("phi i32 [ 0, %entry ], [ %body.next, %body ]") !=
           std::string::npos);
    assert(text.find("load i32, %value") == std::string::npos);
    assert(text.find("store i32 0, %value") == std::string::npos);
}

void test_nested_reinitialized_loop() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_mem2reg_loop_reinit");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);

    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header =
        function->create_basic_block<CoreIrBasicBlock>("outer.header");
    auto *init = function->create_basic_block<CoreIrBasicBlock>("init");
    auto *inner_header =
        function->create_basic_block<CoreIrBasicBlock>("inner.header");
    auto *inner_body =
        function->create_basic_block<CoreIrBasicBlock>("inner.body");
    auto *inner_exit =
        function->create_basic_block<CoreIrBasicBlock>("inner.exit");
    auto *outer_latch =
        function->create_basic_block<CoreIrBasicBlock>("outer.latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");

    auto *slot = function->create_stack_slot<CoreIrStackSlot>("acc", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, outer_header);

    auto *outer_iv =
        outer_header->create_instruction<CoreIrPhiInst>(i32_type, "outer.iv");
    auto *outer_cmp = outer_header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "outer.cmp", outer_iv, two);
    outer_header->create_instruction<CoreIrCondJumpInst>(void_type, outer_cmp, init,
                                                         exit);

    init->create_instruction<CoreIrStoreInst>(void_type, zero, slot);
    init->create_instruction<CoreIrJumpInst>(void_type, inner_header);

    auto *inner_iv =
        inner_header->create_instruction<CoreIrPhiInst>(i32_type, "inner.iv");
    auto *inner_cmp = inner_header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "inner.cmp", inner_iv,
        three);
    inner_header->create_instruction<CoreIrCondJumpInst>(
        void_type, inner_cmp, inner_body, inner_exit);

    auto *inner_load =
        inner_body->create_instruction<CoreIrLoadInst>(i32_type, "acc.load", slot);
    auto *next_acc = inner_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "acc.next", inner_load, one);
    inner_body->create_instruction<CoreIrStoreInst>(void_type, next_acc, slot);
    auto *inner_next = inner_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "inner.next", inner_iv, one);
    inner_body->create_instruction<CoreIrJumpInst>(void_type, inner_header);

    inner_exit->create_instruction<CoreIrJumpInst>(void_type, outer_latch);

    auto *outer_next = outer_latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "outer.next", outer_iv, one);
    outer_latch->create_instruction<CoreIrJumpInst>(void_type, outer_header);

    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    outer_iv->add_incoming(entry, zero);
    outer_iv->add_incoming(outer_latch, outer_next);
    inner_iv->add_incoming(init, zero);
    inner_iv->add_incoming(inner_body, inner_next);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrMem2RegPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("stackslot %acc") == std::string::npos);
    assert(text.find("load i32, %acc") == std::string::npos);
    assert(text.find("store i32 0, %acc") == std::string::npos);
    assert(text.find("phi i32") != std::string::npos);
}

} // namespace

int main() {
    test_simple_loop();
    test_nested_reinitialized_loop();

    return 0;
}
