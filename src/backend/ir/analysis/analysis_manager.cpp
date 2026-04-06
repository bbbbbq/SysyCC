#include "backend/ir/analysis/analysis_manager.hpp"

#include <utility>

#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

namespace {

void bump_compute_count(
    std::unordered_map<CoreIrFunction *,
                       std::unordered_map<CoreIrAnalysisKind, std::size_t>>
        &compute_counts,
    CoreIrFunction &function, CoreIrAnalysisKind kind) {
    ++compute_counts[&function][kind];
}

} // namespace

CoreIrAnalysisManager::CachedFunctionAnalyses &
CoreIrAnalysisManager::get_or_create_cache_entry(CoreIrFunction &function) {
    return function_cache_[&function];
}

const CoreIrCfgAnalysisResult &
CoreIrAnalysisManager::get_or_compute_cfg(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.cfg_analysis == nullptr) {
        CoreIrCfgAnalysis analysis;
        cache_entry.cfg_analysis =
            std::make_unique<CoreIrCfgAnalysisResult>(analysis.Run(function));
        cache_entry.dominator_tree_analysis.reset();
        cache_entry.dominance_frontier_analysis.reset();
        cache_entry.loop_info_analysis.reset();
        cache_entry.induction_var_analysis.reset();
        cache_entry.scalar_evolution_lite_analysis.reset();
        cache_entry.memory_ssa_analysis.reset();
        bump_compute_count(compute_counts_, function, CoreIrAnalysisKind::Cfg);
    }
    return *cache_entry.cfg_analysis;
}

const CoreIrDominatorTreeAnalysisResult &
CoreIrAnalysisManager::get_or_compute_dominator_tree(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.dominator_tree_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        CoreIrDominatorTreeAnalysis analysis;
        cache_entry.dominator_tree_analysis =
            std::make_unique<CoreIrDominatorTreeAnalysisResult>(
                analysis.Run(function, cfg_analysis));
        cache_entry.dominance_frontier_analysis.reset();
        cache_entry.loop_info_analysis.reset();
        cache_entry.induction_var_analysis.reset();
        cache_entry.scalar_evolution_lite_analysis.reset();
        cache_entry.memory_ssa_analysis.reset();
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::DominatorTree);
    }
    return *cache_entry.dominator_tree_analysis;
}

const CoreIrDominanceFrontierAnalysisResult &
CoreIrAnalysisManager::get_or_compute_dominance_frontier(
    CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.dominance_frontier_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            get_or_compute_dominator_tree(function);
        CoreIrDominanceFrontierAnalysis analysis;
        cache_entry.dominance_frontier_analysis =
            std::make_unique<CoreIrDominanceFrontierAnalysisResult>(
                analysis.Run(function, cfg_analysis, dominator_tree));
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::DominanceFrontier);
    }
    return *cache_entry.dominance_frontier_analysis;
}

const CoreIrPromotableStackSlotAnalysisResult &
CoreIrAnalysisManager::get_or_compute_promotable_stack_slots(
    CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.promotable_stack_slot_analysis == nullptr) {
        CoreIrPromotableStackSlotAnalysis analysis;
        cache_entry.promotable_stack_slot_analysis =
            std::make_unique<CoreIrPromotableStackSlotAnalysisResult>(
                analysis.Run(function));
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::PromotableStackSlot);
    }
    return *cache_entry.promotable_stack_slot_analysis;
}

const CoreIrLoopInfoAnalysisResult &
CoreIrAnalysisManager::get_or_compute_loop_info(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.loop_info_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            get_or_compute_dominator_tree(function);
        CoreIrLoopInfoAnalysis analysis;
        cache_entry.loop_info_analysis =
            std::make_unique<CoreIrLoopInfoAnalysisResult>(
                analysis.Run(function, cfg_analysis, dominator_tree));
        cache_entry.induction_var_analysis.reset();
        cache_entry.scalar_evolution_lite_analysis.reset();
        bump_compute_count(compute_counts_, function, CoreIrAnalysisKind::LoopInfo);
    }
    return *cache_entry.loop_info_analysis;
}

const CoreIrInductionVarAnalysisResult &
CoreIrAnalysisManager::get_or_compute_induction_vars(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.induction_var_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            get_or_compute_dominator_tree(function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            get_or_compute_loop_info(function);
        CoreIrInductionVarAnalysis analysis;
        cache_entry.induction_var_analysis =
            std::make_unique<CoreIrInductionVarAnalysisResult>(
                analysis.Run(function, cfg_analysis, dominator_tree, loop_info));
        cache_entry.scalar_evolution_lite_analysis.reset();
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::InductionVar);
    }
    return *cache_entry.induction_var_analysis;
}

const CoreIrScalarEvolutionLiteAnalysisResult &
CoreIrAnalysisManager::get_or_compute_scalar_evolution_lite(
    CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.scalar_evolution_lite_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            get_or_compute_loop_info(function);
        const CoreIrInductionVarAnalysisResult &induction_vars =
            get_or_compute_induction_vars(function);
        CoreIrScalarEvolutionLiteAnalysis analysis;
        cache_entry.scalar_evolution_lite_analysis =
            std::make_unique<CoreIrScalarEvolutionLiteAnalysisResult>(
                analysis.Run(function, cfg_analysis, loop_info, induction_vars));
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::ScalarEvolutionLite);
    }
    return *cache_entry.scalar_evolution_lite_analysis;
}

const CoreIrFunctionEffectSummaryAnalysisResult &
CoreIrAnalysisManager::get_or_compute_function_effect_summary(
    CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.function_effect_summary == nullptr) {
        CoreIrFunctionEffectSummaryAnalysis analysis;
        cache_entry.function_effect_summary =
            std::make_unique<CoreIrFunctionEffectSummaryAnalysisResult>(
                analysis.Run(function));
        bump_compute_count(compute_counts_, function,
                           CoreIrAnalysisKind::FunctionEffectSummary);
    }
    return *cache_entry.function_effect_summary;
}

const CoreIrAliasAnalysisResult &
CoreIrAnalysisManager::get_or_compute_alias_analysis(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.alias_analysis == nullptr) {
        CoreIrAliasAnalysis analysis;
        cache_entry.alias_analysis =
            std::make_unique<CoreIrAliasAnalysisResult>(analysis.Run(function));
        cache_entry.memory_ssa_analysis.reset();
        bump_compute_count(compute_counts_, function, CoreIrAnalysisKind::AliasAnalysis);
    }
    return *cache_entry.alias_analysis;
}

const CoreIrMemorySSAAnalysisResult &
CoreIrAnalysisManager::get_or_compute_memory_ssa(CoreIrFunction &function) {
    CachedFunctionAnalyses &cache_entry = get_or_create_cache_entry(function);
    if (cache_entry.memory_ssa_analysis == nullptr) {
        const CoreIrCfgAnalysisResult &cfg_analysis = get_or_compute_cfg(function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            get_or_compute_dominator_tree(function);
        const CoreIrDominanceFrontierAnalysisResult &dominance_frontier =
            get_or_compute_dominance_frontier(function);
        const CoreIrAliasAnalysisResult &alias_analysis =
            get_or_compute_alias_analysis(function);
        const CoreIrFunctionEffectSummaryAnalysisResult &effect_summary =
            get_or_compute_function_effect_summary(function);
        CoreIrMemorySSAAnalysis analysis;
        cache_entry.memory_ssa_analysis =
            std::make_unique<CoreIrMemorySSAAnalysisResult>(
                analysis.Run(function, cfg_analysis, dominator_tree,
                             dominance_frontier, effect_summary, alias_analysis));
        bump_compute_count(compute_counts_, function, CoreIrAnalysisKind::MemorySSA);
    }
    return *cache_entry.memory_ssa_analysis;
}

void CoreIrAnalysisManager::invalidate_all() noexcept {
    function_cache_.clear();
    compute_counts_.clear();
}

void CoreIrAnalysisManager::invalidate(CoreIrFunction &function) noexcept {
    function_cache_.erase(&function);
}

void CoreIrAnalysisManager::invalidate(CoreIrAnalysisKind kind) noexcept {
    for (auto it = function_cache_.begin(); it != function_cache_.end();) {
        switch (kind) {
        case CoreIrAnalysisKind::Cfg:
            it->second.cfg_analysis.reset();
            it->second.dominator_tree_analysis.reset();
            it->second.dominance_frontier_analysis.reset();
            it->second.promotable_stack_slot_analysis.reset();
            it->second.loop_info_analysis.reset();
            it->second.induction_var_analysis.reset();
            it->second.scalar_evolution_lite_analysis.reset();
            it->second.memory_ssa_analysis.reset();
            break;
        case CoreIrAnalysisKind::DominatorTree:
            it->second.dominator_tree_analysis.reset();
            it->second.dominance_frontier_analysis.reset();
            it->second.loop_info_analysis.reset();
            it->second.induction_var_analysis.reset();
            it->second.scalar_evolution_lite_analysis.reset();
            it->second.memory_ssa_analysis.reset();
            break;
        case CoreIrAnalysisKind::DominanceFrontier:
            it->second.dominance_frontier_analysis.reset();
            break;
        case CoreIrAnalysisKind::PromotableStackSlot:
            it->second.promotable_stack_slot_analysis.reset();
            break;
        case CoreIrAnalysisKind::LoopInfo:
            it->second.loop_info_analysis.reset();
            it->second.induction_var_analysis.reset();
            it->second.scalar_evolution_lite_analysis.reset();
            break;
        case CoreIrAnalysisKind::InductionVar:
            it->second.induction_var_analysis.reset();
            it->second.scalar_evolution_lite_analysis.reset();
            break;
        case CoreIrAnalysisKind::ScalarEvolutionLite:
            it->second.scalar_evolution_lite_analysis.reset();
            break;
        case CoreIrAnalysisKind::AliasAnalysis:
            it->second.alias_analysis.reset();
            it->second.memory_ssa_analysis.reset();
            break;
        case CoreIrAnalysisKind::MemorySSA:
            it->second.memory_ssa_analysis.reset();
            break;
        case CoreIrAnalysisKind::FunctionEffectSummary:
            it->second.function_effect_summary.reset();
            it->second.alias_analysis.reset();
            it->second.memory_ssa_analysis.reset();
            break;
        }

        if (it->second.cfg_analysis == nullptr &&
            it->second.dominator_tree_analysis == nullptr &&
            it->second.dominance_frontier_analysis == nullptr &&
            it->second.promotable_stack_slot_analysis == nullptr &&
            it->second.loop_info_analysis == nullptr &&
            it->second.induction_var_analysis == nullptr &&
            it->second.scalar_evolution_lite_analysis == nullptr &&
            it->second.alias_analysis == nullptr &&
            it->second.memory_ssa_analysis == nullptr &&
            it->second.function_effect_summary == nullptr) {
            it = function_cache_.erase(it);
            continue;
        }
        ++it;
    }
}

void CoreIrAnalysisManager::invalidate(CoreIrFunction &function,
                                       CoreIrAnalysisKind kind) noexcept {
    auto it = function_cache_.find(&function);
    if (it == function_cache_.end()) {
        return;
    }

    switch (kind) {
    case CoreIrAnalysisKind::Cfg:
        it->second.cfg_analysis.reset();
        it->second.dominator_tree_analysis.reset();
        it->second.dominance_frontier_analysis.reset();
        it->second.promotable_stack_slot_analysis.reset();
        it->second.loop_info_analysis.reset();
        it->second.induction_var_analysis.reset();
        it->second.scalar_evolution_lite_analysis.reset();
        it->second.memory_ssa_analysis.reset();
        break;
    case CoreIrAnalysisKind::DominatorTree:
        it->second.dominator_tree_analysis.reset();
        it->second.dominance_frontier_analysis.reset();
        it->second.loop_info_analysis.reset();
        it->second.induction_var_analysis.reset();
        it->second.scalar_evolution_lite_analysis.reset();
        it->second.memory_ssa_analysis.reset();
        break;
    case CoreIrAnalysisKind::DominanceFrontier:
        it->second.dominance_frontier_analysis.reset();
        break;
    case CoreIrAnalysisKind::PromotableStackSlot:
        it->second.promotable_stack_slot_analysis.reset();
        break;
    case CoreIrAnalysisKind::LoopInfo:
        it->second.loop_info_analysis.reset();
        it->second.induction_var_analysis.reset();
        it->second.scalar_evolution_lite_analysis.reset();
        break;
    case CoreIrAnalysisKind::InductionVar:
        it->second.induction_var_analysis.reset();
        it->second.scalar_evolution_lite_analysis.reset();
        break;
    case CoreIrAnalysisKind::ScalarEvolutionLite:
        it->second.scalar_evolution_lite_analysis.reset();
        break;
    case CoreIrAnalysisKind::AliasAnalysis:
        it->second.alias_analysis.reset();
        it->second.memory_ssa_analysis.reset();
        break;
    case CoreIrAnalysisKind::MemorySSA:
        it->second.memory_ssa_analysis.reset();
        break;
    case CoreIrAnalysisKind::FunctionEffectSummary:
        it->second.function_effect_summary.reset();
        it->second.alias_analysis.reset();
        it->second.memory_ssa_analysis.reset();
        break;
    }

    if (it->second.cfg_analysis == nullptr &&
        it->second.dominator_tree_analysis == nullptr &&
        it->second.dominance_frontier_analysis == nullptr &&
        it->second.promotable_stack_slot_analysis == nullptr &&
        it->second.loop_info_analysis == nullptr &&
        it->second.induction_var_analysis == nullptr &&
        it->second.scalar_evolution_lite_analysis == nullptr &&
        it->second.alias_analysis == nullptr &&
        it->second.memory_ssa_analysis == nullptr &&
        it->second.function_effect_summary == nullptr) {
        function_cache_.erase(it);
    }
}

std::size_t CoreIrAnalysisManager::get_compute_count(
    CoreIrFunction &function, CoreIrAnalysisKind kind) const noexcept {
    auto function_it = compute_counts_.find(&function);
    if (function_it == compute_counts_.end()) {
        return 0;
    }
    auto count_it = function_it->second.find(kind);
    return count_it == function_it->second.end() ? 0
                                                                : count_it->second;
}

} // namespace sysycc
