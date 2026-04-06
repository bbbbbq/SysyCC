#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_memory_access_context.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrValue;

class AArch64AbiEmissionContext : public AArch64MemoryAccessContext {
  public:
    virtual ~AArch64AbiEmissionContext() = default;

    virtual bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg &out) = 0;
    virtual bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                                const CoreIrValue *value,
                                                AArch64VirtualReg &out) = 0;
    virtual bool
    materialize_canonical_memory_address(AArch64MachineBlock &machine_block,
                                         const CoreIrValue *value,
                                         AArch64VirtualReg &out) = 0;
    virtual bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;

    virtual void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                               const AArch64VirtualReg &target_reg,
                                               unsigned physical_reg,
                                               AArch64VirtualRegKind reg_kind) = 0;
    virtual void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                             unsigned physical_reg,
                                             AArch64VirtualRegKind reg_kind,
                                             const AArch64VirtualReg &source_reg) = 0;
    virtual void append_load_from_incoming_stack_arg(
        AArch64MachineBlock &machine_block, const CoreIrType *type,
        const AArch64VirtualReg &target_reg, std::size_t stack_offset,
        AArch64MachineFunction &function) = 0;
    virtual bool append_load_from_address(AArch64MachineBlock &machine_block,
                                          const CoreIrType *type,
                                          const AArch64VirtualReg &target_reg,
                                          const AArch64VirtualReg &address_reg,
                                          std::size_t offset,
                                          AArch64MachineFunction &function) = 0;
    virtual bool append_store_to_address(AArch64MachineBlock &machine_block,
                                         const CoreIrType *type,
                                         const AArch64VirtualReg &source_reg,
                                         const AArch64VirtualReg &address_reg,
                                         std::size_t offset,
                                         AArch64MachineFunction &function) = 0;
    virtual bool materialize_incoming_stack_address(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
        std::size_t stack_offset, AArch64MachineFunction &function) = 0;

    virtual void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                               const AArch64VirtualReg &reg,
                                               const CoreIrType *type) = 0;
    virtual bool emit_memory_copy(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &destination_address,
                                  const AArch64VirtualReg &source_address,
                                  const CoreIrType *value_type,
                                  AArch64MachineFunction &function) = 0;

    virtual std::optional<AArch64VirtualReg>
    prepare_stack_argument_area(AArch64MachineBlock &machine_block,
                                std::size_t stack_arg_bytes,
                                AArch64MachineFunction &function) = 0;
    virtual void finish_stack_argument_area(AArch64MachineBlock &machine_block,
                                            std::size_t stack_arg_bytes) = 0;
    virtual void emit_direct_call(AArch64MachineBlock &machine_block,
                                  const std::string &callee_name) = 0;
    virtual bool emit_indirect_call(AArch64MachineBlock &machine_block,
                                    const AArch64VirtualReg &callee_reg) = 0;

    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
