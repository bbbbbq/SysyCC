#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"

#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_backend_context.hpp"

namespace sysycc {

std::unique_ptr<AsmResult>
AArch64AsmBackend::Generate(const CoreIrModule &module,
                            const BackendOptions &backend_options,
                            DiagnosticEngine &diagnostic_engine) const {
    AArch64CodegenContext codegen_context{
        &module, &backend_options, &diagnostic_engine, AArch64MachineModule{}};
    AArch64MachineLoweringPass machine_lowering_pass;
    if (!machine_lowering_pass.run(codegen_context)) {
        return nullptr;
    }

    AArch64BackendPipeline backend_pipeline;
    if (!backend_pipeline.finalize_module(codegen_context.machine_module,
                                          diagnostic_engine)) {
        return nullptr;
    }
    return backend_pipeline.emit_asm_result(codegen_context.machine_module);
}

} // namespace sysycc
