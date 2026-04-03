#include "backend/ir/pipeline/core_ir_pipeline.hpp"

#include <memory>

#include "backend/ir/core/core_ir_builder.hpp"
#include "backend/ir/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lowering/core_ir_target_backend_factory.hpp"
#include "backend/ir/pass/core_ir_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::unique_ptr<CoreIrBuildResult>
CoreIrPipeline::BuildAndOptimize(CompilerContext &context) {
    CoreIrBuilder builder;
    std::unique_ptr<CoreIrBuildResult> build_result = builder.Build(context);
    if (build_result == nullptr) {
        return nullptr;
    }

    CoreIrPassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrNoOpPass>());
    if (!pass_manager.Run(*build_result->get_module(),
                          context.get_diagnostic_engine())) {
        return nullptr;
    }
    return build_result;
}

std::unique_ptr<IRResult>
CoreIrPipeline::BuildOptimizeAndLower(CompilerContext &context) {
    std::unique_ptr<CoreIrBuildResult> build_result = BuildAndOptimize(context);
    if (build_result == nullptr) {
        return nullptr;
    }

    std::unique_ptr<CoreIrTargetBackend> target_backend =
        create_core_ir_target_backend(target_kind_);
    if (target_backend == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "failed to create core ir target backend");
        return nullptr;
    }
    return target_backend->Lower(*build_result->get_module(),
                                 context.get_diagnostic_engine());
}

} // namespace sysycc
