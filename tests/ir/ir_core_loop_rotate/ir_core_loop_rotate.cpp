#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/loop_rotate/core_ir_loop_rotate_pass.hpp"
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

void test_rotates_phi_free_loop_header() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_rotate_ok");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp", one, one);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add,
                                               i32_type, "body.add", one, two);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLoopRotatePass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_term = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_term != nullptr);
    assert(entry_term->get_true_block() == body);
    assert(entry_term->get_false_block() == exit);
}

void test_rotates_header_with_phi_and_exit_use() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_rotate_skip");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, one);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *ret = exit->create_instruction<CoreIrReturnInst>(void_type, iv);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, one);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLoopRotatePass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_term = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_term != nullptr);
    assert(entry_term->get_true_block() == header);
    assert(entry_term->get_false_block() == exit);

    auto *header_term =
        dynamic_cast<CoreIrJumpInst *>(header->get_instructions().back().get());
    assert(header_term != nullptr);
    assert(header_term->get_target_block() == body);

    auto *body_term =
        dynamic_cast<CoreIrCondJumpInst *>(body->get_instructions().back().get());
    assert(body_term != nullptr);
    assert(body_term->get_true_block() == header);
    assert(body_term->get_false_block() == exit);

    auto *exit_phi =
        dynamic_cast<CoreIrPhiInst *>(exit->get_instructions().front().get());
    assert(exit_phi != nullptr);
    assert(exit_phi->get_incoming_count() == 2);
    assert(exit_phi->get_incoming_block(0) == entry ||
           exit_phi->get_incoming_block(1) == entry);
    assert(exit_phi->get_incoming_block(0) == body ||
           exit_phi->get_incoming_block(1) == body);
    assert(ret->get_return_value() == exit_phi);
}

void test_rotates_real_cfg_header_compare_chain() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_rotate_chain");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *sum = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, one);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", sum, two);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add,
                                               i32_type, "body.add", one, two);
    body->create_instruction<CoreIrJumpInst>(void_type, latch);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLoopRotatePass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_term = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(entry_term != nullptr);
    assert(entry_term->get_true_block() == body);
    assert(entry_term->get_false_block() == exit);
    assert(entry->get_instructions().size() == 3);
}

} // namespace

int main() {
    test_rotates_phi_free_loop_header();
    test_rotates_header_with_phi_and_exit_use();
    test_rotates_real_cfg_header_compare_chain();
    return 0;
}
