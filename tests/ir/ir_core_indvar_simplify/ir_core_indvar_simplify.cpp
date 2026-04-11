#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/indvar_simplify/core_ir_indvar_simplify_pass.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
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

void assert_module_verifies(const CoreIrModule &module) {
    CoreIrVerifier verifier;
    assert(verifier.verify_module(module).ok);
}

void test_normalizes_bound_cmp_into_iv_cmp_bound() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("indvar_simplify");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "cmp", ten, iv);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    assert(cmp->get_lhs() == iv);
    assert(cmp->get_rhs() == ten);
    assert(cmp->get_predicate() == CoreIrComparePredicate::SignedLess);
}

void test_strength_reduces_scaled_iv_multiply() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("indvar_strength_reduce");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);
    auto *eight = context->create_constant<CoreIrConstantInt>(i32_type, 8);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, four);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *scaled = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "scaled", iv, eight);
    auto *sink = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sink", scaled, one);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%scaled = mul i32 %iv, 8") == std::string::npos);
    assert(text.find("%scaled.loop.") != std::string::npos);
}

void test_strength_reduces_scaled_iv_with_non_unit_step() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("indvar_strength_reduce_step2");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *eight = context->create_constant<CoreIrConstantInt>(i32_type, 8);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, eight);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *scaled = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "scaled", iv, two);
    body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type, "sink",
                                               scaled, one);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, two);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%scaled = mul i32 %iv, 2") == std::string::npos);
    assert(text.find("%scaled.loop.") != std::string::npos);
}

void test_strength_reduces_affine_iv_chain() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("indvar_strength_reduce_affine");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);
    auto *eight = context->create_constant<CoreIrConstantInt>(i32_type, 8);
    auto *hundred = context->create_constant<CoreIrConstantInt>(i32_type, 100);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, four);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *scaled = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "scaled", iv, eight);
    auto *offset = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "offset", hundred, scaled);
    body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type, "sink",
                                               offset, one);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%scaled = mul i32 %iv, 8") == std::string::npos);
    assert(text.find("%offset = add i32 100") == std::string::npos);
    assert(text.find("%offset.loop.") != std::string::npos);
}

void test_strength_reduces_scaled_iv_with_negative_step() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("indvar_strength_reduce_negative_step");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "cmp", iv, zero);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *scaled = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "scaled", iv, two);
    body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type, "sink",
                                               scaled, one);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, four);
    iv->add_incoming(entry, four);
    iv->add_incoming(body, next);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%scaled = mul i32 %iv, 2") == std::string::npos);
    assert(text.find("%scaled.loop.") != std::string::npos);
}

void test_strength_reduces_readonly_row_bound_recurrence() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i32_ptr_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type, i32_ptr_type}, false);
    auto *module =
        context->create_module<CoreIrModule>("indvar_row_bound_recurrence");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *n_param = function->create_parameter<CoreIrParameter>(i32_type, "n");
    auto *xptr_param =
        function->create_parameter<CoreIrParameter>(i32_ptr_type, "xptr");
    function->set_parameter_readonly({false, true});

    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body0 = function->create_basic_block<CoreIrBasicBlock>("body0");
    auto *cond1 = function->create_basic_block<CoreIrBasicBlock>("cond1");
    auto *body1 = function->create_basic_block<CoreIrBasicBlock>("body1");
    auto *end1 = function->create_basic_block<CoreIrBasicBlock>("end1");
    auto *cond2 = function->create_basic_block<CoreIrBasicBlock>("cond2");
    auto *body2 = function->create_basic_block<CoreIrBasicBlock>("body2");
    auto *latch = function->create_basic_block<CoreIrBasicBlock>("latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");

    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, n_param);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body0, exit);

    auto *seed_addr0 = body0->create_instruction<CoreIrGetElementPtrInst>(
        i32_ptr_type, "seed.addr0", xptr_param, std::vector<CoreIrValue *>{iv});
    auto *seed0 =
        body0->create_instruction<CoreIrLoadInst>(i32_type, "seed0", seed_addr0);
    body0->create_instruction<CoreIrJumpInst>(void_type, cond1);

    auto *cursor0 = cond1->create_instruction<CoreIrPhiInst>(i32_type, "cursor0");
    auto *end_index0 = cond1->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "end.index0", iv, one);
    auto *end_addr0 = cond1->create_instruction<CoreIrGetElementPtrInst>(
        i32_ptr_type, "end.addr0", xptr_param,
        std::vector<CoreIrValue *>{end_index0});
    auto *end0 =
        cond1->create_instruction<CoreIrLoadInst>(i32_type, "end0", end_addr0);
    auto *cmp0 = cond1->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp0", cursor0, end0);
    cond1->create_instruction<CoreIrCondJumpInst>(void_type, cmp0, body1, end1);

    auto *next_cursor0 = body1->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next.cursor0", cursor0, one);
    body1->create_instruction<CoreIrJumpInst>(void_type, cond1);

    auto *seed_addr1 = end1->create_instruction<CoreIrGetElementPtrInst>(
        i32_ptr_type, "seed.addr1", xptr_param, std::vector<CoreIrValue *>{iv});
    auto *seed1 =
        end1->create_instruction<CoreIrLoadInst>(i32_type, "seed1", seed_addr1);
    end1->create_instruction<CoreIrJumpInst>(void_type, cond2);

    auto *cursor1 = cond2->create_instruction<CoreIrPhiInst>(i32_type, "cursor1");
    auto *end_index1 = cond2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "end.index1", iv, one);
    auto *end_addr1 = cond2->create_instruction<CoreIrGetElementPtrInst>(
        i32_ptr_type, "end.addr1", xptr_param,
        std::vector<CoreIrValue *>{end_index1});
    auto *end1_load =
        cond2->create_instruction<CoreIrLoadInst>(i32_type, "end1", end_addr1);
    auto *cmp1 = cond2->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp1", cursor1, end1_load);
    cond2->create_instruction<CoreIrCondJumpInst>(void_type, cmp1, body2, latch);

    auto *next_cursor1 = body2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next.cursor1", cursor1, one);
    body2->create_instruction<CoreIrJumpInst>(void_type, cond2);

    auto *next_iv = latch->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next.iv", iv, one);
    latch->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(latch, next_iv);
    cursor0->add_incoming(body0, seed0);
    cursor0->add_incoming(body1, next_cursor0);
    cursor1->add_incoming(end1, seed1);
    cursor1->add_incoming(body2, next_cursor1);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrIndVarSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);
    assert_module_verifies(*module);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%xptr.row.start.loop.") != std::string::npos);
    assert(text.find("%seed0 = load i32") == std::string::npos);
    assert(text.find("%seed1 = load i32") == std::string::npos);
    assert(text.find("%end1 = load i32") == std::string::npos);
}

} // namespace

int main() {
    test_normalizes_bound_cmp_into_iv_cmp_bound();
    test_strength_reduces_scaled_iv_multiply();
    test_strength_reduces_scaled_iv_with_non_unit_step();
    test_strength_reduces_affine_iv_chain();
    test_strength_reduces_scaled_iv_with_negative_step();
    test_strength_reduces_readonly_row_bound_recurrence();
    return 0;
}
