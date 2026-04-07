#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_context.hpp"

namespace sysycc {

class CoreIrAddressOfStackSlotInst;
class CoreIrAddressOfGlobalInst;
class CoreIrAddressOfFunctionInst;
class CoreIrGetElementPtrInst;

bool emit_address_of_stack_slot_value(
    AArch64MachineBlock &machine_block,
    AArch64ValueMaterializationContext &context,
    const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
    const AArch64VirtualReg &target_reg, AArch64MachineFunction &function);

bool emit_address_of_global_value(AArch64MachineBlock &machine_block,
                                  AArch64ValueMaterializationContext &context,
                                  const CoreIrAddressOfGlobalInst &address_of_global,
                                  const AArch64VirtualReg &target_reg);

bool emit_address_of_function_value(
    AArch64MachineBlock &machine_block,
    AArch64ValueMaterializationContext &context,
    const CoreIrAddressOfFunctionInst &address_of_function,
    const AArch64VirtualReg &target_reg);

bool emit_getelementptr_value(AArch64MachineBlock &machine_block,
                              AArch64ValueMaterializationContext &context,
                              const CoreIrGetElementPtrInst &gep,
                              const AArch64VirtualReg &target_reg,
                              AArch64MachineFunction &function);

} // namespace sysycc
