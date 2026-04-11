#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrValue;

class AArch64AddressMaterializationContext {
  public:
    virtual ~AArch64AddressMaterializationContext() = default;

    virtual AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) = 0;
    virtual const CoreIrType *create_fake_pointer_type() const = 0;
    virtual bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg &out) = 0;
    virtual bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                              const CoreIrType *type,
                                              std::uint64_t value,
                                              const AArch64VirtualReg &target_reg,
                                              AArch64MachineFunction &function) = 0;
    virtual bool add_constant_offset(AArch64MachineBlock &machine_block,
                                     const AArch64VirtualReg &base_reg,
                                     long long offset,
                                     AArch64MachineFunction &function) = 0;
    virtual void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                                  const AArch64VirtualReg &dst_reg,
                                                  const CoreIrType *source_type,
                                                  const CoreIrType *target_type) = 0;
    virtual void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                                  const AArch64VirtualReg &dst_reg,
                                                  const CoreIrType *source_type,
                                                  const CoreIrType *target_type) = 0;
    virtual void record_symbol_reference(const std::string &name,
                                         AArch64SymbolKind kind) = 0;
    virtual AArch64SymbolReference
    make_symbol_reference(const std::string &name, AArch64SymbolKind kind,
                          AArch64SymbolBinding binding,
                          std::optional<AArch64SectionKind> section_kind = std::nullopt,
                          long long addend = 0, bool is_defined = false) const = 0;
    virtual bool is_position_independent() const = 0;
    virtual bool is_nonpreemptible_global_symbol(const std::string &name) const = 0;
    virtual bool is_nonpreemptible_function_symbol(const std::string &name) const = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
