#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/argument_promotion/core_ir_argument_promotion_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *main_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_argument_promotion_pass");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *forty_one = context->create_constant<CoreIrConstantInt>(i32_type, 41);

    auto *helper =
        module->create_function<CoreIrFunction>("helper", callee_type, true);
    auto *param = helper->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *helper_entry = helper->create_basic_block<CoreIrBasicBlock>("entry");
    auto *load =
        helper_entry->create_instruction<CoreIrLoadInst>(i32_type, "load", param);
    auto *sum = helper_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", load, one);
    helper_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    auto *main_fn = module->create_function<CoreIrFunction>("main", main_type, false);
    auto *slot = main_fn->create_stack_slot<CoreIrStackSlot>("x", i32_type, 4);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    main_entry->create_instruction<CoreIrStoreInst>(void_type, forty_one, slot);
    auto *addr = main_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "x.addr", slot);
    auto *call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "helper", callee_type, std::vector<CoreIrValue *>{addr});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, call);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrArgumentPromotionPass pass;
    PassResult result = pass.Run(compiler_context);
    assert(result.ok);

    CoreIrFunction *promoted = nullptr;
    for (const auto &function_ptr : module->get_functions()) {
        if (function_ptr != nullptr &&
            function_ptr->get_name().find("helper.argprom.") == 0) {
            promoted = function_ptr.get();
            break;
        }
    }
    assert(promoted != nullptr);
    assert(promoted->get_parameters().size() == 1);
    assert(promoted->get_parameters().front()->get_type() == i32_type);

    bool saw_promoted_call = false;
    bool saw_inserted_load = false;
    for (const auto &instruction_ptr : main_entry->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (auto *load_inst = dynamic_cast<CoreIrLoadInst *>(instruction);
            load_inst != nullptr && load_inst->get_name() == "call.argprom.load") {
            saw_inserted_load = true;
        }
        if (auto *call_inst = dynamic_cast<CoreIrCallInst *>(instruction);
            call_inst != nullptr && call_inst->get_callee_name() == promoted->get_name()) {
            saw_promoted_call = true;
            assert(call_inst->get_argument_count() == 1);
            assert(call_inst->get_argument(0) != addr);
        }
    }
    assert(saw_promoted_call);
    assert(saw_inserted_load);
    return 0;
}
