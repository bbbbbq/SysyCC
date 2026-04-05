#pragma once

#include <unordered_map>
#include <unordered_set>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrCfgAnalysisResult;
class CoreIrDominatorTreeAnalysisResult;
class CoreIrFunction;

class CoreIrDominanceFrontierAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *,
                       std::unordered_set<CoreIrBasicBlock *>>
        frontiers_;

    static const std::unordered_set<CoreIrBasicBlock *> &empty_frontier();

  public:
    CoreIrDominanceFrontierAnalysisResult() = default;
    CoreIrDominanceFrontierAnalysisResult(
        const CoreIrFunction *function,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::unordered_set<CoreIrBasicBlock *>>
            frontiers) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const std::unordered_set<CoreIrBasicBlock *> &
    get_frontier(const CoreIrBasicBlock *block) const;

    bool has_frontier_edge(const CoreIrBasicBlock *from,
                           const CoreIrBasicBlock *to) const noexcept;
};

class CoreIrDominanceFrontierAnalysis {
  public:
    using ResultType = CoreIrDominanceFrontierAnalysisResult;

    CoreIrDominanceFrontierAnalysisResult Run(
        const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
        const CoreIrDominatorTreeAnalysisResult &dominator_tree) const;
};

} // namespace sysycc
