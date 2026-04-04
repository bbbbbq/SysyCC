#include "backend/ir/build/build_core_ir_pass.hpp"

#include <memory>

#include "backend/ir/shared/core/core_ir_builder.hpp"

namespace sysycc {

PassKind BuildCoreIrPass::Kind() const { return PassKind::BuildCoreIr; }

const char *BuildCoreIrPass::Name() const { return "BuildCoreIrPass"; }

PassResult BuildCoreIrPass::Run(CompilerContext &context) {
    context.clear_core_ir_build_result();
    CoreIrBuilder builder;
    std::unique_ptr<CoreIrBuildResult> build_result = builder.Build(context);
    if (build_result == nullptr) {
        return PassResult::Failure("failed to build core ir");
    }
    context.set_core_ir_build_result(std::move(build_result));
    return PassResult::Success();
}

} // namespace sysycc
