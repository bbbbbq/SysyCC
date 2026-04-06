#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
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
    auto *module =
        context->create_module<CoreIrModule>("ir_core_sccp_phi_constant");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("cond", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    auto *loaded =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "loaded", slot);
    auto *condition = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cond", loaded, zero);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, condition, left, right);
    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    phi->add_incoming(left, three);
    phi->add_incoming(right, three);
    merge->create_instruction<CoreIrReturnInst>(void_type, phi);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSccpPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("ret i32 3") != std::string::npos);
    return 0;
}
