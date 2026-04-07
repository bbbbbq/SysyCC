#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_context.hpp"

namespace sysycc {

class CoreIrLoadInst;
class CoreIrStoreInst;

bool emit_nonpromoted_load(AArch64MachineBlock &machine_block,
                           AArch64MemoryValueLoweringContext &context,
                           const CoreIrLoadInst &load,
                           AArch64MachineFunction &function);

bool emit_nonpromoted_store(AArch64MachineBlock &machine_block,
                            AArch64MemoryValueLoweringContext &context,
                            const CoreIrStoreInst &store,
                            AArch64MachineFunction &function);

} // namespace sysycc
