#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
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
        "ir_core_copy_propagation_pass");

    auto *load_function =
        module->create_function<CoreIrFunction>("load_copy", function_type, false);
    auto *load_entry =
        load_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *load_slot =
        load_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    load_entry->create_instruction<CoreIrStoreInst>(void_type, seven, load_slot);
    auto *first_load =
        load_entry->create_instruction<CoreIrLoadInst>(i32_type, "t0", load_slot);
    auto *second_load =
        load_entry->create_instruction<CoreIrLoadInst>(i32_type, "t1", load_slot);
    auto *sum = load_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t2", second_load, one);
    load_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    auto *address_function = module->create_function<CoreIrFunction>(
        "address_copy", function_type, false);
    auto *address_entry =
        address_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *address_slot =
        address_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *address0 = address_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr0", address_slot);
    auto *address1 = address_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr1", address_slot);
    auto *load_from_address = address_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "t3", address1);
    address_entry->create_instruction<CoreIrReturnInst>(void_type, load_from_address);

    auto *array_type = context->create_type<CoreIrArrayType>(i32_type, 4);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *array_function = module->create_function<CoreIrFunction>(
        "cross_store_copy", function_type, false);
    auto *array_entry =
        array_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *scalar_slot =
        array_function->create_stack_slot<CoreIrStackSlot>("scalar", i32_type, 4);
    auto *array_slot =
        array_function->create_stack_slot<CoreIrStackSlot>("array", array_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    array_entry->create_instruction<CoreIrStoreInst>(void_type, seven, scalar_slot);
    auto *scalar_load0 =
        array_entry->create_instruction<CoreIrLoadInst>(i32_type, "t4", scalar_slot);
    auto *array_addr = array_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array_type, "array.addr", array_slot);
    auto *array_elem = array_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "array.elem", array_addr,
        std::vector<CoreIrValue *>{zero, one});
    array_entry->create_instruction<CoreIrStoreInst>(void_type, one, array_elem);
    auto *scalar_load1 =
        array_entry->create_instruction<CoreIrLoadInst>(i32_type, "t5", scalar_slot);
    auto *sum2 = array_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t6", scalar_load1, one);
    array_entry->create_instruction<CoreIrReturnInst>(void_type, sum2);

    auto *exact_path_function = module->create_function<CoreIrFunction>(
        "exact_path_forward", function_type, false);
    auto *exact_entry =
        exact_path_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *state_slot = exact_path_function->create_stack_slot<CoreIrStackSlot>(
        "state", array_type, 4);
    auto *state_addr =
        exact_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_array_type, "state.addr", state_slot);
    auto *state_elem0 = exact_entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "state.elem0", state_addr,
        std::vector<CoreIrValue *>{zero, zero});
    exact_entry->create_instruction<CoreIrStoreInst>(void_type, seven, state_elem0);
    auto *state_addr_again =
        exact_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_array_type, "state.addr.dup", state_slot);
    auto *state_elem0_again =
        exact_entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "state.elem0.dup", state_addr_again,
            std::vector<CoreIrValue *>{zero, zero});
    auto *exact_load = exact_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "t16", state_elem0_again);
    auto *exact_sum = exact_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t17", exact_load, one);
    exact_entry->create_instruction<CoreIrReturnInst>(void_type, exact_sum);

    auto *cross_block_function = module->create_function<CoreIrFunction>(
        "immutable_slot_forward", function_type, false);
    auto *cross_entry =
        cross_block_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *cross_body =
        cross_block_function->create_basic_block<CoreIrBasicBlock>("body");
    auto *cross_exit =
        cross_block_function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *immutable_slot =
        cross_block_function->create_stack_slot<CoreIrStackSlot>("immutable", i32_type, 4);
    cross_entry->create_instruction<CoreIrStoreInst>(void_type, seven, immutable_slot);
    cross_entry->create_instruction<CoreIrJumpInst>(void_type, cross_body);
    auto *cross_load0 =
        cross_body->create_instruction<CoreIrLoadInst>(i32_type, "t7", immutable_slot);
    auto *cross_sum = cross_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t8", cross_load0, one);
    cross_body->create_instruction<CoreIrJumpInst>(void_type, cross_exit);
    auto *cross_load1 =
        cross_exit->create_instruction<CoreIrLoadInst>(i32_type, "t9", immutable_slot);
    auto *cross_sum2 = cross_exit->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t10", cross_sum, cross_load1);
    cross_exit->create_instruction<CoreIrReturnInst>(void_type, cross_sum2);

    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    module->create_function<CoreIrFunction>("sink", callee_type, false);
    auto *escaped_exact_function = module->create_function<CoreIrFunction>(
        "escaped_exact_path", function_type, false);
    auto *escaped_exact_entry =
        escaped_exact_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *escaped_exact_slot =
        escaped_exact_function->create_stack_slot<CoreIrStackSlot>(
            "state", array_type, 4);
    auto *escaped_exact_addr =
        escaped_exact_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_array_type, "state.addr", escaped_exact_slot);
    auto *escaped_exact_elem =
        escaped_exact_entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "state.elem", escaped_exact_addr,
            std::vector<CoreIrValue *>{zero, zero});
    escaped_exact_entry->create_instruction<CoreIrStoreInst>(void_type, seven,
                                                             escaped_exact_elem);
    escaped_exact_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call.exact", "sink", callee_type,
        std::vector<CoreIrValue *>{escaped_exact_elem});
    auto *escaped_exact_load =
        escaped_exact_entry->create_instruction<CoreIrLoadInst>(
            i32_type, "t18", escaped_exact_elem);
    escaped_exact_entry->create_instruction<CoreIrReturnInst>(void_type,
                                                              escaped_exact_load);

    auto *escape_function = module->create_function<CoreIrFunction>(
        "escaped_slot", function_type, false);
    auto *escape_entry =
        escape_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *escaped_slot =
        escape_function->create_stack_slot<CoreIrStackSlot>("escaped", i32_type, 4);
    auto *escaped_addr = escape_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "escaped.addr", escaped_slot);
    escape_entry->create_instruction<CoreIrStoreInst>(void_type, zero, escaped_slot);
    auto *escape_call = escape_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "sink", callee_type, std::vector<CoreIrValue *>{escaped_addr});
    auto *escaped_load =
        escape_entry->create_instruction<CoreIrLoadInst>(i32_type, "t11", escaped_slot);
    escape_entry->create_instruction<CoreIrReturnInst>(void_type, escaped_load);

    auto *mutable_function = module->create_function<CoreIrFunction>(
        "mutable_slot_chain", function_type, false);
    auto *mutable_entry =
        mutable_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *mutable_slot =
        mutable_function->create_stack_slot<CoreIrStackSlot>("mutable", i32_type, 4);
    mutable_entry->create_instruction<CoreIrStoreInst>(void_type, zero, mutable_slot);
    auto *mutable_load0 =
        mutable_entry->create_instruction<CoreIrLoadInst>(i32_type, "t12", mutable_slot);
    auto *mutable_next = mutable_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t13", mutable_load0, one);
    mutable_entry->create_instruction<CoreIrStoreInst>(void_type, mutable_next, mutable_slot);
    auto *mutable_load1 =
        mutable_entry->create_instruction<CoreIrLoadInst>(i32_type, "t14", mutable_slot);
    auto *mutable_sum = mutable_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t15", mutable_load1, one);
    mutable_entry->create_instruction<CoreIrReturnInst>(void_type, mutable_sum);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrCopyPropagationPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%t1 = load") == std::string::npos);
    assert(text.find("%t2 = add i32 %t0, 1") != std::string::npos ||
           text.find("%t2 = add i32 7, 1") != std::string::npos);
    assert(text.find("%addr1 = addr_of_stackslot") == std::string::npos);
    assert(text.find("load i32, %addr0") != std::string::npos);
    assert(text.find("%t5 = load i32, stackslot %scalar") == std::string::npos);
    assert(text.find("%t6 = add i32 %t4, 1") != std::string::npos ||
           text.find("%t6 = add i32 7, 1") != std::string::npos);
    assert(text.find("%t16 = load i32, %state.elem0.dup") == std::string::npos);
    assert(text.find("%t17 = add i32 7, 1") != std::string::npos);
    assert(text.find("%t18 = load i32") != std::string::npos);
    assert(escaped_exact_load != nullptr);
    assert(text.find("%t7 = load i32, stackslot %immutable") == std::string::npos);
    assert(text.find("%t9 = load i32, stackslot %immutable") == std::string::npos);
    assert(text.find("%t8 = add i32 7, 1") != std::string::npos);
    assert(text.find("%t10 = add i32 %t8, 7") != std::string::npos);
    assert(text.find("ret i32 0") == std::string::npos);
    assert(text.find("%t14 = load i32, stackslot %mutable") == std::string::npos);
    assert(text.find("%t15 = add i32 %t13, 1") != std::string::npos);
    assert(escape_call != nullptr);
    return 0;
}
