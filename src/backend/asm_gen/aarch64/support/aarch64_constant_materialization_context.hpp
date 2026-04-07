#pragma once

#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;

class AArch64ConstantMaterializationContext {
  public:
    virtual ~AArch64ConstantMaterializationContext() = default;

    virtual const CoreIrType *create_fake_pointer_type() const = 0;
    virtual void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                               const AArch64VirtualReg &reg,
                                               const CoreIrType *type) = 0;
    virtual void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                             unsigned physical_reg,
                                             AArch64VirtualRegKind reg_kind,
                                             const AArch64VirtualReg &source_reg) = 0;
    virtual void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                               const AArch64VirtualReg &target_reg,
                                               unsigned physical_reg,
                                               AArch64VirtualRegKind reg_kind) = 0;
    virtual void append_helper_call(AArch64MachineBlock &machine_block,
                                    const std::string &symbol_name) = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
