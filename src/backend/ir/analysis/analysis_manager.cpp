#include "backend/ir/analysis/analysis_manager.hpp"

#include <utility>

#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

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
    }
    return *cache_entry.dominator_tree_analysis;
}

void CoreIrAnalysisManager::invalidate_all() noexcept { function_cache_.clear(); }

void CoreIrAnalysisManager::invalidate(CoreIrFunction &function) noexcept {
    function_cache_.erase(&function);
}

void CoreIrAnalysisManager::invalidate(CoreIrAnalysisKind kind) noexcept {
    for (auto it = function_cache_.begin(); it != function_cache_.end();) {
        switch (kind) {
        case CoreIrAnalysisKind::Cfg:
            it->second.cfg_analysis.reset();
            it->second.dominator_tree_analysis.reset();
            break;
        case CoreIrAnalysisKind::DominatorTree:
            it->second.dominator_tree_analysis.reset();
            break;
        }

        if (it->second.cfg_analysis == nullptr &&
            it->second.dominator_tree_analysis == nullptr) {
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
        break;
    case CoreIrAnalysisKind::DominatorTree:
        it->second.dominator_tree_analysis.reset();
        break;
    }

    if (it->second.cfg_analysis == nullptr &&
        it->second.dominator_tree_analysis == nullptr) {
        function_cache_.erase(it);
    }
}

} // namespace sysycc
