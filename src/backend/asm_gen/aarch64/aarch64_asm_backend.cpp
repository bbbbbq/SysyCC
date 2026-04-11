#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"

#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"

namespace sysycc {

bool AArch64AsmBackend::BuildModule(const CoreIrModule &module,
                                    const BackendOptions &backend_options,
                                    DiagnosticEngine &diagnostic_engine,
                                    AArch64AsmModule &asm_module,
                                    AArch64MachineModule &machine_module,
                                    AArch64ObjectModule &object_module) const {
    AArch64BackendPipeline backend_pipeline;
    return backend_pipeline.build_and_finalize_module(
        module, backend_options, diagnostic_engine, asm_module, machine_module,
        object_module);
}

std::unique_ptr<AsmResult>
AArch64AsmBackend::Generate(const CoreIrModule &module,
                            const BackendOptions &backend_options,
                            DiagnosticEngine &diagnostic_engine) const {
    AArch64AsmModule asm_module;
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
    if (!BuildModule(module, backend_options, diagnostic_engine, asm_module, machine_module,
                     object_module)) {
        return nullptr;
    }

    AArch64BackendPipeline backend_pipeline;
    return backend_pipeline.emit_asm_result(asm_module, machine_module, object_module);
}

} // namespace sysycc
