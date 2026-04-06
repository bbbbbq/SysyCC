#pragma once

#include <cstddef>

#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_context.hpp"

namespace sysycc {

class CoreIrStackSlot;

class AArch64ValueMaterializationContext
    : public AArch64AddressMaterializationContext,
      public AArch64ConstantMaterializationContext {
  public:
    virtual ~AArch64ValueMaterializationContext() = default;

    virtual void report_error(const std::string &message) = 0;
    virtual void append_frame_address(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &target_reg,
                                      std::size_t offset,
                                      AArch64MachineFunction &function) = 0;
    virtual std::size_t get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const = 0;
};

} // namespace sysycc
