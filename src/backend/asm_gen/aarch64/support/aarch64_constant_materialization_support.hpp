#pragma once

#include <cstdint>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_context.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrConstantFloat;

std::string strip_floating_literal_suffix(std::string value_text);
std::string format_bits_literal(std::uint64_t bits, unsigned hex_digits);
std::uint16_t float32_to_float16_bits(float value);
bool floating_literal_is_zero(const std::string &literal_text);
bool float128_literal_is_supported_by_helper_path(const std::string &literal_text);

bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                  AArch64ConstantMaterializationContext &context,
                                  const CoreIrType *type, std::uint64_t value,
                                  const AArch64VirtualReg &target_reg);

bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                AArch64ConstantMaterializationContext &context,
                                const CoreIrConstantFloat &constant,
                                const AArch64VirtualReg &target_reg,
                                AArch64MachineFunction &function);

} // namespace sysycc
