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

    const CoreIrContext *get_context() const noexcept { return context_.get(); }
    CoreIrContext *get_context() noexcept { return context_.get(); }

    const CoreIrModule *get_module() const noexcept { return module_; }
    CoreIrModule *get_module() noexcept { return module_; }
};

class CoreIrBuilder {
  public:
    std::unique_ptr<CoreIrBuildResult> Build(CompilerContext &context);
};

} // namespace sysycc
