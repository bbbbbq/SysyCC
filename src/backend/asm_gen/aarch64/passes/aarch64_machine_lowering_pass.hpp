#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_backend_context.hpp"

namespace sysycc {

class AArch64MachineLoweringPass {
  public:
    bool run(AArch64CodegenContext &codegen_context) const;
};

} // namespace sysycc
