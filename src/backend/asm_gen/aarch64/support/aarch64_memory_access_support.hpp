#pragma once

#include <cstddef>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_memory_access_context.hpp"

namespace sysycc {

class CoreIrType;

std::string load_mnemonic_for_type(const CoreIrType *type);
std::string store_mnemonic_for_type(const CoreIrType *type);

bool append_memory_store(AArch64MachineBlock &machine_block,
                         AArch64MemoryAccessContext &context,
                         const CoreIrType *type,
                         const AArch64MachineOperand &source_operand,
                         const AArch64VirtualReg &address_reg, std::size_t offset,
                         AArch64MachineFunction &function);

bool emit_zero_fill(AArch64MachineBlock &machine_block,
                    AArch64MemoryAccessContext &context,
                    const AArch64VirtualReg &address_reg, const CoreIrType *type,
                    AArch64MachineFunction &function);

void append_load_from_frame(AArch64MachineBlock &machine_block,
                            AArch64MemoryAccessContext &context,
                            const CoreIrType *type,
                            const AArch64VirtualReg &target_reg, std::size_t offset,
                            AArch64MachineFunction &function);

void append_store_to_frame(AArch64MachineBlock &machine_block,
                           AArch64MemoryAccessContext &context,
                           const CoreIrType *type,
                           const AArch64VirtualReg &source_reg, std::size_t offset,
                           AArch64MachineFunction &function);

void append_load_from_incoming_stack_arg(AArch64MachineBlock &machine_block,
                                         AArch64MemoryAccessContext &context,
                                         const CoreIrType *type,
                                         const AArch64VirtualReg &target_reg,
                                         std::size_t offset,
                                         AArch64MachineFunction &function);

bool append_load_from_address(AArch64MachineBlock &machine_block,
                              AArch64MemoryAccessContext &context,
                              const CoreIrType *type,
                              const AArch64VirtualReg &target_reg,
                              const AArch64VirtualReg &address_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function);

bool append_store_to_address(AArch64MachineBlock &machine_block,
                             AArch64MemoryAccessContext &context,
                             const CoreIrType *type,
                             const AArch64VirtualReg &source_reg,
                             const AArch64VirtualReg &address_reg,
                             std::size_t offset,
                             AArch64MachineFunction &function);

bool emit_memory_copy(AArch64MachineBlock &machine_block,
                      AArch64MemoryAccessContext &context,
                      const AArch64VirtualReg &destination_address,
                      const AArch64VirtualReg &source_address,
                      const CoreIrType *type, AArch64MachineFunction &function);

} // namespace sysycc
