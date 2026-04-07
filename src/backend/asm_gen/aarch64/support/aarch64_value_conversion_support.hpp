#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;

void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &reg,
                                   const CoreIrType *type);

void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &dst_reg,
                                      const CoreIrType *source_type,
                                      const CoreIrType *target_type);

void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &dst_reg,
                                      const CoreIrType *source_type,
                                      const CoreIrType *target_type);

AArch64VirtualReg promote_float16_to_float32(AArch64MachineBlock &machine_block,
                                             const AArch64VirtualReg &source_reg,
                                             AArch64MachineFunction &function);

void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                               const AArch64VirtualReg &source_reg,
                               const AArch64VirtualReg &target_reg);

} // namespace sysycc
