#pragma once

#include <memory>

#include "backend/ir/shared/core/ir_context.hpp"

namespace sysycc {

class CompilerContext;
class CoreIrModule;

class CoreIrBuildResult {
  private:
    std::unique_ptr<CoreIrContext> context_;
    CoreIrModule *module_ = nullptr;

  public:
    CoreIrBuildResult(std::unique_ptr<CoreIrContext> context,
                      CoreIrModule *module) noexcept;

    const CoreIrContext *get_context() const noexcept;
    CoreIrContext *get_context() noexcept;

    const CoreIrModule *get_module() const noexcept;
    CoreIrModule *get_module() noexcept;
};

class CoreIrBuilder {
  public:
    std::unique_ptr<CoreIrBuildResult> Build(CompilerContext &context);
};

} // namespace sysycc
