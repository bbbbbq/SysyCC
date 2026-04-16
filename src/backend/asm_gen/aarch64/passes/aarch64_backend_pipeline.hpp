#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_frame_finalize_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_register_allocation_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_backend_context.hpp"
#include "backend/asm_gen/asm_result.hpp"
#include "common/diagnostic/diagnostic.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

class CoreIrModule;

class AArch64BackendPipeline {
  private:
    AArch64MachineLoweringPass machine_lowering_pass_;
    AArch64RegisterAllocationPass register_allocation_pass_;
    AArch64SpillRewritePass spill_rewrite_pass_;
    AArch64FrameFinalizePass frame_finalize_pass_;
    AArch64EmissionPass emission_pass_;

  public:
    bool build_and_finalize_module(const AArch64CodegenInput &input,
                                   const BackendOptions &backend_options,
                                   DiagnosticEngine &diagnostic_engine,
                                   AArch64AsmModule &asm_module,
                                   AArch64MachineModule &machine_module,
                                   AArch64ObjectModule &object_module) const {
        AArch64CodegenContext codegen_context{
            &input,
            &backend_options,
            &diagnostic_engine,
            AArch64AsmModule{},
            AArch64MachineModule{},
            AArch64ObjectModule{}};
        if (!build_and_finalize_module(codegen_context)) {
            return false;
        }
        asm_module = std::move(codegen_context.asm_module);
        machine_module = std::move(codegen_context.machine_module);
        object_module = std::move(codegen_context.object_module);
        return true;
    }

    bool build_and_finalize_module(const CoreIrModule &module,
                                   const BackendOptions &backend_options,
                                   DiagnosticEngine &diagnostic_engine,
                                   AArch64AsmModule &asm_module,
                                   AArch64MachineModule &machine_module,
                                   AArch64ObjectModule &object_module) const {
        const AArch64CoreIrCodegenInputAdapter input(module);
        return build_and_finalize_module(input, backend_options,
                                         diagnostic_engine, asm_module,
                                         machine_module, object_module);
    }

    bool build_module(AArch64CodegenContext &codegen_context) const {
        return machine_lowering_pass_.run(codegen_context);
    }

    bool finalize_function(AArch64MachineFunction &function,
                           DiagnosticEngine &diagnostic_engine) const {
        if (!register_allocation_pass_.run(function, diagnostic_engine)) {
            return false;
        }
        if (!spill_rewrite_pass_.run(function, diagnostic_engine)) {
            return false;
        }
        frame_finalize_pass_.run(function);
        return true;
    }

    bool finalize_module(AArch64MachineModule &module,
                         DiagnosticEngine &diagnostic_engine) const {
        for (AArch64MachineFunction &function : module.get_functions()) {
            if (!finalize_function(function, diagnostic_engine)) {
                return false;
            }
        }
        return true;
    }

    bool build_and_finalize_module(AArch64CodegenContext &codegen_context) const {
        if (!build_module(codegen_context)) {
            if (!codegen_context.diagnostic_engine->has_error()) {
                codegen_context.diagnostic_engine->add_error(
                    DiagnosticStage::Compiler,
                    "AArch64 backend machine lowering failed without emitting a "
                    "specific diagnostic");
            }
            return false;
        }
        if (!finalize_module(codegen_context.machine_module,
                             *codegen_context.diagnostic_engine)) {
            if (!codegen_context.diagnostic_engine->has_error()) {
                codegen_context.diagnostic_engine->add_error(
                    DiagnosticStage::Compiler,
                    "AArch64 backend finalization failed without emitting a "
                    "specific diagnostic");
            }
            return false;
        }
        return true;
    }

    std::unique_ptr<AsmResult>
    emit_asm_result(const AArch64AsmModule &asm_module,
                    const AArch64MachineModule &machine_module,
                    const AArch64ObjectModule &object_module) const {
        return emission_pass_.emit_asm_result(asm_module, machine_module,
                                              object_module);
    }

    std::unique_ptr<ObjectResult>
    emit_object_result(const AArch64MachineModule &machine_module,
                       const AArch64ObjectModule &object_module,
                       const BackendOptions &backend_options,
                       const std::filesystem::path &object_file,
                       DiagnosticEngine &diagnostic_engine) const {
        return emission_pass_.emit_object_result(
            machine_module, object_module, backend_options, object_file,
            diagnostic_engine);
    }
};

} // namespace sysycc
