#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/asm_result.hpp"
#include "backend/asm_gen/backend_options.hpp"
#include "backend/asm_gen/object_result.hpp"

namespace sysycc {

class DiagnosticEngine;

class AArch64EmissionPass {
  public:
    std::string print_module(const AArch64AsmModule &asm_module,
                             const AArch64MachineModule &machine_module,
                             const AArch64ObjectModule &object_module) const;
    std::unique_ptr<AsmResult>
    emit_asm_result(const AArch64AsmModule &asm_module,
                    const AArch64MachineModule &machine_module,
                    const AArch64ObjectModule &object_module) const;
    std::unique_ptr<ObjectResult>
    emit_object_result(const AArch64MachineModule &machine_module,
                       const AArch64ObjectModule &object_module,
                       const BackendOptions &backend_options,
                       const std::filesystem::path &object_file,
                       DiagnosticEngine &diagnostic_engine) const;
};

} // namespace sysycc
