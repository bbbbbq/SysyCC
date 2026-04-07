#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class DiagnosticEngine;

class AArch64RegisterAllocationPass {
  public:
    bool run(AArch64MachineFunction &function, DiagnosticEngine &diagnostic_engine) const;
};

} // namespace sysycc
