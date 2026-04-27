#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool erase_instruction(CoreIrBasicBlock &block,
                       CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

bool forward_stack_slot_values(CoreIrBasicBlock &block) {
    bool changed = false;
    auto &instructions = block.get_instructions();
    std::unordered_map<CoreIrStackSlot *, CoreIrValue *> available_values;

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            load != nullptr) {
            CoreIrStackSlot *slot = load->get_stack_slot();
            if (slot == nullptr) {
                ++index;
                continue;
            }
            const auto available_it = available_values.find(slot);
            if (available_it != available_values.end() &&
                available_it->second != nullptr) {
                load->replace_all_uses_with(available_it->second);
                erase_instruction(block, load);
                changed = true;
                continue;
            }
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
            store != nullptr) {
            if (store->get_stack_slot() != nullptr &&
                store->get_value() != nullptr) {
                available_values[store->get_stack_slot()] = store->get_value();
            } else {
                available_values.clear();
            }
            ++index;
            continue;
        }

        const CoreIrEffectInfo effect =
            get_core_ir_instruction_effect(*instruction);
        if (memory_behavior_writes(effect.memory_behavior)) {
            available_values.clear();
        }

        ++index;
    }

    return changed;
}

} // namespace

PassKind CoreIrStackSlotForwardPass::Kind() const {
    return PassKind::CoreIrStackSlotForward;
}

const char *CoreIrStackSlotForwardPass::Name() const {
    return "CoreIrStackSlotForwardPass";
}

PassResult CoreIrStackSlotForwardPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }
    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed =
                forward_stack_slot_values(*block) || function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve_loop_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
