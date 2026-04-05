#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
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

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
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

std::size_t find_instruction_index(const CoreIrBasicBlock &block,
                                   const CoreIrInstruction *instruction) {
    const auto &instructions = block.get_instructions();
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        if (instructions[index].get() == instruction) {
            return index;
        }
    }
    return instructions.size();
}

struct PendingStoreInfo {
    CoreIrStoreInst *store = nullptr;
    CoreIrMemoryLocation location = CoreIrMemoryLocation::make_unknown();
};

void clear_pending_stores(std::vector<PendingStoreInfo> &pending_stores) {
    pending_stores.clear();
}

bool eliminate_dead_stores(CoreIrBasicBlock &block,
                           const CoreIrAliasAnalysisResult &alias_analysis,
                           const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    bool changed = false;
    std::vector<PendingStoreInfo> pending_stores;
    auto &instructions = block.get_instructions();

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
            const CoreIrMemoryLocation *store_location =
                alias_analysis.get_location_for_instruction(store);
            if (store_location == nullptr || store_location->root_kind ==
                                               CoreIrMemoryLocationRootKind::Unknown) {
                clear_pending_stores(pending_stores);
                ++index;
                continue;
            }

            for (std::size_t pending_index = 0; pending_index < pending_stores.size();) {
                const CoreIrAliasKind alias_kind = alias_core_ir_memory_locations(
                    pending_stores[pending_index].location, *store_location);
                if (alias_kind == CoreIrAliasKind::NoAlias) {
                    ++pending_index;
                    continue;
                }
                if (alias_kind == CoreIrAliasKind::MustAlias &&
                    pending_stores[pending_index].store != nullptr) {
                    const std::size_t previous_index =
                        find_instruction_index(block, pending_stores[pending_index].store);
                    if (previous_index < instructions.size() &&
                        erase_instruction(block, pending_stores[pending_index].store)) {
                        if (previous_index < index) {
                            --index;
                        }
                        changed = true;
                    }
                    pending_stores.erase(pending_stores.begin() +
                                         static_cast<std::ptrdiff_t>(pending_index));
                    continue;
                }
                clear_pending_stores(pending_stores);
                break;
            }
            pending_stores.push_back(PendingStoreInfo{store, *store_location});
            ++index;
            continue;
        }

        const CoreIrEffectInfo effect = get_core_ir_instruction_effect(*instruction);
        if (!memory_behavior_reads(effect.memory_behavior) &&
            !memory_behavior_writes(effect.memory_behavior)) {
            ++index;
            continue;
        }

        if (memory_ssa.get_access_for_instruction(instruction) == nullptr) {
            clear_pending_stores(pending_stores);
            ++index;
            continue;
        }

        const CoreIrMemoryLocation *location =
            alias_analysis.get_location_for_instruction(instruction);
        if (location == nullptr ||
            location->root_kind == CoreIrMemoryLocationRootKind::Unknown) {
            clear_pending_stores(pending_stores);
            ++index;
            continue;
        }

        for (std::size_t pending_index = 0; pending_index < pending_stores.size();) {
            const CoreIrAliasKind alias_kind = alias_core_ir_memory_locations(
                pending_stores[pending_index].location, *location);
            if (alias_kind == CoreIrAliasKind::NoAlias) {
                ++pending_index;
                continue;
            }
            pending_stores.erase(pending_stores.begin() +
                                 static_cast<std::ptrdiff_t>(pending_index));
        }
        ++index;
    }

    return changed;
}

} // namespace

PassKind CoreIrDeadStoreEliminationPass::Kind() const {
    return PassKind::CoreIrDeadStoreElimination;
}

const char *CoreIrDeadStoreEliminationPass::Name() const {
    return "CoreIrDeadStoreEliminationPass";
}

PassResult CoreIrDeadStoreEliminationPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrAliasAnalysisResult &alias_analysis =
            analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
        const CoreIrMemorySSAAnalysisResult &memory_ssa =
            analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed =
                eliminate_dead_stores(*block, alias_analysis, memory_ssa) ||
                function_changed;
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
