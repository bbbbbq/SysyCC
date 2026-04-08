#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"

#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_backend_context.hpp"

namespace sysycc {

bool AArch64AsmBackend::BuildModule(const CoreIrModule &module,
                                    const BackendOptions &backend_options,
                                    DiagnosticEngine &diagnostic_engine,
                                    AArch64MachineModule &machine_module,
                                    AArch64ObjectModule &object_module) const {
    AArch64CodegenContext codegen_context{
        &module,
        &backend_options,
        &diagnostic_engine,
        AArch64MachineModule{},
        AArch64ObjectModule{}};
    AArch64MachineLoweringPass machine_lowering_pass;
    if (!machine_lowering_pass.run(codegen_context)) {
        return false;
    }

    AArch64BackendPipeline backend_pipeline;
    if (!backend_pipeline.finalize_module(codegen_context.machine_module,
                                          diagnostic_engine)) {
        return false;
    }
    machine_module = std::move(codegen_context.machine_module);
    object_module = std::move(codegen_context.object_module);
    return true;
}

std::unique_ptr<AsmResult>
AArch64AsmBackend::Generate(const CoreIrModule &module,
                            const BackendOptions &backend_options,
                            DiagnosticEngine &diagnostic_engine) const {
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
    if (!BuildModule(module, backend_options, diagnostic_engine, machine_module,
                     object_module)) {
        return nullptr;
    }

    AArch64BackendPipeline backend_pipeline;
    return backend_pipeline.emit_asm_result(machine_module, object_module);
}

} // namespace sysycc
