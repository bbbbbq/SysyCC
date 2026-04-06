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
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_stack_slot_forward_pass");

    auto *forwarded_function = module->create_function<CoreIrFunction>(
        "forwarded", function_type, false);
    auto *forwarded_entry =
        forwarded_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *forwarded_slot =
        forwarded_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    forwarded_entry->create_instruction<CoreIrStoreInst>(void_type, seven,
                                                         forwarded_slot);
    auto *forwarded_load =
        forwarded_entry->create_instruction<CoreIrLoadInst>(i32_type, "t0",
                                                            forwarded_slot);
    auto *forwarded_add = forwarded_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "t1", forwarded_load, one);
    forwarded_entry->create_instruction<CoreIrReturnInst>(void_type, forwarded_add);

    auto *barrier_function = module->create_function<CoreIrFunction>(
        "barrier", function_type, false);
    auto *barrier_entry =
        barrier_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *barrier_slot =
        barrier_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    barrier_entry->create_instruction<CoreIrStoreInst>(void_type, seven, barrier_slot);
    barrier_entry->create_instruction<CoreIrCallInst>(i32_type, "call.tmp",
                                                      "side_effect", callee_type,
                                                      std::vector<CoreIrValue *>{});
    auto *barrier_load =
        barrier_entry->create_instruction<CoreIrLoadInst>(i32_type, "t2",
                                                          barrier_slot);
    barrier_entry->create_instruction<CoreIrReturnInst>(void_type, barrier_load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrStackSlotForwardPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%t0 = load") == std::string::npos);
    assert(text.find("%t1 = add i32 7, 1") != std::string::npos);
    assert(text.find("%t2 = load i32, %value") != std::string::npos);
    return 0;
}
