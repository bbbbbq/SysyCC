#include "backend/ir/analysis/function_effect_summary_analysis.hpp"

#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

CoreIrFunctionEffectSummaryAnalysisResult
CoreIrFunctionEffectSummaryAnalysis::Run(const CoreIrFunction &function) const noexcept {
    return CoreIrFunctionEffectSummaryAnalysisResult(
        &function, summarize_core_ir_function_effect(function));
}

} // namespace sysycc
