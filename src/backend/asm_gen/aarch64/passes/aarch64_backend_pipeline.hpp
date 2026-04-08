#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_frame_finalize_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_register_allocation_pass.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"
#include "backend/asm_gen/asm_result.hpp"

namespace sysycc {

class DiagnosticEngine;

class AArch64BackendPipeline {
  private:
    AArch64RegisterAllocationPass register_allocation_pass_;
    AArch64SpillRewritePass spill_rewrite_pass_;
    AArch64FrameFinalizePass frame_finalize_pass_;
    AArch64EmissionPass emission_pass_;

  public:
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
