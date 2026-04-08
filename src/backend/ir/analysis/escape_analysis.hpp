#pragma once

#include <unordered_map>

#include "backend/ir/effect/core_ir_memory_location.hpp"

namespace sysycc {

class CoreIrFunction;
class CoreIrFunctionAttrsAnalysisResult;
class CoreIrGlobal;
class CoreIrParameter;
class CoreIrStackSlot;
class CoreIrValue;

enum class CoreIrEscapeKind : unsigned char {
    NoEscape,
    Returned,
    CapturedByStore,
    CapturedByCall,
    Unknown,
};

CoreIrEscapeKind merge_core_ir_escape_kind(CoreIrEscapeKind lhs,
                                           CoreIrEscapeKind rhs) noexcept;

class CoreIrEscapeAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind>
        value_escape_kinds_;
    std::unordered_map<const CoreIrStackSlot *, CoreIrEscapeKind>
        stack_slot_escape_kinds_;
    std::unordered_map<const CoreIrParameter *, CoreIrEscapeKind>
        parameter_escape_kinds_;
    std::unordered_map<const CoreIrGlobal *, CoreIrEscapeKind>
        global_escape_kinds_;

  public:
    CoreIrEscapeAnalysisResult() = default;
    CoreIrEscapeAnalysisResult(
        const CoreIrFunction *function,
        std::unordered_map<const CoreIrValue *, CoreIrEscapeKind>
            value_escape_kinds,
        std::unordered_map<const CoreIrStackSlot *, CoreIrEscapeKind>
            stack_slot_escape_kinds,
        std::unordered_map<const CoreIrParameter *, CoreIrEscapeKind>
            parameter_escape_kinds,
        std::unordered_map<const CoreIrGlobal *, CoreIrEscapeKind>
            global_escape_kinds) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    CoreIrEscapeKind
    get_escape_kind_for_value(const CoreIrValue *value) const noexcept;

    CoreIrEscapeKind get_escape_kind_for_location(
        const CoreIrMemoryLocation &location) const noexcept;

    bool is_non_escaping_location(
        const CoreIrMemoryLocation &location) const noexcept;
};

class CoreIrEscapeAnalysis {
  public:
    using ResultType = CoreIrEscapeAnalysisResult;

    CoreIrEscapeAnalysisResult Run(const CoreIrFunction &function,
                                   const CoreIrFunctionAttrsAnalysisResult
                                       *function_attrs = nullptr) const;
};

} // namespace sysycc
