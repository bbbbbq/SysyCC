#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrCfgAnalysisResult;
class CoreIrDominatorTreeAnalysisResult;
class CoreIrFunction;
class CoreIrLoopInfo;
class CoreIrLoopInfoAnalysisResult;
class CoreIrValue;

struct CoreIrCanonicalInductionVarInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrBasicBlock *header = nullptr;
    CoreIrBasicBlock *preheader = nullptr;
    CoreIrBasicBlock *latch = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrInstruction *latch_update = nullptr;
    std::int64_t step = 0;
    CoreIrCompareInst *exit_compare = nullptr;
    CoreIrCondJumpInst *exit_branch = nullptr;
    CoreIrValue *exit_bound = nullptr;
    CoreIrComparePredicate normalized_predicate =
        CoreIrComparePredicate::SignedLess;
    bool inside_successor_is_true = true;

    bool is_valid() const noexcept {
        return phi != nullptr && header != nullptr && preheader != nullptr &&
               latch != nullptr && initial_value != nullptr &&
               latch_update != nullptr && step != 0;
    }
};

class CoreIrInductionVarAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrCanonicalInductionVarInfo>
        canonical_induction_vars_;

  public:
    CoreIrInductionVarAnalysisResult() = default;
    explicit CoreIrInductionVarAnalysisResult(const CoreIrFunction *function) noexcept
        : function_(function) {}

    const CoreIrFunction *get_function() const noexcept { return function_; }

    void set_canonical_induction_var(const CoreIrBasicBlock *header,
                                     CoreIrCanonicalInductionVarInfo info);

    const CoreIrCanonicalInductionVarInfo *
    get_canonical_induction_var(const CoreIrBasicBlock *header) const noexcept;

    const CoreIrCanonicalInductionVarInfo *
    get_canonical_induction_var(const CoreIrLoopInfo &loop) const noexcept;
};

class CoreIrInductionVarAnalysis {
  public:
    using ResultType = CoreIrInductionVarAnalysisResult;

    CoreIrInductionVarAnalysisResult Run(
        const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
        const CoreIrDominatorTreeAnalysisResult &dominator_tree,
        const CoreIrLoopInfoAnalysisResult &loop_info) const;
};

} // namespace sysycc

