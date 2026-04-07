#pragma once

#include <memory>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/asm_result.hpp"
#include "backend/asm_gen/backend_options.hpp"

namespace sysycc {

class CoreIrModule;
class DiagnosticEngine;

class AArch64AsmBackend {
  public:
    std::unique_ptr<AsmResult>
    Generate(const CoreIrModule &module, const BackendOptions &backend_options,
             DiagnosticEngine &diagnostic_engine) const;
};

} // namespace sysycc
