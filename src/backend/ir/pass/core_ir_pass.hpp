#pragma once

#include <memory>
#include <vector>

namespace sysycc {

class CoreIrModule;
class DiagnosticEngine;

class CoreIrPass {
  public:
    virtual ~CoreIrPass() = default;

    virtual const char *get_name() const noexcept = 0;
    virtual bool Run(CoreIrModule &module,
                     DiagnosticEngine &diagnostic_engine) = 0;
};

class CoreIrNoOpPass final : public CoreIrPass {
  public:
    const char *get_name() const noexcept override;
    bool Run(CoreIrModule &module,
             DiagnosticEngine &diagnostic_engine) override;
};

class CoreIrPassManager {
  private:
    std::vector<std::unique_ptr<CoreIrPass>> passes_;

  public:
    void AddPass(std::unique_ptr<CoreIrPass> pass);
    bool Run(CoreIrModule &module, DiagnosticEngine &diagnostic_engine);
};

} // namespace sysycc
