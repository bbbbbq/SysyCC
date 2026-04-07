#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrConstantFloat;

class AArch64FloatHelperLoweringContext {
  public:
    virtual ~AArch64FloatHelperLoweringContext() = default;

    virtual const CoreIrType *create_fake_pointer_type() const = 0;
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
    virtual void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                               const AArch64VirtualReg &reg,
                                               const CoreIrType *type) = 0;
    virtual void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                                  const AArch64VirtualReg &dst_reg,
                                                  const CoreIrType *source_type,
                                                  const CoreIrType *target_type) = 0;
    virtual void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                                  const AArch64VirtualReg &dst_reg,
                                                  const CoreIrType *source_type,
                                                  const CoreIrType *target_type) = 0;
    virtual AArch64VirtualReg promote_float16_to_float32(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &source_reg,
        AArch64MachineFunction &function) = 0;
    virtual void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &source_reg,
                                           const AArch64VirtualReg &target_reg) = 0;
    virtual bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                            const CoreIrConstantFloat &constant,
                                            const AArch64VirtualReg &target_reg,
                                            AArch64MachineFunction &function) = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
