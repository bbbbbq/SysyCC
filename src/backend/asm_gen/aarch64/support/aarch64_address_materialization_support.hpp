#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_context.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrValue;

bool materialize_global_address(AArch64MachineBlock &machine_block,
                                AArch64AddressMaterializationContext &context,
                                const std::string &symbol_name,
                                const AArch64VirtualReg &target_reg,
                                AArch64SymbolKind symbol_kind =
                                    AArch64SymbolKind::Object);

bool materialize_gep_value(
    AArch64MachineBlock &machine_block, AArch64AddressMaterializationContext &context,
    const CoreIrValue *base_value, const CoreIrType *base_type,
    std::size_t index_count,
    const std::function<CoreIrValue *(std::size_t)> &get_index_value,
    const AArch64VirtualReg &target_reg, AArch64MachineFunction &function);

} // namespace sysycc
