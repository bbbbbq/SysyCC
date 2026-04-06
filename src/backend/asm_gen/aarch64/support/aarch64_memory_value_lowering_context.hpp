#pragma once

#include <cstddef>

#include "backend/asm_gen/aarch64/support/aarch64_memory_access_context.hpp"

namespace sysycc {

class CoreIrValue;
class CoreIrStackSlot;

class AArch64MemoryValueLoweringContext : public AArch64MemoryAccessContext {
  public:
    virtual ~AArch64MemoryValueLoweringContext() = default;

    virtual bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg &out) = 0;
    virtual bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                                const CoreIrValue *value,
                                                AArch64VirtualReg &out) = 0;
    virtual bool materialize_canonical_memory_address(
        AArch64MachineBlock &machine_block, const CoreIrValue *value,
        AArch64VirtualReg &out) = 0;
    virtual bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;
    virtual std::size_t get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
