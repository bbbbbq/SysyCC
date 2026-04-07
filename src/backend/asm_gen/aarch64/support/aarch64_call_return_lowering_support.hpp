#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_context.hpp"

namespace sysycc {

class CoreIrCallInst;
class CoreIrReturnInst;

bool emit_call_instruction(AArch64MachineBlock &machine_block,
                           AArch64CallReturnLoweringContext &context,
                           AArch64AbiEmissionContext &abi_context,
                           const CoreIrCallInst &call,
                           AArch64MachineFunction &function);

bool emit_return_instruction(AArch64MachineFunction &machine_function,
                             AArch64MachineBlock &machine_block,
                             AArch64CallReturnLoweringContext &context,
                             AArch64AbiEmissionContext &abi_context,
                             const CoreIrReturnInst &return_inst);

} // namespace sysycc
