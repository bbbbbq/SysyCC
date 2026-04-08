#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
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
    auto *ptr_array4_i32_type =
        context->create_type<CoreIrPointerType>(array4_i32_type);
    auto *ptr_array2_row_type =
        context->create_type<CoreIrPointerType>(array2_row_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *indexed_function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *nested_indexed_function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type, i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_dead_store_elimination_pass");

    auto *overwrite_function = module->create_function<CoreIrFunction>(
        "overwrite", function_type, false);
    auto *overwrite_entry =
        overwrite_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *overwrite_slot = overwrite_function->create_stack_slot<CoreIrStackSlot>(
        "value", i32_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    overwrite_entry->create_instruction<CoreIrStoreInst>(void_type, one,
                                                         overwrite_slot);
    overwrite_entry->create_instruction<CoreIrStoreInst>(void_type, two,
                                                         overwrite_slot);
    auto *overwrite_load =
        overwrite_entry->create_instruction<CoreIrLoadInst>(i32_type, "t0",
                                                            overwrite_slot);
    overwrite_entry->create_instruction<CoreIrReturnInst>(void_type, overwrite_load);

    auto *barrier_function = module->create_function<CoreIrFunction>(
        "barrier", function_type, false);
    auto *barrier_entry =
        barrier_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *barrier_slot =
        barrier_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    barrier_entry->create_instruction<CoreIrStoreInst>(void_type, one, barrier_slot);
    barrier_entry->create_instruction<CoreIrCallInst>(i32_type, "call.tmp",
                                                      "touch", callee_type,
                                                      std::vector<CoreIrValue *>{});
    barrier_entry->create_instruction<CoreIrStoreInst>(void_type, two, barrier_slot);
    auto *barrier_load =
        barrier_entry->create_instruction<CoreIrLoadInst>(i32_type, "t1",
                                                          barrier_slot);
    barrier_entry->create_instruction<CoreIrReturnInst>(void_type, barrier_load);

    auto *dynamic_function = module->create_function<CoreIrFunction>(
        "dynamic_exact", indexed_function_type, false);
    auto *dynamic_index =
        dynamic_function->create_parameter<CoreIrParameter>(i32_type, "idx");
    auto *dynamic_entry =
        dynamic_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *dynamic_slot = dynamic_function->create_stack_slot<CoreIrStackSlot>(
        "values", array4_i32_type, 4);
    auto *zero_dynamic = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *dynamic_base = dynamic_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32_type, "values.addr", dynamic_slot);
    auto *dynamic_addr = dynamic_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "elt.addr", dynamic_base,
        std::vector<CoreIrValue *>{zero_dynamic, dynamic_index});
    dynamic_entry->create_instruction<CoreIrStoreInst>(void_type, one, dynamic_addr);
    dynamic_entry->create_instruction<CoreIrStoreInst>(void_type, two, dynamic_addr);
    auto *dynamic_load = dynamic_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "t2", dynamic_addr);
    dynamic_entry->create_instruction<CoreIrReturnInst>(void_type, dynamic_load);

    auto *nested_function = module->create_function<CoreIrFunction>(
        "dynamic_nested_exact", nested_indexed_function_type, false);
    auto *row_index =
        nested_function->create_parameter<CoreIrParameter>(i32_type, "row");
    auto *column_index =
        nested_function->create_parameter<CoreIrParameter>(i32_type, "col");
    auto *nested_entry =
        nested_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *nested_slot = nested_function->create_stack_slot<CoreIrStackSlot>(
        "matrix", array2_row_type, 32);
    auto *nested_base =
        nested_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_array2_row_type, "matrix.addr", nested_slot);
    auto *zero_row = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero_nested = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero_flat = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *nested_row = nested_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array4_i32_type, "row.addr", nested_base,
        std::vector<CoreIrValue *>{zero_row, row_index});
    auto *nested_addr = nested_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "elt.addr.nested", nested_row,
        std::vector<CoreIrValue *>{zero_nested, column_index});
    auto *flat_addr = nested_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "elt.addr.flat", nested_base,
        std::vector<CoreIrValue *>{zero_flat, row_index, column_index});
    nested_entry->create_instruction<CoreIrStoreInst>(void_type, one, nested_addr);
    nested_entry->create_instruction<CoreIrStoreInst>(void_type, two, flat_addr);
    auto *nested_load = nested_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "t3", flat_addr);
    nested_entry->create_instruction<CoreIrReturnInst>(void_type, nested_load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrDeadStoreEliminationPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    const std::size_t overwrite_offset = text.find("func @overwrite");
    assert(overwrite_offset != std::string::npos);
    const std::size_t barrier_offset = text.find("func @barrier");
    assert(barrier_offset != std::string::npos);
    const std::size_t dynamic_offset = text.find("func @dynamic_exact");
    assert(dynamic_offset != std::string::npos);
    const std::size_t nested_offset = text.find("func @dynamic_nested_exact");
    assert(nested_offset != std::string::npos);
    const std::string overwrite_text =
        text.substr(overwrite_offset, barrier_offset - overwrite_offset);
    const std::string barrier_text =
        text.substr(barrier_offset, dynamic_offset - barrier_offset);
    const std::string dynamic_text =
        text.substr(dynamic_offset, nested_offset - dynamic_offset);
    const std::string nested_text = text.substr(nested_offset);
    assert(overwrite_text.find("store i32 1, %value") == std::string::npos);
    assert(overwrite_text.find("store i32 2, %value") != std::string::npos);
    assert(dynamic_text.find("store i32 1, %elt.addr") == std::string::npos);
    assert(dynamic_text.find("store i32 2, %elt.addr") != std::string::npos);
    assert(nested_text.find("store i32 1, %elt.addr.nested") == std::string::npos);
    assert(nested_text.find("store i32 2, %elt.addr.flat") != std::string::npos);
    assert(barrier_text.find("store i32 1, %value") != std::string::npos);
    return 0;
}
