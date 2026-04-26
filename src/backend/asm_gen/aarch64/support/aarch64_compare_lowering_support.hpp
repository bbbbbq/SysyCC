#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrCompareInst;

bool emit_non_float128_compare(AArch64MachineBlock &machine_block,
                               const CoreIrCompareInst &compare,
                               const AArch64VirtualReg &lhs_reg,
                               const AArch64VirtualReg &rhs_reg,
                               const AArch64VirtualReg &dst_reg,
                               AArch64MachineFunction &function);
bool emit_non_float128_compare_immediate(AArch64MachineBlock &machine_block,
                                         const CoreIrCompareInst &compare,
                                         const AArch64VirtualReg &lhs_reg,
                                         long long rhs_immediate,
                                         const AArch64VirtualReg &dst_reg,
                                         AArch64MachineFunction &function);

} // namespace sysycc
