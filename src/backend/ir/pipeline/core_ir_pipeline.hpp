#pragma once

#include <memory>

#include "backend/ir/ir_kind.hpp"

namespace sysycc {

class CompilerContext;
class CoreIrBuildResult;
class IRResult;

class CoreIrPipeline {
  private:
    IrKind target_kind_ = IrKind::LLVM;

  public:
    explicit CoreIrPipeline(IrKind target_kind) noexcept
        : target_kind_(target_kind) {}

    IrKind get_target_kind() const noexcept { return target_kind_; }

    std::unique_ptr<CoreIrBuildResult> BuildAndOptimize(CompilerContext &context);
    std::unique_ptr<IRResult> BuildOptimizeAndLower(CompilerContext &context);
};

} // namespace sysycc
