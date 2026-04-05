#include "backend/ir/analysis/dominance_frontier_analysis.hpp"

#include <utility>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

CoreIrDominanceFrontierAnalysisResult::CoreIrDominanceFrontierAnalysisResult(
    const CoreIrFunction *function,
    std::unordered_map<const CoreIrBasicBlock *,
                       std::unordered_set<CoreIrBasicBlock *>>
        frontiers) noexcept
    : function_(function), frontiers_(std::move(frontiers)) {}

const std::unordered_set<CoreIrBasicBlock *> &
CoreIrDominanceFrontierAnalysisResult::empty_frontier() {
    static const std::unordered_set<CoreIrBasicBlock *> empty;
    return empty;
}

const std::unordered_set<CoreIrBasicBlock *> &
CoreIrDominanceFrontierAnalysisResult::get_frontier(
    const CoreIrBasicBlock *block) const {
    auto it = frontiers_.find(block);
    return it == frontiers_.end() ? empty_frontier() : it->second;
}

bool CoreIrDominanceFrontierAnalysisResult::has_frontier_edge(
    const CoreIrBasicBlock *from, const CoreIrBasicBlock *to) const noexcept {
    auto it = frontiers_.find(from);
    return it != frontiers_.end() && it->second.find(const_cast<CoreIrBasicBlock *>(to)) !=
                                        it->second.end();
}

CoreIrDominanceFrontierAnalysisResult CoreIrDominanceFrontierAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) const {
    std::unordered_map<const CoreIrBasicBlock *,
                       std::unordered_set<CoreIrBasicBlock *>>
        frontiers;
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr && cfg_analysis.is_reachable(block.get())) {
            frontiers.emplace(block.get(),
                              std::unordered_set<CoreIrBasicBlock *>{});
        }
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get()) ||
            cfg_analysis.get_predecessor_count(block.get()) < 2) {
            continue;
        }

        CoreIrBasicBlock *idom = dominator_tree.get_immediate_dominator(block.get());
        for (CoreIrBasicBlock *predecessor : cfg_analysis.get_predecessors(block.get())) {
            CoreIrBasicBlock *runner = predecessor;
            while (runner != nullptr && runner != idom) {
                frontiers[runner].insert(block.get());
                runner = dominator_tree.get_immediate_dominator(runner);
            }
        }
    }

    return CoreIrDominanceFrontierAnalysisResult(&function, std::move(frontiers));
}

} // namespace sysycc
