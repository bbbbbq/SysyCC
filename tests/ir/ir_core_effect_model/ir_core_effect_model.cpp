#include <cassert>
#include <memory>

#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_effect_model");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *binary = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *call = entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "callee", callee_type,
        std::vector<CoreIrValue *>{address});
    entry->create_instruction<CoreIrStoreInst>(void_type, call, slot);
    auto *exit_block = function->create_basic_block<CoreIrBasicBlock>("exit");
    entry->create_instruction<CoreIrJumpInst>(void_type, exit_block);
    exit_block->create_instruction<CoreIrReturnInst>(void_type, load);

    const CoreIrEffectInfo binary_effect = get_core_ir_instruction_effect(*binary);
    assert(binary_effect.is_pure_value);
    assert(binary_effect.memory_behavior == CoreIrMemoryBehavior::None);

    const CoreIrEffectInfo load_effect = get_core_ir_instruction_effect(*load);
    assert(memory_behavior_reads(load_effect.memory_behavior));
    assert(!memory_behavior_writes(load_effect.memory_behavior));

    const CoreIrEffectInfo call_effect = get_core_ir_instruction_effect(*call);
    assert(memory_behavior_reads(call_effect.memory_behavior));
    assert(memory_behavior_writes(call_effect.memory_behavior));
    assert(call_effect.may_capture_pointer_operands);

    CoreIrFunctionEffectSummaryAnalysis summary_analysis;
    const CoreIrFunctionEffectSummaryAnalysisResult summary =
        summary_analysis.Run(*function);
    assert(memory_behavior_reads(summary.get_effect_info().memory_behavior));
    assert(memory_behavior_writes(summary.get_effect_info().memory_behavior));
    assert(summary.get_effect_info().may_capture_pointer_operands);
    assert(!summary.get_effect_info().is_pure_value);
    assert(!summary.get_effect_info().has_control_effect);
    return 0;
}
