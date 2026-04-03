#pragma once

#include <memory>

namespace sysycc {

class CoreIrModule;
class DiagnosticEngine;
class IRResult;
enum class IrKind : unsigned char;

class CoreIrTargetBackend {
  public:
    virtual ~CoreIrTargetBackend() = default;

    virtual IrKind get_kind() const noexcept = 0;
    virtual std::unique_ptr<IRResult>
    Lower(const CoreIrModule &module, DiagnosticEngine &diagnostic_engine) = 0;
};

} // namespace sysycc
