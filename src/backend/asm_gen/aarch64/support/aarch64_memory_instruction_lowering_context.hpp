#pragma once

#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrValue;
class CoreIrStackSlot;

class AArch64MemoryInstructionLoweringContext {
  public:
    virtual ~AArch64MemoryInstructionLoweringContext() = default;

    virtual bool is_promoted_stack_slot(const CoreIrStackSlot *stack_slot) const = 0;
    virtual bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg &out) = 0;
    virtual bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;
    virtual std::optional<AArch64VirtualReg>
    get_promoted_stack_slot_value(const CoreIrStackSlot *stack_slot) const = 0;
    virtual void set_promoted_stack_slot_value(
        const CoreIrStackSlot *stack_slot, const AArch64VirtualReg &value_reg) = 0;
    virtual void append_register_copy(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &target_reg,
                                      const AArch64VirtualReg &source_reg) = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
