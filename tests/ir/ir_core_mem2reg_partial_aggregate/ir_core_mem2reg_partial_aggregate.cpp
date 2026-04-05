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
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i64_type = context->create_type<CoreIrIntegerType>(64);
    auto *array2_i32 = context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, array2_i32});
    auto *ptr_struct_type = context->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array2_i32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_mem2reg_partial_aggregate");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("state", struct_type, 4);
    auto *index_slot =
        function->create_stack_slot<CoreIrStackSlot>("idx", i64_type, 8);
    auto *zero32 = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one32 = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *one64 = context->create_constant<CoreIrConstantInt>(i64_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *slot_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_struct_type, "state.addr", slot);
    auto *field0_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0.addr", slot_addr,
        std::vector<CoreIrValue *>{zero32, zero32});
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, field0_addr);
    auto *field0_load = entry->create_instruction<CoreIrLoadInst>(
        i32_type, "field0.load", field0_addr);

    entry->create_instruction<CoreIrStoreInst>(void_type, one64, index_slot);
    auto *field1_base = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array_type, "field1.base", slot_addr,
        std::vector<CoreIrValue *>{zero32, one32});
    auto *dynamic_index =
        entry->create_instruction<CoreIrLoadInst>(i64_type, "idx.load", index_slot);
    auto *dynamic_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field1.addr", field1_base,
        std::vector<CoreIrValue *>{zero32, dynamic_index});
    auto *dynamic_load = entry->create_instruction<CoreIrLoadInst>(
        i32_type, "field1.load", dynamic_addr);
    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", field0_load, dynamic_load);
    entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrMem2RegPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%field0.load = load i32") == std::string::npos);
    assert(text.find("store i32 7, %field0.addr") == std::string::npos);
    assert(text.find("%field1.addr = gep") != std::string::npos);
    assert(text.find("stackslot %state") != std::string::npos);
    return 0;
}
