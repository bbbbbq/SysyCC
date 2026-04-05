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
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_mem2reg_if_else");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond, left, right);
    left->create_instruction<CoreIrStoreInst>(void_type, one, slot);
    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrStoreInst>(void_type, two, slot);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *load = merge->create_instruction<CoreIrLoadInst>(i32_type, "merged", slot);
    merge->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrMem2RegPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("phi i32 [ 1, %left ], [ 2, %right ]") != std::string::npos);
    assert(text.find("load i32, %value") == std::string::npos);
    assert(text.find("store i32 1, %value") == std::string::npos);
    assert(function->get_stack_slots().empty());
    return 0;
}
