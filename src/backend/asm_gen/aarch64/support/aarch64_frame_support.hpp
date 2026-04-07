#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

std::string load_mnemonic_for_kind(AArch64VirtualRegKind kind, std::size_t size);
std::string store_mnemonic_for_kind(AArch64VirtualRegKind kind, std::size_t size);
void append_materialize_physical_integer_constant(
    std::vector<AArch64MachineInstr> &instructions, unsigned physical_reg,
    std::uint64_t value);
void append_frame_address_into_physical_reg(
    std::vector<AArch64MachineInstr> &instructions, unsigned address_reg,
    std::size_t offset);
void append_physical_frame_load(std::vector<AArch64MachineInstr> &instructions,
                                unsigned value_reg, AArch64VirtualRegKind kind,
                                std::size_t size, std::size_t offset,
                                unsigned address_temp_reg);
void append_physical_frame_store(std::vector<AArch64MachineInstr> &instructions,
                                 unsigned value_reg, AArch64VirtualRegKind kind,
                                 std::size_t size, std::size_t offset,
                                 unsigned address_temp_reg);

} // namespace sysycc
