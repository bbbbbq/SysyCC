#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "backend/asm_gen/aarch64/model/aarch64_machine_ir.hpp"

namespace sysycc {

bool is_supported_scalar_fp_kind(AArch64VirtualRegKind kind);
std::size_t scalar_fp_size(AArch64VirtualRegKind kind);
std::optional<std::uint32_t> fp_reg_move_base(AArch64VirtualRegKind kind);
std::optional<std::uint32_t>
fp_gp_move_base(AArch64VirtualRegKind fp_kind, bool gp_is_64bit, bool gp_to_fp);
std::optional<std::uint32_t>
fp_binary_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind kind);
std::optional<std::uint32_t>
fp_ternary_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind kind);
std::optional<std::uint32_t> fcmp_base(AArch64VirtualRegKind kind);
std::optional<std::uint32_t>
int_to_fp_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind fp_kind,
               bool src_is_64bit);
std::optional<std::uint32_t>
fp_to_int_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind fp_kind,
               bool dst_is_64bit);
std::optional<std::uint32_t>
fp_convert_base(AArch64VirtualRegKind dst_kind, AArch64VirtualRegKind src_kind);

} // namespace sysycc
