#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
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
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_gvn_dominated_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *child = function->create_basic_block<CoreIrBasicBlock>("child");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", address, std::vector<CoreIrValue *>{one});
    entry->create_instruction<CoreIrJumpInst>(void_type, child);
    auto *gep1 = child->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep1", address, std::vector<CoreIrValue *>{one});
    child->create_instruction<CoreIrStoreInst>(void_type, seven, gep1);
    child->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGvnPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%gep1 = getelementptr") == std::string::npos);
    assert(text.find("store i32 7, %gep0") != std::string::npos);
    return 0;
}
