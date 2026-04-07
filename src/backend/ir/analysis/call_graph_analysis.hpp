#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sysycc {

class CoreIrCallInst;
class CoreIrFunction;
class CoreIrModule;

class CoreIrCallGraphAnalysisResult {
  private:
    const CoreIrModule *module_ = nullptr;
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
        callees_;
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
        callers_;
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrCallInst *>>
        callsites_by_callee_;
    std::unordered_set<const CoreIrFunction *> recursive_functions_;

  public:
    CoreIrCallGraphAnalysisResult() = default;
    CoreIrCallGraphAnalysisResult(
        const CoreIrModule *module,
        std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
            callees,
        std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
            callers,
        std::unordered_map<const CoreIrFunction *, std::vector<CoreIrCallInst *>>
            callsites_by_callee,
        std::unordered_set<const CoreIrFunction *> recursive_functions) noexcept;

    const CoreIrModule *get_module() const noexcept { return module_; }

    const std::vector<CoreIrFunction *> &
    get_callees(const CoreIrFunction *function) const noexcept;

    const std::vector<CoreIrFunction *> &
    get_callers(const CoreIrFunction *function) const noexcept;

    const std::vector<CoreIrCallInst *> &
    get_callsites_for_callee(const CoreIrFunction *function) const noexcept;

    bool is_recursive(const CoreIrFunction *function) const noexcept;
};

class CoreIrCallGraphAnalysis {
  public:
    using ResultType = CoreIrCallGraphAnalysisResult;

    CoreIrCallGraphAnalysisResult Run(CoreIrModule &module) const;
};

} // namespace sysycc
