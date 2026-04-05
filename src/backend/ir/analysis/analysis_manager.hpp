#pragma once

#include <memory>
#include <unordered_map>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/core_ir_analysis_kind.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"

namespace sysycc {

class CoreIrFunction;

class CoreIrAnalysisManager {
  private:
    struct CachedFunctionAnalyses {
        std::unique_ptr<CoreIrCfgAnalysisResult> cfg_analysis;
        std::unique_ptr<CoreIrDominatorTreeAnalysisResult>
            dominator_tree_analysis;
    };

    std::unordered_map<CoreIrFunction *, CachedFunctionAnalyses> function_cache_;

    CachedFunctionAnalyses &get_or_create_cache_entry(CoreIrFunction &function);
    const CoreIrCfgAnalysisResult &get_or_compute_cfg(CoreIrFunction &function);
    const CoreIrDominatorTreeAnalysisResult &
    get_or_compute_dominator_tree(CoreIrFunction &function);

  public:
    template <typename AnalysisT>
    const typename AnalysisT::ResultType &
    get_or_compute(CoreIrFunction &function);

    void invalidate_all() noexcept;
    void invalidate(CoreIrFunction &function) noexcept;
    void invalidate(CoreIrAnalysisKind kind) noexcept;
    void invalidate(CoreIrFunction &function, CoreIrAnalysisKind kind) noexcept;
};

template <>
inline const CoreIrCfgAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrCfgAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_cfg(function);
}

template <>
inline const CoreIrDominatorTreeAnalysisResult &
CoreIrAnalysisManager::get_or_compute<CoreIrDominatorTreeAnalysis>(
    CoreIrFunction &function) {
    return get_or_compute_dominator_tree(function);
}

} // namespace sysycc
