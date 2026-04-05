#pragma once

#include "backend/ir/effect/core_ir_effect.hpp"

namespace sysycc {

class CoreIrFunction;

class CoreIrFunctionEffectSummaryAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    CoreIrEffectInfo effect_info_{};

  public:
    CoreIrFunctionEffectSummaryAnalysisResult() = default;
    CoreIrFunctionEffectSummaryAnalysisResult(const CoreIrFunction *function,
                                             CoreIrEffectInfo effect_info) noexcept
        : function_(function), effect_info_(effect_info) {}

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const CoreIrEffectInfo &get_effect_info() const noexcept {
        return effect_info_;
    }
};

class CoreIrFunctionEffectSummaryAnalysis {
  public:
    using ResultType = CoreIrFunctionEffectSummaryAnalysisResult;

    CoreIrFunctionEffectSummaryAnalysisResult Run(
        const CoreIrFunction &function) const noexcept;
};

} // namespace sysycc
