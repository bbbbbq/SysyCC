#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class AArch64FrameFinalizePass {
  public:
    void run(AArch64MachineFunction &function) const;
};

} // namespace sysycc
