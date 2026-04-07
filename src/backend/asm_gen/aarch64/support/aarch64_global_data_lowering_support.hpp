#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_context.hpp"

namespace sysycc {

class CoreIrModule;
class CoreIrGlobal;
class CoreIrConstant;
class CoreIrType;

bool append_global_constant_fragments(AArch64DataObject &data_object,
                                      const CoreIrConstant *constant,
                                      const CoreIrType *type,
                                      AArch64GlobalDataLoweringContext &context);

bool append_global(AArch64MachineModule &machine_module, const CoreIrGlobal &global,
                   AArch64GlobalDataLoweringContext &context);

bool append_globals(AArch64MachineModule &machine_module, const CoreIrModule &module,
                    AArch64GlobalDataLoweringContext &context);

} // namespace sysycc
