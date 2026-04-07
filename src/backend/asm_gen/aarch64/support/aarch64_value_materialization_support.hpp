#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_context.hpp"

namespace sysycc {

class CoreIrValue;

bool materialize_noncanonical_value(AArch64MachineBlock &machine_block,
                                    AArch64ValueMaterializationContext &context,
                                    const CoreIrValue *value,
                                    const AArch64VirtualReg &target_reg,
                                    AArch64MachineFunction &function);

} // namespace sysycc
