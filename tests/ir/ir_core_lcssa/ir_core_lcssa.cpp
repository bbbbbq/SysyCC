#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"
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

void test_inserts_exit_phi_for_single_exit_loop() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("lcssa_single_exit");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *loop_value = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "loop.sum", iv, two);
    header->create_instruction<CoreIrCondJumpInst>(void_type, one, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *outside_use = exit->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "outside", loop_value, two);
    exit->create_instruction<CoreIrReturnInst>(void_type, outside_use);

    iv->add_incoming(entry, zero);
    iv->add_incoming(body, loop_value);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLcssaPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *exit_phi =
        dynamic_cast<CoreIrPhiInst *>(exit->get_instructions().front().get());
    assert(exit_phi != nullptr);
    assert(exit_phi->get_incoming_count() == 1);
    assert(exit_phi->get_incoming_block(0) == header);
    assert(exit_phi->get_incoming_value(0) == loop_value);
    assert(outside_use->get_lhs() == exit_phi);
}

void test_inserts_exit_phis_for_multi_exit_phi_users() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("lcssa_multi_exit");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit_a = function->create_basic_block<CoreIrBasicBlock>("exit_a");
    auto *exit_b = function->create_basic_block<CoreIrBasicBlock>("exit_b");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *loop_value = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "loop.sum", iv, two);
    header->create_instruction<CoreIrCondJumpInst>(void_type, one, body,
                                                   exit_a);
    body->create_instruction<CoreIrCondJumpInst>(void_type, one, latch, exit_b);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit_a->create_instruction<CoreIrJumpInst>(void_type, merge);
    exit_b->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *outside_phi =
        merge->create_instruction<CoreIrPhiInst>(i32_type, "outside.phi");
    auto *outside_use = merge->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "outside", outside_phi, two);
    merge->create_instruction<CoreIrReturnInst>(void_type, outside_use);

    iv->add_incoming(entry, zero);
    iv->add_incoming(latch, loop_value);
    outside_phi->add_incoming(exit_a, loop_value);
    outside_phi->add_incoming(exit_b, loop_value);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLcssaPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *exit_a_phi =
        dynamic_cast<CoreIrPhiInst *>(exit_a->get_instructions().front().get());
    auto *exit_b_phi =
        dynamic_cast<CoreIrPhiInst *>(exit_b->get_instructions().front().get());
    assert(exit_a_phi != nullptr);
    assert(exit_b_phi != nullptr);
    assert(exit_a_phi->get_incoming_count() == 1);
    assert(exit_b_phi->get_incoming_count() == 1);
    assert(exit_a_phi->get_incoming_block(0) == header);
    assert(exit_b_phi->get_incoming_block(0) == body);
    assert(outside_phi->get_incoming_value(0) == exit_a_phi);
    assert(outside_phi->get_incoming_value(1) == exit_b_phi);
}

} // namespace

int main() {
    test_inserts_exit_phi_for_single_exit_loop();
    test_inserts_exit_phis_for_multi_exit_phi_users();
    return 0;
}
