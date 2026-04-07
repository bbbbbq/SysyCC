#pragma once

#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"

namespace sysycc {

class CoreIrValue;
class CoreIrFunction;
class CoreIrReturnInst;

bool lower_function_entry_parameters(
    AArch64MachineBlock &prologue_block, const CoreIrFunction &function,
    const AArch64FunctionAbiInfo &abi_info, AArch64MachineFunction &machine_function,
    AArch64AbiEmissionContext &context);

bool emit_function_return(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrReturnInst &return_inst,
                          const AArch64FunctionAbiInfo &abi_info,
                          const AArch64VirtualReg &indirect_result_address,
                          AArch64AbiEmissionContext &context);

} // namespace sysycc
