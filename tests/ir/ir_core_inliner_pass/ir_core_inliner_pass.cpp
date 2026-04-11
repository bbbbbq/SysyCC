#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/inliner/core_ir_inliner_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *main_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_inliner_pass");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);

    auto *helper =
        module->create_function<CoreIrFunction>("helper", callee_type, true);
    helper->set_is_always_inline(true);
    auto *param = helper->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *helper_entry = helper->create_basic_block<CoreIrBasicBlock>("entry");
    auto *sum = helper_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", param, one);
    helper_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    auto *helper_with_slot = module->create_function<CoreIrFunction>(
        "helper_with_slot", callee_type, true);
    helper_with_slot->set_is_always_inline(true);
    auto *slot_param =
        helper_with_slot->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *slot =
        helper_with_slot->create_stack_slot<CoreIrStackSlot>("tmp", i32_type, 4);
    auto *slot_entry =
        helper_with_slot->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot_cmp = slot_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "slot.cmp", slot_param, zero);
    auto *slot_selected = slot_entry->create_instruction<CoreIrSelectInst>(
        i32_type, "slot.sel", slot_cmp, one, slot_param);
    slot_entry->create_instruction<CoreIrStoreInst>(void_type, slot_selected,
                                                    slot);
    auto *slot_loaded = slot_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "slot.loaded", slot);
    auto *slot_sum = slot_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "slot.sum", slot_loaded, one);
    slot_entry->create_instruction<CoreIrReturnInst>(void_type, slot_sum);

    auto *too_complex =
        module->create_function<CoreIrFunction>("too_complex", callee_type, false);
    auto *too_complex_param =
        too_complex->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *too_complex_entry =
        too_complex->create_basic_block<CoreIrBasicBlock>("entry");
    auto *too_complex_exit =
        too_complex->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = too_complex_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cond", too_complex_param, zero);
    too_complex_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, cond, too_complex_exit, too_complex_exit);
    too_complex_exit->create_instruction<CoreIrReturnInst>(void_type, too_complex_param);

    auto *main_fn = module->create_function<CoreIrFunction>("main", main_type, false);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *simple_call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "helper", callee_type, std::vector<CoreIrValue *>{five});
    auto *slot_call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "slot.call", "helper_with_slot", callee_type,
        std::vector<CoreIrValue *>{simple_call});
    auto *complex_call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "complex", "too_complex", callee_type,
        std::vector<CoreIrValue *>{slot_call});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, complex_call);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInlinerPass pass;
    PassResult result = pass.Run(compiler_context);
    assert(result.ok);

    bool saw_helper_call = false;
    bool saw_slot_call = false;
    bool saw_complex_call = false;
    bool saw_add = false;
    bool saw_compare = false;
    bool saw_cond_jump = false;
    bool saw_select = false;
    for (const auto &block_ptr : main_fn->get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            auto *call = dynamic_cast<CoreIrCallInst *>(instruction);
            if (call != nullptr && call->get_is_direct_call()) {
                if (call->get_callee_name() == "helper") {
                    saw_helper_call = true;
                }
                if (call->get_callee_name() == "helper_with_slot") {
                    saw_slot_call = true;
                }
                if (call->get_callee_name() == "too_complex") {
                    saw_complex_call = true;
                }
            }
            if (dynamic_cast<CoreIrBinaryInst *>(instruction) != nullptr) {
                saw_add = true;
            }
            if (dynamic_cast<CoreIrCompareInst *>(instruction) != nullptr) {
                saw_compare = true;
            }
            if (dynamic_cast<CoreIrCondJumpInst *>(instruction) != nullptr) {
                saw_cond_jump = true;
            }
            if (dynamic_cast<CoreIrSelectInst *>(instruction) != nullptr) {
                saw_select = true;
            }
        }
    }
    assert(!saw_helper_call);
    assert(!saw_slot_call);
    assert(!saw_complex_call);
    assert(saw_add);
    assert(saw_compare);
    assert(saw_cond_jump);
    assert(saw_select);
    assert(!main_fn->get_stack_slots().empty());
    return 0;
}
