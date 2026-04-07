#pragma once

#include <cstddef>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;

class AArch64MemoryAccessContext {
  public:
    virtual ~AArch64MemoryAccessContext() = default;

    virtual AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) = 0;
    virtual const CoreIrType *create_fake_pointer_type() const = 0;
    virtual void append_frame_address(AArch64MachineBlock &machine_block,
                                      const AArch64VirtualReg &target_reg,
                                      std::size_t offset,
                                      AArch64MachineFunction &function) = 0;
    virtual bool add_constant_offset(AArch64MachineBlock &machine_block,
                                     const AArch64VirtualReg &base_reg,
                                     long long offset,
                                     AArch64MachineFunction &function) = 0;
};

} // namespace sysycc
