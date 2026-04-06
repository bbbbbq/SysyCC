#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "backend/ir/analysis/induction_var_analysis.hpp"

namespace sysycc {

class CoreIrCfgAnalysisResult;
class CoreIrFunction;
class CoreIrLoopInfo;
class CoreIrLoopInfoAnalysisResult;
class CoreIrValue;

enum class CoreIrScevExprKind : unsigned char {
    Unknown,
    Constant,
    AddRec,
};

struct CoreIrScevExpr {
    CoreIrScevExprKind kind = CoreIrScevExprKind::Unknown;
    std::int64_t constant = 0;
    CoreIrValue *start_value = nullptr;
    std::int64_t step = 0;
    const CoreIrBasicBlock *loop_header = nullptr;

    static CoreIrScevExpr unknown() noexcept { return {}; }

    static CoreIrScevExpr constant_expr(std::int64_t value) noexcept {
        CoreIrScevExpr expr;
        expr.kind = CoreIrScevExprKind::Constant;
        expr.constant = value;
        return expr;
    }

    static CoreIrScevExpr add_rec(CoreIrValue *start_value, std::int64_t step,
                                  const CoreIrBasicBlock *loop_header) noexcept {
        CoreIrScevExpr expr;
        expr.kind = CoreIrScevExprKind::AddRec;
        expr.start_value = start_value;
        expr.step = step;
        expr.loop_header = loop_header;
        return expr;
    }
};

struct CoreIrBackedgeTakenCountInfo {
    bool has_symbolic_count = false;
    std::optional<std::uint64_t> constant_trip_count;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *bound_value = nullptr;
    std::int64_t step = 0;
    CoreIrComparePredicate predicate = CoreIrComparePredicate::SignedLess;
};

class CoreIrScalarEvolutionLiteAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrCanonicalInductionVarInfo>
        canonical_induction_vars_;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrBackedgeTakenCountInfo>
        backedge_taken_counts_;

    bool compute_loop_invariant(
        CoreIrValue *value, const CoreIrLoopInfo &loop,
        std::unordered_map<const CoreIrValue *, bool> &cache,
        std::unordered_map<const CoreIrValue *, bool> &visiting) const;

    CoreIrScevExpr compute_expr(
        CoreIrValue *value, const CoreIrLoopInfo &loop,
        std::unordered_map<const CoreIrValue *, CoreIrScevExpr> &cache,
        std::unordered_map<const CoreIrValue *, bool> &visiting) const;

  public:
    CoreIrScalarEvolutionLiteAnalysisResult() = default;
    explicit CoreIrScalarEvolutionLiteAnalysisResult(
        const CoreIrFunction *function) noexcept
        : function_(function) {}

    const CoreIrFunction *get_function() const noexcept { return function_; }

    void set_canonical_induction_var(const CoreIrBasicBlock *header,
                                     const CoreIrCanonicalInductionVarInfo &info);
    void set_backedge_taken_count(const CoreIrBasicBlock *header,
                                  const CoreIrBackedgeTakenCountInfo &info);

    bool is_loop_invariant(CoreIrValue *value, const CoreIrLoopInfo &loop) const;
    CoreIrScevExpr get_expr(CoreIrValue *value, const CoreIrLoopInfo &loop) const;

    const CoreIrCanonicalInductionVarInfo *
    get_canonical_induction_var(const CoreIrLoopInfo &loop) const noexcept;

    const CoreIrBackedgeTakenCountInfo *
    get_backedge_taken_count(const CoreIrLoopInfo &loop) const noexcept;

    std::optional<std::uint64_t>
    get_constant_trip_count(const CoreIrLoopInfo &loop) const noexcept;
};

class CoreIrScalarEvolutionLiteAnalysis {
  public:
    using ResultType = CoreIrScalarEvolutionLiteAnalysisResult;

    CoreIrScalarEvolutionLiteAnalysisResult Run(
        const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
        const CoreIrLoopInfoAnalysisResult &loop_info,
        const CoreIrInductionVarAnalysisResult &induction_vars) const;
};

} // namespace sysycc
