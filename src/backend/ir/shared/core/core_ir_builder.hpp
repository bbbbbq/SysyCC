#pragma once

#include "backend/ir/analysis/analysis_manager.hpp"
#include <memory>

#include "backend/ir/shared/core/ir_context.hpp"

namespace sysycc {

class CoreIrFunction;
class CompilerContext;
class CoreIrModule;

class CoreIrBuildResult {
  private:
    std::unique_ptr<CoreIrContext> context_;
    CoreIrModule *module_ = nullptr;
    std::unique_ptr<CoreIrAnalysisManager> analysis_manager_;

  public:
    CoreIrBuildResult(std::unique_ptr<CoreIrContext> context,
                      CoreIrModule *module) noexcept;
    ~CoreIrBuildResult() = default;

    const CoreIrContext *get_context() const noexcept;
    CoreIrContext *get_context() noexcept;

    const CoreIrModule *get_module() const noexcept;
    CoreIrModule *get_module() noexcept;

    const CoreIrAnalysisManager *get_analysis_manager() const noexcept;
    CoreIrAnalysisManager *get_analysis_manager() noexcept;

    void invalidate_all_core_ir_analyses() noexcept;
    void invalidate_core_ir_analyses(CoreIrFunction &function) noexcept;
};

class CoreIrBuilder {
  public:
    std::unique_ptr<CoreIrBuildResult> Build(CompilerContext &context);
};

} // namespace sysycc
