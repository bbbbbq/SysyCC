#pragma once

#include <memory>
#include <unordered_map>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/core_ir_analysis_kind.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"

namespace sysycc {

class CoreIrFunction;

class CoreIrAnalysisManager {
  private:
    struct CachedFunctionAnalyses {
        std::unique_ptr<CoreIrCfgAnalysisResult> cfg_analysis;
        std::unique_ptr<CoreIrDominatorTreeAnalysisResult>
            dominator_tree_analysis;
        std::unique_ptr<CoreIrDominanceFrontierAnalysisResult>
            dominance_frontier_analysis;
        std::unique_ptr<CoreIrPromotableStackSlotAnalysisResult>
            promotable_stack_slot_analysis;
        std::unique_ptr<CoreIrLoopInfoAnalysisResult> loop_info_analysis;
        std::unique_ptr<CoreIrAliasAnalysisResult> alias_analysis;
        std::unique_ptr<CoreIrMemorySSAAnalysisResult> memory_ssa_analysis;
        std::unique_ptr<CoreIrFunctionEffectSummaryAnalysisResult>
            function_effect_summary;
    };

    std::unordered_map<CoreIrFunction *, CachedFunctionAnalyses> function_cache_;
    std::unordered_map<CoreIrFunction *,
                       std::unordered_map<CoreIrAnalysisKind, std::size_t>>
        compute_counts_;

    CachedFunctionAnalyses &get_or_create_cache_entry(CoreIrFunction &function);
    const CoreIrCfgAnalysisResult &get_or_compute_cfg(CoreIrFunction &function);
    const CoreIrDominatorTreeAnalysisResult &
    get_or_compute_dominator_tree(CoreIrFunction &function);
    const CoreIrDominanceFrontierAnalysisResult &
    get_or_compute_dominance_frontier(CoreIrFunction &function);
    const CoreIrPromotableStackSlotAnalysisResult &
    get_or_compute_promotable_stack_slots(CoreIrFunction &function);
    const CoreIrLoopInfoAnalysisResult &get_or_compute_loop_info(CoreIrFunction &function);
    const CoreIrAliasAnalysisResult &get_or_compute_alias_analysis(CoreIrFunction &function);
    const CoreIrMemorySSAAnalysisResult &get_or_compute_memory_ssa(CoreIrFunction &function);
    const CoreIrFunctionEffectSummaryAnalysisResult &
    get_or_compute_function_effect_summary(CoreIrFunction &function);

  public:
    template <typename AnalysisT>
    const typename AnalysisT::ResultType &
    get_or_compute(CoreIrFunction &function);

    void invalidate_all() noexcept;
    void invalidate(CoreIrFunction &function) noexcept;
    void invalidate(CoreIrAnalysisKind kind) noexcept;
    void invalidate(CoreIrFunction &function, CoreIrAnalysisKind kind) noexcept;
    std::size_t get_compute_count(CoreIrFunction &function,
                                  CoreIrAnalysisKind kind) const noexcept;
};

template <>
inline const CoreIrCfgAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrCfgAnalysis>(CoreIrFunction &function) {
    return get_or_compute_cfg(function);
}

template <>
inline const CoreIrDominatorTreeAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrDominatorTreeAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_dominator_tree(function);
}

template <>
inline const CoreIrDominanceFrontierAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrDominanceFrontierAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_dominance_frontier(function);
}

template <>
inline const CoreIrPromotableStackSlotAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrPromotableStackSlotAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_promotable_stack_slots(function);
}

template <>
inline const CoreIrLoopInfoAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrLoopInfoAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_loop_info(function);
}

template <>
inline const CoreIrAliasAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrAliasAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_alias_analysis(function);
}

template <>
inline const CoreIrMemorySSAAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrMemorySSAAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_memory_ssa(function);
}

template <>
inline const CoreIrFunctionEffectSummaryAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrFunctionEffectSummaryAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_function_effect_summary(function);
}

} // namespace sysycc
