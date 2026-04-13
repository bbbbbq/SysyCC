#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_dce_pass");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *unused_block =
        function->create_basic_block<CoreIrBasicBlock>("unreachable");
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *cond_true = context->create_constant<CoreIrConstantInt>(i1_type, 1);

    entry->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type,
                                                "dead", one, two);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond_true, left, right);

    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);

    auto *phi1 = merge->create_instruction<CoreIrPhiInst>(i32_type, "phi1");
    auto *phi2 = merge->create_instruction<CoreIrPhiInst>(i32_type, "phi2");
    phi1->add_incoming(left, one);
    phi1->add_incoming(right, phi2);
    phi2->add_incoming(left, phi1);
    phi2->add_incoming(right, two);
    merge->create_instruction<CoreIrReturnInst>(void_type, one);

    unused_block->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "also_dead", one, two);
    unused_block->create_instruction<CoreIrReturnInst>(void_type, two);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrDcePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%dead = add") == std::string::npos);
    assert(text.find("%phi1 = phi") == std::string::npos);
    assert(text.find("%phi2 = phi") == std::string::npos);
    assert(text.find("unreachable:") == std::string::npos);
    return 0;
}
