#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "backend/ir/effect/core_ir_effect.hpp"

namespace sysycc {

class CoreIrConstant;
class CoreIrFunction;
class CoreIrModule;
class CoreIrCallGraphAnalysisResult;

struct CoreIrFunctionAttrsSummary {
    CoreIrMemoryBehavior memory_behavior = CoreIrMemoryBehavior::ReadWrite;
    bool is_norecurse = false;
    std::vector<bool> parameter_nocapture;
    std::vector<bool> parameter_readonly;
    const CoreIrConstant *constant_return = nullptr;
    std::optional<std::size_t> returned_parameter_index;
};

class CoreIrFunctionAttrsAnalysisResult {
  private:
    const CoreIrModule *module_ = nullptr;
    std::unordered_map<const CoreIrFunction *, CoreIrFunctionAttrsSummary>
        summaries_;

  public:
    CoreIrFunctionAttrsAnalysisResult() = default;
    CoreIrFunctionAttrsAnalysisResult(
        const CoreIrModule *module,
        std::unordered_map<const CoreIrFunction *, CoreIrFunctionAttrsSummary>
            summaries) noexcept;

    const CoreIrModule *get_module() const noexcept { return module_; }

    const CoreIrFunctionAttrsSummary *
    get_summary(const CoreIrFunction *function) const noexcept;
};

class CoreIrFunctionAttrsAnalysis {
  public:
    using ResultType = CoreIrFunctionAttrsAnalysisResult;

    CoreIrFunctionAttrsAnalysisResult Run(
        CoreIrModule &module,
        const CoreIrCallGraphAnalysisResult &call_graph) const;
};

} // namespace sysycc
