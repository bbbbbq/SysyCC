#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrCfgAnalysisResult;
class CoreIrDominatorTreeAnalysisResult;
class CoreIrFunction;

class CoreIrLoopInfo {
  private:
    CoreIrBasicBlock *header_ = nullptr;
    CoreIrBasicBlock *preheader_ = nullptr;
    std::unordered_set<CoreIrBasicBlock *> latches_;
    std::unordered_set<CoreIrBasicBlock *> blocks_;
    std::unordered_set<CoreIrBasicBlock *> exit_blocks_;
    CoreIrLoopInfo *parent_ = nullptr;
    std::vector<CoreIrLoopInfo *> subloops_;
    std::size_t depth_ = 1;

  public:
    explicit CoreIrLoopInfo(CoreIrBasicBlock *header) : header_(header) {}

    CoreIrBasicBlock *get_header() const noexcept { return header_; }
    CoreIrBasicBlock *get_preheader() const noexcept { return preheader_; }
    void set_preheader(CoreIrBasicBlock *preheader) noexcept { preheader_ = preheader; }

    const std::unordered_set<CoreIrBasicBlock *> &get_latches() const noexcept {
        return latches_;
    }
    std::unordered_set<CoreIrBasicBlock *> &mutable_latches() noexcept {
        return latches_;
    }

    const std::unordered_set<CoreIrBasicBlock *> &get_blocks() const noexcept {
        return blocks_;
    }
    std::unordered_set<CoreIrBasicBlock *> &mutable_blocks() noexcept { return blocks_; }

    const std::unordered_set<CoreIrBasicBlock *> &get_exit_blocks() const noexcept {
        return exit_blocks_;
    }
    std::unordered_set<CoreIrBasicBlock *> &mutable_exit_blocks() noexcept {
        return exit_blocks_;
    }

    CoreIrLoopInfo *get_parent_loop() const noexcept { return parent_; }
    void set_parent_loop(CoreIrLoopInfo *parent) noexcept { parent_ = parent; }

    const std::vector<CoreIrLoopInfo *> &get_subloops() const noexcept {
        return subloops_;
    }
    std::vector<CoreIrLoopInfo *> &mutable_subloops() noexcept { return subloops_; }

    std::size_t get_depth() const noexcept { return depth_; }
    void set_depth(std::size_t depth) noexcept { depth_ = depth; }
};

class CoreIrLoopInfoAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::vector<std::unique_ptr<CoreIrLoopInfo>> loops_;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrLoopInfo *> header_to_loop_;

  public:
    CoreIrLoopInfoAnalysisResult() = default;
    explicit CoreIrLoopInfoAnalysisResult(const CoreIrFunction *function) noexcept
        : function_(function) {}

    const CoreIrFunction *get_function() const noexcept { return function_; }

    CoreIrLoopInfo *create_loop(CoreIrBasicBlock *header);

    const std::vector<std::unique_ptr<CoreIrLoopInfo>> &get_loops() const noexcept {
        return loops_;
    }

    CoreIrLoopInfo *get_loop_for_header(const CoreIrBasicBlock *header) const noexcept;
};

class CoreIrLoopInfoAnalysis {
  public:
    using ResultType = CoreIrLoopInfoAnalysisResult;

    CoreIrLoopInfoAnalysisResult Run(
        const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
        const CoreIrDominatorTreeAnalysisResult &dominator_tree) const;
};

} // namespace sysycc
