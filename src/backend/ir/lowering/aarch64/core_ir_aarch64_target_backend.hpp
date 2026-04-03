#pragma once

#include "backend/ir/lowering/core_ir_target_backend.hpp"

namespace sysycc {

class CoreIrAArch64TargetBackend final : public CoreIrTargetBackend {
  public:
    IrKind get_kind() const noexcept override;
    std::unique_ptr<IRResult>
    Lower(const CoreIrModule &module, DiagnosticEngine &diagnostic_engine) override;
};

} // namespace sysycc
