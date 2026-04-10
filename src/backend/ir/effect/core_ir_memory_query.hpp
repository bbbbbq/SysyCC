#pragma once

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/escape_analysis.hpp"

namespace sysycc {

class CoreIrInstruction;

bool core_ir_memory_location_is_precise(
    const CoreIrMemoryLocation *location) noexcept;

CoreIrAliasKind get_precise_core_ir_memory_alias_kind(
    const CoreIrInstruction &lhs, const CoreIrInstruction &rhs,
    const CoreIrAliasAnalysisResult &alias_analysis) noexcept;

bool core_ir_memory_location_is_non_escaping_local(
    const CoreIrMemoryLocation *location,
    const CoreIrEscapeAnalysisResult *escape_analysis) noexcept;

} // namespace sysycc
