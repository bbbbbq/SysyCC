#pragma once

#include <unordered_map>
#include <unordered_set>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrFunction;
class CoreIrCfgAnalysisResult;

class CoreIrDominatorTreeAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks_;
    mutable std::unordered_map<const CoreIrBasicBlock *,
                               std::unordered_set<const CoreIrBasicBlock *>>
        dominators_;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
        immediate_dominators_;

    static const std::unordered_set<const CoreIrBasicBlock *> &empty_block_set();

  public:
    CoreIrDominatorTreeAnalysisResult() = default;
    CoreIrDominatorTreeAnalysisResult(
        const CoreIrFunction *function,
        std::unordered_set<const CoreIrBasicBlock *> reachable_blocks,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::unordered_set<const CoreIrBasicBlock *>>
            dominators,
        std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
            immediate_dominators) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    bool is_reachable(const CoreIrBasicBlock *block) const noexcept;

    bool dominates(const CoreIrBasicBlock *dominator,
                   const CoreIrBasicBlock *block) const noexcept;

    CoreIrBasicBlock *
    get_immediate_dominator(const CoreIrBasicBlock *block) const noexcept;

    const std::unordered_set<const CoreIrBasicBlock *> &
    get_dominators(const CoreIrBasicBlock *block) const;
};

class CoreIrDominatorTreeAnalysis {
  public:
    using ResultType = CoreIrDominatorTreeAnalysisResult;

    CoreIrDominatorTreeAnalysisResult Run(
        const CoreIrFunction &function,
        const CoreIrCfgAnalysisResult &cfg_analysis) const;
};

} // namespace sysycc
