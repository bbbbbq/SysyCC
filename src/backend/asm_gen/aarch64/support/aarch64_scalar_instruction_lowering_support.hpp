#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_scalar_lowering_context.hpp"

namespace sysycc {

class CoreIrBinaryInst;
class CoreIrUnaryInst;
class CoreIrCompareInst;
class CoreIrSelectInst;
class CoreIrCastInst;

bool emit_binary_instruction(AArch64MachineBlock &machine_block,
                             AArch64ScalarLoweringContext &context,
                             const CoreIrBinaryInst &binary,
                             AArch64MachineFunction &function);

bool emit_unary_instruction(AArch64MachineBlock &machine_block,
                            AArch64ScalarLoweringContext &context,
                            const CoreIrUnaryInst &unary,
                            AArch64MachineFunction &function);

bool emit_compare_instruction(AArch64MachineBlock &machine_block,
                              AArch64ScalarLoweringContext &context,
                              const CoreIrCompareInst &compare,
                              AArch64MachineFunction &function);

bool emit_select_instruction(AArch64MachineBlock &machine_block,
                             AArch64ScalarLoweringContext &context,
                             const CoreIrSelectInst &select,
                             AArch64MachineFunction &function);

bool emit_cast_instruction(AArch64MachineBlock &machine_block,
                           AArch64ScalarLoweringContext &context,
                           const CoreIrCastInst &cast,
                           AArch64MachineFunction &function);

} // namespace sysycc
