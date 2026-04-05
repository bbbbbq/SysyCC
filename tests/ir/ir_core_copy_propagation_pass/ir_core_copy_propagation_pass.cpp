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

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrCopyPropagationPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%t1 = load") == std::string::npos);
    assert(text.find("%t2 = add i32 %t0, 1") != std::string::npos);
    assert(text.find("%addr1 = addr_of_stackslot") == std::string::npos);
    assert(text.find("load i32, %addr0") != std::string::npos);
    assert(first_load->get_uses().size() >= 1);
    assert(address0->get_uses().size() >= 1);
    return 0;
}
