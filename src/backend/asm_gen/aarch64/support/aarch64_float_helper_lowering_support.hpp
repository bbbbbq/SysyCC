#pragma once

#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_context.hpp"

namespace sysycc {

class CoreIrCastInst;

bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                 AArch64FloatHelperLoweringContext &context,
                                 CoreIrBinaryOpcode opcode,
                                 const AArch64VirtualReg &lhs_reg,
                                 const AArch64VirtualReg &rhs_reg,
                                 const AArch64VirtualReg &dst_reg);

bool emit_float128_compare_helper(AArch64MachineBlock &machine_block,
                                  AArch64FloatHelperLoweringContext &context,
                                  CoreIrComparePredicate predicate,
                                  const AArch64VirtualReg &lhs_reg,
                                  const AArch64VirtualReg &rhs_reg,
                                  const AArch64VirtualReg &dst_reg,
                                  AArch64MachineFunction &function);

bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                               AArch64FloatHelperLoweringContext &context,
                               const CoreIrCastInst &cast,
                               const AArch64VirtualReg &operand_reg,
                               const AArch64VirtualReg &dst_reg,
                               AArch64MachineFunction &function);

} // namespace sysycc
