#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "backend/ir/effect/core_ir_memory_location.hpp"

namespace sysycc {

class CoreIrFunction;
class CoreIrInstruction;
class CoreIrValue;

enum class CoreIrAliasKind : unsigned char {
    NoAlias,
    MayAlias,
    MustAlias,
};

CoreIrAliasKind
alias_core_ir_memory_locations(const CoreIrMemoryLocation &lhs,
                               const CoreIrMemoryLocation &rhs) noexcept;

class CoreIrAliasAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation>
        value_locations_;
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
        instruction_locations_;

  public:
    CoreIrAliasAnalysisResult() = default;
    CoreIrAliasAnalysisResult(
        const CoreIrFunction *function,
        std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation>
            value_locations,
        std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
            instruction_locations) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const CoreIrMemoryLocation *
    get_location_for_value(const CoreIrValue *value) const noexcept;

    const CoreIrMemoryLocation *get_location_for_instruction(
        const CoreIrInstruction *instruction) const noexcept;

    CoreIrAliasKind alias_values(const CoreIrValue *lhs,
                                 const CoreIrValue *rhs) const noexcept;

    CoreIrAliasKind
    alias_instructions(const CoreIrInstruction *lhs,
                       const CoreIrInstruction *rhs) const noexcept;
};

class CoreIrAliasAnalysis {
  public:
    using ResultType = CoreIrAliasAnalysisResult;

    CoreIrAliasAnalysisResult Run(const CoreIrFunction &function) const;
};

} // namespace sysycc
