#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrFunction;

class CoreIrCfgAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    const CoreIrBasicBlock *entry_block_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        predecessors_;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        successors_;
    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks_;

    static const std::vector<CoreIrBasicBlock *> &empty_block_list();

  public:
    CoreIrCfgAnalysisResult() = default;
    CoreIrCfgAnalysisResult(
        const CoreIrFunction *function, const CoreIrBasicBlock *entry_block,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::vector<CoreIrBasicBlock *>> predecessors,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::vector<CoreIrBasicBlock *>> successors,
        std::unordered_set<const CoreIrBasicBlock *> reachable_blocks) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const CoreIrBasicBlock *get_entry_block() const noexcept { return entry_block_; }

    const std::vector<CoreIrBasicBlock *> &
    get_predecessors(const CoreIrBasicBlock *block) const;

    const std::vector<CoreIrBasicBlock *> &
    get_successors(const CoreIrBasicBlock *block) const;

    std::size_t get_predecessor_count(const CoreIrBasicBlock *block) const;

    bool has_block(const CoreIrBasicBlock *block) const noexcept;

    bool is_reachable(const CoreIrBasicBlock *block) const noexcept;
};

class CoreIrCfgAnalysis {
  public:
    using ResultType = CoreIrCfgAnalysisResult;

    CoreIrCfgAnalysisResult Run(const CoreIrFunction &function) const;
};

} // namespace sysycc
