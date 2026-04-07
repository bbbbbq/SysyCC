#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/sroa/core_ir_sroa_pass.hpp"
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
    auto *array3_i32 = context->create_type<CoreIrArrayType>(i32_type, 3);
    auto *ptr_array3_type = context->create_type<CoreIrPointerType>(array3_i32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *sink_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_sroa");
    module->create_function<CoreIrFunction>("sink", sink_type, false);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("state", struct_type, 4);
    auto *escaped_slot =
        function->create_stack_slot<CoreIrStackSlot>("escaped", array3_i32, 4);
    auto *index_slot =
        function->create_stack_slot<CoreIrStackSlot>("idx", i64_type, 8);
    auto *zero32 = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one32 = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two32 = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *one64 = context->create_constant<CoreIrConstantInt>(i64_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *eleven = context->create_constant<CoreIrConstantInt>(i32_type, 11);
    auto *twentytwo = context->create_constant<CoreIrConstantInt>(i32_type, 22);
    auto *thirtythree =
        context->create_constant<CoreIrConstantInt>(i32_type, 33);
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
    auto *escaped_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array3_type, "escaped.addr", escaped_slot);
    auto *escaped_elem0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "escaped.elem0", escaped_addr,
        std::vector<CoreIrValue *>{zero32, zero32});
    auto *escaped_elem1 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "escaped.elem1", escaped_addr,
        std::vector<CoreIrValue *>{zero32, one32});
    auto *escaped_elem2 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "escaped.elem2", escaped_addr,
        std::vector<CoreIrValue *>{zero32, two32});
    entry->create_instruction<CoreIrStoreInst>(void_type, eleven, escaped_elem0);
    entry->create_instruction<CoreIrStoreInst>(void_type, twentytwo, escaped_elem1);
    entry->create_instruction<CoreIrStoreInst>(void_type, thirtythree, escaped_elem2);
    entry->create_instruction<CoreIrCallInst>(
        i32_type, "escape", "sink", sink_type,
        std::vector<CoreIrValue *>{escaped_elem0});
    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", field0_load, dynamic_load);
    entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSroaPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("stackslot %state.0 : i32") != std::string::npos);
    assert(text.find("store i32 7, %state.0") != std::string::npos);
    assert(text.find("%field0.load = load i32, %state.0") !=
           std::string::npos);
    assert(text.find("%field1.addr = gep") != std::string::npos);
    assert(text.find("stackslot %state : { i32, [2 x i32] }") !=
           std::string::npos);
    assert(text.find("stackslot %escaped : [3 x i32]") != std::string::npos);
    assert(text.find("stackslot %escaped.0") == std::string::npos);
    assert(text.find("stackslot %escaped.1") == std::string::npos);
    assert(text.find("stackslot %escaped.2") == std::string::npos);
    assert(text.find("call i32 @sink(i32* %escaped.elem0)") !=
           std::string::npos);
    return 0;
}
