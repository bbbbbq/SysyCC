#include "backend/ir/effect/core_ir_effect.hpp"

#include <unordered_map>
#include <unordered_set>

#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool value_has_pointer_type(const CoreIrValue *value) noexcept {
    return value != nullptr && value->get_type() != nullptr &&
           value->get_type()->get_kind() == CoreIrTypeKind::Pointer;
}

CoreIrEffectInfo make_pure_value_effect() noexcept {
    return CoreIrEffectInfo{CoreIrMemoryBehavior::None, false, false, true};
}

CoreIrEffectInfo make_conservative_call_graph_cycle_effect() noexcept {
    return CoreIrEffectInfo{CoreIrMemoryBehavior::ReadWrite, false, true,
                            false};
}

} // namespace

CoreIrMemoryBehavior merge_memory_behavior(CoreIrMemoryBehavior lhs,
                                           CoreIrMemoryBehavior rhs) noexcept {
    if (lhs == CoreIrMemoryBehavior::ReadWrite ||
        rhs == CoreIrMemoryBehavior::ReadWrite) {
        return CoreIrMemoryBehavior::ReadWrite;
    }
    if (lhs == CoreIrMemoryBehavior::None) {
        return rhs;
    }
    if (rhs == CoreIrMemoryBehavior::None) {
        return lhs;
    }
    if (lhs == rhs) {
        return lhs;
    }
    return CoreIrMemoryBehavior::ReadWrite;
}

bool memory_behavior_reads(CoreIrMemoryBehavior behavior) noexcept {
    return behavior == CoreIrMemoryBehavior::Read ||
           behavior == CoreIrMemoryBehavior::ReadWrite;
}

bool memory_behavior_writes(CoreIrMemoryBehavior behavior) noexcept {
    return behavior == CoreIrMemoryBehavior::Write ||
           behavior == CoreIrMemoryBehavior::ReadWrite;
}

CoreIrEffectInfo
get_core_ir_instruction_effect(const CoreIrInstruction &instruction) noexcept {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::ExtractElement:
    case CoreIrOpcode::InsertElement:
    case CoreIrOpcode::ShuffleVector:
    case CoreIrOpcode::VectorReduceAdd:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
        return make_pure_value_effect();
    case CoreIrOpcode::DynamicAlloca:
        return CoreIrEffectInfo{CoreIrMemoryBehavior::Write, false, false,
                                false};
    case CoreIrOpcode::Load:
        return CoreIrEffectInfo{CoreIrMemoryBehavior::Read, false, false,
                                false};
    case CoreIrOpcode::Store:
        return CoreIrEffectInfo{CoreIrMemoryBehavior::Write, false, false,
                                false};
    case CoreIrOpcode::Call: {
        bool captures_pointer_operand = false;
        for (CoreIrValue *operand : instruction.get_operands()) {
            if (value_has_pointer_type(operand)) {
                captures_pointer_operand = true;
                break;
            }
        }
        return CoreIrEffectInfo{CoreIrMemoryBehavior::ReadWrite, false,
                                captures_pointer_operand, false};
    }
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return CoreIrEffectInfo{CoreIrMemoryBehavior::None, true, false, false};
    }

    return {};
}

CoreIrEffectInfo
summarize_core_ir_function_effect_impl(
    const CoreIrFunction &function,
    std::unordered_set<const CoreIrFunction *> &visiting,
    std::unordered_map<const CoreIrFunction *, CoreIrEffectInfo> &cache)
    noexcept {
    if (const auto it = cache.find(&function); it != cache.end()) {
        return it->second;
    }
    if (!visiting.insert(&function).second) {
        return make_conservative_call_graph_cycle_effect();
    }

    CoreIrEffectInfo summary{};
    summary.is_pure_value = true;
    const CoreIrModule *module = function.get_parent();
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction : block->get_instructions()) {
            if (instruction == nullptr) {
                continue;
            }

            CoreIrEffectInfo effect =
                get_core_ir_instruction_effect(*instruction);
            if (auto *call = dynamic_cast<CoreIrCallInst *>(instruction.get());
                call != nullptr && call->get_is_direct_call() &&
                module != nullptr) {
                if (CoreIrFunction *callee =
                        module->find_function(call->get_callee_name());
                    callee != nullptr && !callee->get_basic_blocks().empty()) {
                    effect = summarize_core_ir_function_effect_impl(
                        *callee, visiting, cache);
                }
            }

            summary.memory_behavior = merge_memory_behavior(
                summary.memory_behavior, effect.memory_behavior);
            summary.may_capture_pointer_operands =
                summary.may_capture_pointer_operands ||
                effect.may_capture_pointer_operands;
            summary.is_pure_value =
                summary.is_pure_value && effect.is_pure_value;
        }
    }

    summary.has_control_effect = false;
    if (memory_behavior_reads(summary.memory_behavior) ||
        memory_behavior_writes(summary.memory_behavior) ||
        summary.may_capture_pointer_operands) {
        summary.is_pure_value = false;
    }
    visiting.erase(&function);
    cache.emplace(&function, summary);
    return summary;
}

CoreIrEffectInfo
summarize_core_ir_function_effect(const CoreIrFunction &function) noexcept {
    std::unordered_set<const CoreIrFunction *> visiting;
    std::unordered_map<const CoreIrFunction *, CoreIrEffectInfo> cache;
    return summarize_core_ir_function_effect_impl(function, visiting, cache);
}

} // namespace sysycc
