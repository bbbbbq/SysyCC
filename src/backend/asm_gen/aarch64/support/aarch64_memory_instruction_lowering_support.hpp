#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_context.hpp"

namespace sysycc {

class CoreIrLoadInst;
class CoreIrStoreInst;

bool emit_load_instruction(AArch64MachineBlock &machine_block,
                           AArch64MemoryInstructionLoweringContext &context,
                           AArch64MemoryValueLoweringContext &memory_value_context,
                           const CoreIrLoadInst &load,
                           AArch64MachineFunction &function);

bool emit_store_instruction(AArch64MachineBlock &machine_block,
                            AArch64MemoryInstructionLoweringContext &context,
                            AArch64MemoryValueLoweringContext &memory_value_context,
                            const CoreIrStoreInst &store,
                            AArch64MachineFunction &function);

} // namespace sysycc
