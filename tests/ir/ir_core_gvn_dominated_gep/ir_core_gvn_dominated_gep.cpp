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
    auto *array4_i32_type = context->create_type<CoreIrArrayType>(i32_type, 4);
    auto *array2_row_type =
        context->create_type<CoreIrArrayType>(array4_i32_type, 2);
    auto *ptr_array2_row_type =
        context->create_type<CoreIrPointerType>(array2_row_type);
    auto *ptr_array4_i32_type =
        context->create_type<CoreIrPointerType>(array4_i32_type);
    auto *entry_ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *child_ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_gvn_dominated_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *child = function->create_basic_block<CoreIrBasicBlock>("child");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("matrix",
                                                              array2_row_type, 32);
    auto *zero_row = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one_row = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *zero_nested = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *two_nested = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *zero_flat = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one_flat = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two_flat = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_row_type, "addr", slot);
    auto *row_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array4_i32_type, "row.ptr", address,
        std::vector<CoreIrValue *>{zero_row, one_row});
    auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        entry_ptr_i32_type, "gep0", row_ptr,
        std::vector<CoreIrValue *>{zero_nested, two_nested});
    entry->create_instruction<CoreIrJumpInst>(void_type, child);
    auto *gep1 = child->create_instruction<CoreIrGetElementPtrInst>(
        child_ptr_i32_type, "gep1", address,
        std::vector<CoreIrValue *>{zero_flat, one_flat, two_flat});
    child->create_instruction<CoreIrStoreInst>(void_type, seven, gep1);
    child->create_instruction<CoreIrReturnInst>(void_type, zero_flat);

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
