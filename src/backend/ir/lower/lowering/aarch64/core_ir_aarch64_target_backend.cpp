#include "backend/ir/lower/lowering/aarch64/core_ir_aarch64_target_backend.hpp"

#include <memory>

#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

IrKind CoreIrAArch64TargetBackend::get_kind() const noexcept {
    return IrKind::AArch64;
}

std::unique_ptr<IRResult>
CoreIrAArch64TargetBackend::Lower(const CoreIrModule &module,
                                  DiagnosticEngine &diagnostic_engine) {
    static_cast<void>(module);
    diagnostic_engine.add_error(
        DiagnosticStage::Compiler,
        "core ir aarch64 target backend is not implemented yet");
    return nullptr;
}

} // namespace sysycc
