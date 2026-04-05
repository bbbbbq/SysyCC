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

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("phi i32 [ 0, %entry ], [ %body.next, %body ]") !=
           std::string::npos);
    assert(text.find("load i32, %value") == std::string::npos);
    assert(text.find("store i32 0, %value") == std::string::npos);
    return 0;
}
