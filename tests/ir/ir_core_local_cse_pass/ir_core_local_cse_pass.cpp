#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
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

namespace {

CompilerContext make_compiler_context(std::unique_ptr<CoreIrContext> context,
                                      CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    return compiler_context;
}

void test_eliminates_duplicate_binary_and_gep_in_block() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_local_cse_pass");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", address, std::vector<CoreIrValue *>{one});
    auto *gep1 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep1", address, std::vector<CoreIrValue *>{one});
    auto *sum0 = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum0", one, two);
    auto *sum1 = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum1", one, two);
    auto *final_sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum2", sum1, two);
    entry->create_instruction<CoreIrStoreInst>(void_type, final_sum, gep1);
    entry->create_instruction<CoreIrReturnInst>(void_type, sum1);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);

    CoreIrLocalCsePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%gep1 = getelementptr") == std::string::npos);
    assert(text.find("%sum1 = add") == std::string::npos);
    assert(text.find("store i32 %sum2, %gep0") != std::string::npos);
    assert(text.find("ret i32 %sum0") != std::string::npos);
    assert(sum0->get_uses().size() >= 1);
    assert(gep0->get_uses().size() >= 1);
}

void test_eliminates_structurally_equivalent_nested_geps() {
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
    auto *nested_ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *flat_ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_local_cse_structural_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("matrix",
                                                              array2_row_type, 32);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_row_type, "matrix.addr", slot);
    auto *row_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array4_i32_type, "row.ptr", address,
        std::vector<CoreIrValue *>{zero, one});
    auto *nested_elem = entry->create_instruction<CoreIrGetElementPtrInst>(
        nested_ptr_i32_type, "nested.elem", row_ptr,
        std::vector<CoreIrValue *>{zero, two});
    auto *flat_elem = entry->create_instruction<CoreIrGetElementPtrInst>(
        flat_ptr_i32_type, "flat.elem", address,
        std::vector<CoreIrValue *>{zero, one, two});
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, flat_elem);
    entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);

    CoreIrLocalCsePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%flat.elem = getelementptr") == std::string::npos);
    assert(text.find("store i32 7, %nested.elem") != std::string::npos);
}

} // namespace

int main() {
    test_eliminates_duplicate_binary_and_gep_in_block();
    test_eliminates_structurally_equivalent_nested_geps();
    return 0;
}
