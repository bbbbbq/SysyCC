#pragma once

#include <optional>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"

namespace sysycc {

class CoreIrCallInst;

bool emit_call_with_abi(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                        const AArch64FunctionAbiInfo &abi_info,
                        AArch64MachineFunction &machine_function,
                        AArch64AbiEmissionContext &context,
                        const std::vector<std::size_t> *indirect_copy_offsets =
                            nullptr);

} // namespace sysycc
