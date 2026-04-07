#pragma once

#include <cstddef>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"

namespace sysycc {

class CoreIrType;

std::size_t compute_call_stack_arg_bytes(const AArch64FunctionAbiInfo &abi_info);
const CoreIrType *type_for_abi_location(const AArch64AbiLocation &location);

bool copy_aggregate_from_assignment_to_memory(
    AArch64MachineBlock &machine_block, const AArch64AbiAssignment &assignment,
    const CoreIrType *value_type, const AArch64VirtualReg &destination_address,
    AArch64MachineFunction &function, AArch64AbiEmissionContext &context);

bool copy_aggregate_from_memory_to_assignment(
    AArch64MachineBlock &machine_block, const AArch64AbiAssignment &assignment,
    const CoreIrType *value_type, const AArch64VirtualReg &source_address,
    AArch64MachineFunction &function, AArch64AbiEmissionContext &context,
    const std::vector<std::size_t> *indirect_call_argument_copy_offsets = nullptr,
    const AArch64VirtualReg *stack_base_address = nullptr,
    std::size_t argument_index = 0);

} // namespace sysycc
