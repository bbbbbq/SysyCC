#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

bool emit_call_instruction(AArch64MachineBlock &machine_block,
                           AArch64CallReturnLoweringContext &context,
                           AArch64AbiEmissionContext &abi_context,
                           const CoreIrCallInst &call,
                           AArch64MachineFunction &function) {
    return emit_call_with_abi(machine_block, call, context.classify_call(call),
                              function, abi_context,
                              context.lookup_indirect_call_copy_offsets(call));
}

bool emit_return_instruction(AArch64MachineFunction &machine_function,
                             AArch64MachineBlock &machine_block,
                             AArch64CallReturnLoweringContext &context,
                             AArch64AbiEmissionContext &abi_context,
                             const CoreIrReturnInst &return_inst) {
    return emit_function_return(machine_function, machine_block, return_inst,
                                context.function_abi_info(),
                                context.indirect_result_address(), abi_context);
}

} // namespace sysycc
