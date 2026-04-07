#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrCastInst;

bool emit_non_float128_cast(AArch64MachineBlock &machine_block,
                            const CoreIrCastInst &cast,
                            const AArch64VirtualReg &operand_reg,
                            const AArch64VirtualReg &dst_reg,
                            AArch64MachineFunction &function);

} // namespace sysycc
