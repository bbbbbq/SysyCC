#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/alias_analysis.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrCfgAnalysisResult;
class CoreIrDominatorTreeAnalysisResult;
class CoreIrDominanceFrontierAnalysisResult;
class CoreIrFunction;
class CoreIrFunctionEffectSummaryAnalysisResult;
class CoreIrInstruction;

enum class CoreIrMemoryAccessKind : unsigned char {
    LiveOnEntry,
    Def,
    Use,
    Phi,
};

class CoreIrMemoryAccess {
  private:
    CoreIrMemoryAccessKind kind_;
    std::size_t id_;

  public:
    CoreIrMemoryAccess(CoreIrMemoryAccessKind kind, std::size_t id) noexcept
        : kind_(kind), id_(id) {}
    virtual ~CoreIrMemoryAccess() = default;

    CoreIrMemoryAccessKind get_kind() const noexcept { return kind_; }
    std::size_t get_id() const noexcept { return id_; }
};

class CoreIrMemoryLiveOnEntryAccess final : public CoreIrMemoryAccess {
  public:
    explicit CoreIrMemoryLiveOnEntryAccess(std::size_t id) noexcept
        : CoreIrMemoryAccess(CoreIrMemoryAccessKind::LiveOnEntry, id) {}
};

class CoreIrMemoryUseAccess final : public CoreIrMemoryAccess {
  private:
    const CoreIrInstruction *instruction_ = nullptr;
    CoreIrMemoryAccess *defining_access_ = nullptr;

  public:
    CoreIrMemoryUseAccess(std::size_t id, const CoreIrInstruction *instruction,
                          CoreIrMemoryAccess *defining_access) noexcept
        : CoreIrMemoryAccess(CoreIrMemoryAccessKind::Use, id),
          instruction_(instruction), defining_access_(defining_access) {}

    const CoreIrInstruction *get_instruction() const noexcept {
        return instruction_;
    }

    CoreIrMemoryAccess *get_defining_access() const noexcept {
        return defining_access_;
    }
};

class CoreIrMemoryDefAccess final : public CoreIrMemoryAccess {
  private:
    const CoreIrInstruction *instruction_ = nullptr;
    CoreIrMemoryAccess *defining_access_ = nullptr;

  public:
    CoreIrMemoryDefAccess(std::size_t id, const CoreIrInstruction *instruction,
                          CoreIrMemoryAccess *defining_access) noexcept
        : CoreIrMemoryAccess(CoreIrMemoryAccessKind::Def, id),
          instruction_(instruction), defining_access_(defining_access) {}

    const CoreIrInstruction *get_instruction() const noexcept {
        return instruction_;
    }

    CoreIrMemoryAccess *get_defining_access() const noexcept {
        return defining_access_;
    }
};

class CoreIrMemoryPhiAccess final : public CoreIrMemoryAccess {
  private:
    const CoreIrBasicBlock *block_ = nullptr;
    std::vector<std::pair<const CoreIrBasicBlock *, CoreIrMemoryAccess *>>
        incomings_;

  public:
    CoreIrMemoryPhiAccess(std::size_t id,
                          const CoreIrBasicBlock *block) noexcept
        : CoreIrMemoryAccess(CoreIrMemoryAccessKind::Phi, id), block_(block) {}

    const CoreIrBasicBlock *get_block() const noexcept { return block_; }

    const std::vector<
        std::pair<const CoreIrBasicBlock *, CoreIrMemoryAccess *>> &
    get_incomings() const noexcept {
        return incomings_;
    }

    void add_incoming(const CoreIrBasicBlock *block,
                      CoreIrMemoryAccess *access) {
        incomings_.emplace_back(block, access);
    }
};

class CoreIrMemorySSAAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    CoreIrAliasAnalysisResult alias_analysis_;
    std::unique_ptr<CoreIrMemoryLiveOnEntryAccess> live_on_entry_;
    std::vector<std::unique_ptr<CoreIrMemoryAccess>> accesses_;
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryAccess *>
        instruction_accesses_;
    std::unordered_map<const CoreIrBasicBlock *,
                       std::vector<CoreIrMemoryPhiAccess *>>
        block_phis_;
    mutable std::unordered_map<std::string, CoreIrMemoryAccess *>
        clobbering_access_cache_;

  public:
    CoreIrMemorySSAAnalysisResult() = default;
    CoreIrMemorySSAAnalysisResult(
        const CoreIrFunction *function,
        CoreIrAliasAnalysisResult alias_analysis,
        std::unique_ptr<CoreIrMemoryLiveOnEntryAccess> live_on_entry,
        std::vector<std::unique_ptr<CoreIrMemoryAccess>> accesses,
        std::unordered_map<const CoreIrInstruction *, CoreIrMemoryAccess *>
            instruction_accesses,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::vector<CoreIrMemoryPhiAccess *>>
            block_phis) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const CoreIrAliasAnalysisResult &get_alias_analysis() const noexcept {
        return alias_analysis_;
    }

    const CoreIrMemoryLiveOnEntryAccess *get_live_on_entry() const noexcept {
        return live_on_entry_.get();
    }

    CoreIrMemoryAccess *get_access_for_instruction(
        const CoreIrInstruction *instruction) const noexcept;

    const std::vector<CoreIrMemoryPhiAccess *> &
    get_phis_for_block(const CoreIrBasicBlock *block) const;

    CoreIrMemoryAccess *
    get_clobbering_access(const CoreIrInstruction *instruction) const;
};

class CoreIrMemorySSAAnalysis {
  public:
    using ResultType = CoreIrMemorySSAAnalysisResult;

    CoreIrMemorySSAAnalysisResult
    Run(const CoreIrFunction &function,
        const CoreIrCfgAnalysisResult &cfg_analysis,
        const CoreIrDominatorTreeAnalysisResult &dominator_tree,
        const CoreIrDominanceFrontierAnalysisResult &dominance_frontier,
        const CoreIrFunctionEffectSummaryAnalysisResult &effect_summary,
        const CoreIrAliasAnalysisResult &alias_analysis) const;
};

} // namespace sysycc
