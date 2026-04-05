#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

void test_constant_branch_condition() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_sccp_constant_branch_condition");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block = function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *one_i32 = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    auto *condition = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cond", one_i32, one_i32);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, condition, true_block,
                                                  false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, seven);
    false_block->create_instruction<CoreIrReturnInst>(void_type, nine);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSccpPass sccp_pass;
    CoreIrConstFoldPass const_fold_pass;
    CoreIrDcePass dce_pass;
    assert(sccp_pass.Run(compiler_context).ok);
    assert(const_fold_pass.Run(compiler_context).ok);
    assert(dce_pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("condjump") == std::string::npos);
    assert(text.find("false_block:") == std::string::npos);
    assert(text.find("ret i32 7") != std::string::npos);
}

void test_signed_narrow_compare_branch_condition() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i8_type = context->create_type<CoreIrIntegerType>(8);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_sccp_signed_narrow_compare");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block = function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *neg_one = context->create_constant<CoreIrConstantInt>(i8_type, 0xFF);
    auto *zero_i8 = context->create_constant<CoreIrConstantInt>(i8_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    auto *condition = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cond", neg_one, zero_i8);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, condition, true_block,
                                                  false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, seven);
    false_block->create_instruction<CoreIrReturnInst>(void_type, nine);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSccpPass sccp_pass;
    CoreIrConstFoldPass const_fold_pass;
    CoreIrDcePass dce_pass;
    assert(sccp_pass.Run(compiler_context).ok);
    assert(const_fold_pass.Run(compiler_context).ok);
    assert(dce_pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("false_block:") == std::string::npos);
    assert(text.find("ret i32 7") != std::string::npos);
}

void test_signed_narrow_division_fold() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i8_type = context->create_type<CoreIrIntegerType>(8);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i8_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_sccp_signed_narrow_division");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *neg_four = context->create_constant<CoreIrConstantInt>(i8_type, 0xFC);
    auto *two = context->create_constant<CoreIrConstantInt>(i8_type, 2);

    auto *division = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SDiv, i8_type, "div", neg_four, two);
    entry->create_instruction<CoreIrReturnInst>(void_type, division);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSccpPass sccp_pass;
    assert(sccp_pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("ret i8 254") != std::string::npos);
}

} // namespace

int main() {
    test_constant_branch_condition();
    test_signed_narrow_compare_branch_condition();
    test_signed_narrow_division_fold();
    return 0;
}
