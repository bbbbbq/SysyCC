#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_scalar_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_context.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "common/source_span.hpp"

namespace sysycc {

class DiagnosticEngine;

template <typename FunctionState>
struct AArch64InstructionDispatchContextCallbacks {
    std::function<void(AArch64MachineBlock &, const SourceSpan &, FunctionState &)>
        emit_debug_location;
    std::function<std::string(const FunctionState &, const CoreIrBasicBlock *,
                              const CoreIrBasicBlock *)>
        resolve_branch_target_label;
    std::function<bool(AArch64MachineBlock &, const CoreIrLoadInst &,
                       FunctionState &)>
        emit_load;
    std::function<bool(AArch64MachineBlock &, const CoreIrStoreInst &, FunctionState &)>
        emit_store;
    std::function<bool(AArch64MachineBlock &, const CoreIrBinaryInst &,
                       const FunctionState &)>
        emit_binary;
    std::function<bool(AArch64MachineBlock &, const CoreIrUnaryInst &,
                       const FunctionState &)>
        emit_unary;
    std::function<bool(AArch64MachineBlock &, const CoreIrCompareInst &,
                       const FunctionState &)>
        emit_compare;
    std::function<bool(AArch64MachineBlock &, const CoreIrCastInst &,
                       const FunctionState &)>
        emit_cast;
    std::function<bool(AArch64MachineBlock &, const CoreIrCallInst &,
                       const FunctionState &)>
        emit_call;
    std::function<bool(AArch64MachineBlock &, const CoreIrCondJumpInst &,
                       const FunctionState &, const CoreIrBasicBlock *)>
        emit_cond_jump;
    std::function<bool(AArch64MachineFunction &, AArch64MachineBlock &,
                       const CoreIrReturnInst &, const FunctionState &)>
        emit_return;
    std::function<bool(AArch64MachineBlock &, const CoreIrAddressOfStackSlotInst &,
                       const FunctionState &)>
        emit_address_of_stack_slot;
    std::function<bool(AArch64MachineBlock &, const CoreIrAddressOfGlobalInst &,
                       const FunctionState &)>
        emit_address_of_global;
    std::function<bool(AArch64MachineBlock &, const CoreIrAddressOfFunctionInst &,
                       const FunctionState &)>
        emit_address_of_function;
    std::function<bool(AArch64MachineBlock &, const CoreIrGetElementPtrInst &,
                       const FunctionState &)>
        emit_getelementptr;
};

template <typename FunctionState>
class AArch64CallbackInstructionDispatchContext final {
  private:
    AArch64InstructionDispatchContextCallbacks<FunctionState> callbacks_;

  public:
    explicit AArch64CallbackInstructionDispatchContext(
        AArch64InstructionDispatchContextCallbacks<FunctionState> callbacks)
        : callbacks_(std::move(callbacks)) {}

    void emit_debug_location(AArch64MachineBlock &machine_block,
                             const SourceSpan &source_span, FunctionState &state) {
        callbacks_.emit_debug_location(machine_block, source_span, state);
    }

    std::string resolve_branch_target_label(const FunctionState &state,
                                            const CoreIrBasicBlock *predecessor,
                                            const CoreIrBasicBlock *successor) const {
        return callbacks_.resolve_branch_target_label(state, predecessor, successor);
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   FunctionState &state) {
        return callbacks_.emit_load(machine_block, load, state);
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, FunctionState &state) {
        return callbacks_.emit_store(machine_block, store, state);
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary,
                     const FunctionState &state) {
        return callbacks_.emit_binary(machine_block, binary, state);
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        return callbacks_.emit_unary(machine_block, unary, state);
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        return callbacks_.emit_compare(machine_block, compare, state);
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        return callbacks_.emit_cast(machine_block, cast, state);
    }

    bool emit_call(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                   const FunctionState &state) {
        return callbacks_.emit_call(machine_block, call, state);
    }

    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state,
                        const CoreIrBasicBlock *current_block) {
        return callbacks_.emit_cond_jump(machine_block, cond_jump, state,
                                         current_block);
    }

    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state) {
        return callbacks_.emit_return(machine_function, machine_block, return_inst,
                                      state);
    }

    bool emit_address_of_stack_slot(
        AArch64MachineBlock &machine_block,
        const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
        const FunctionState &state) {
        return callbacks_.emit_address_of_stack_slot(machine_block,
                                                     address_of_stack_slot, state);
    }

    bool emit_address_of_global(AArch64MachineBlock &machine_block,
                                const CoreIrAddressOfGlobalInst &address_of_global,
                                const FunctionState &state) {
        return callbacks_.emit_address_of_global(machine_block, address_of_global,
                                                 state);
    }

    bool emit_address_of_function(
        AArch64MachineBlock &machine_block,
        const CoreIrAddressOfFunctionInst &address_of_function,
        const FunctionState &state) {
        return callbacks_.emit_address_of_function(machine_block,
                                                   address_of_function, state);
    }

    bool emit_getelementptr(AArch64MachineBlock &machine_block,
                            const CoreIrGetElementPtrInst &gep,
                            const FunctionState &state) {
        return callbacks_.emit_getelementptr(machine_block, gep, state);
    }
};

template <typename FunctionState>
auto make_aarch64_instruction_dispatch_context(
    AArch64InstructionDispatchContextCallbacks<FunctionState> callbacks) {
    return AArch64CallbackInstructionDispatchContext<FunctionState>(
        std::move(callbacks));
}

struct AArch64AbiEmissionContextCallbacks {
    std::function<AArch64VirtualReg(AArch64MachineFunction &)>
        create_pointer_virtual_reg;
    std::function<const CoreIrType *()> create_fake_pointer_type;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, std::size_t,
                       AArch64MachineFunction &)>
        append_frame_address;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &, long long,
                       AArch64MachineFunction &)>
        add_constant_offset;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_vreg;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_memory_address;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        materialize_canonical_memory_address;
    std::function<bool(const CoreIrValue *, AArch64VirtualReg &)>
        require_canonical_vreg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, unsigned,
                       AArch64VirtualRegKind)>
        append_copy_from_physical_reg;
    std::function<void(AArch64MachineBlock &, unsigned, AArch64VirtualRegKind,
                       const AArch64VirtualReg &)>
        append_copy_to_physical_reg;
    std::function<void(AArch64MachineBlock &, const CoreIrType *,
                       const AArch64VirtualReg &, std::size_t,
                       AArch64MachineFunction &)>
        append_load_from_incoming_stack_arg;
    std::function<bool(AArch64MachineBlock &, const CoreIrType *,
                       const AArch64VirtualReg &, const AArch64VirtualReg &,
                       std::size_t, AArch64MachineFunction &)>
        append_load_from_address;
    std::function<bool(AArch64MachineBlock &, const CoreIrType *,
                       const AArch64VirtualReg &, const AArch64VirtualReg &,
                       std::size_t, AArch64MachineFunction &)>
        append_store_to_address;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &, std::size_t,
                       AArch64MachineFunction &)>
        materialize_incoming_stack_address;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *)>
        apply_truncate_to_virtual_reg;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const AArch64VirtualReg &, const CoreIrType *,
                       AArch64MachineFunction &)>
        emit_memory_copy;
    std::function<std::optional<AArch64VirtualReg>(AArch64MachineBlock &, std::size_t,
                                                   AArch64MachineFunction &)>
        prepare_stack_argument_area;
    std::function<void(AArch64MachineBlock &, std::size_t)>
        finish_stack_argument_area;
    std::function<void(AArch64MachineBlock &, const std::string &)> emit_direct_call;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &)>
        emit_indirect_call;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackAbiEmissionContext final : public AArch64AbiEmissionContext {
  private:
    AArch64AbiEmissionContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackAbiEmissionContext(
        AArch64AbiEmissionContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) override {
        return callbacks_.create_pointer_virtual_reg(function);
    }

    const CoreIrType *create_fake_pointer_type() const override {
        return callbacks_.create_fake_pointer_type();
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override {
        callbacks_.append_frame_address(machine_block, target_reg, offset, function);
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) override {
        return callbacks_.add_constant_offset(machine_block, base_reg, offset,
                                              function);
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_vreg(machine_block, value, out);
    }

    bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_memory_address(machine_block, value, out);
    }

    bool materialize_canonical_memory_address(AArch64MachineBlock &machine_block,
                                              const CoreIrValue *value,
                                              AArch64VirtualReg &out) override {
        return callbacks_.materialize_canonical_memory_address(machine_block, value,
                                                               out);
    }

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        return callbacks_.require_canonical_vreg(value, out);
    }

    void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &target_reg,
                                       unsigned physical_reg,
                                       AArch64VirtualRegKind reg_kind) override {
        callbacks_.append_copy_from_physical_reg(machine_block, target_reg,
                                                 physical_reg, reg_kind);
    }

    void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                     unsigned physical_reg,
                                     AArch64VirtualRegKind reg_kind,
                                     const AArch64VirtualReg &source_reg) override {
        callbacks_.append_copy_to_physical_reg(machine_block, physical_reg, reg_kind,
                                               source_reg);
    }

    void append_load_from_incoming_stack_arg(
        AArch64MachineBlock &machine_block, const CoreIrType *type,
        const AArch64VirtualReg &target_reg, std::size_t stack_offset,
        AArch64MachineFunction &function) override {
        callbacks_.append_load_from_incoming_stack_arg(
            machine_block, type, target_reg, stack_offset, function);
    }

    bool append_load_from_address(AArch64MachineBlock &machine_block,
                                  const CoreIrType *type,
                                  const AArch64VirtualReg &target_reg,
                                  const AArch64VirtualReg &address_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override {
        return callbacks_.append_load_from_address(machine_block, type, target_reg,
                                                   address_reg, offset, function);
    }

    bool append_store_to_address(AArch64MachineBlock &machine_block,
                                 const CoreIrType *type,
                                 const AArch64VirtualReg &source_reg,
                                 const AArch64VirtualReg &address_reg,
                                 std::size_t offset,
                                 AArch64MachineFunction &function) override {
        return callbacks_.append_store_to_address(machine_block, type, source_reg,
                                                  address_reg, offset, function);
    }

    bool materialize_incoming_stack_address(AArch64MachineBlock &machine_block,
                                            const AArch64VirtualReg &target_reg,
                                            std::size_t stack_offset,
                                            AArch64MachineFunction &function) override {
        return callbacks_.materialize_incoming_stack_address(machine_block,
                                                             target_reg,
                                                             stack_offset, function);
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override {
        callbacks_.apply_truncate_to_virtual_reg(machine_block, reg, type);
    }

    bool emit_memory_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &destination_address,
                          const AArch64VirtualReg &source_address,
                          const CoreIrType *value_type,
                          AArch64MachineFunction &function) override {
        return callbacks_.emit_memory_copy(machine_block, destination_address,
                                           source_address, value_type, function);
    }

    std::optional<AArch64VirtualReg> prepare_stack_argument_area(
        AArch64MachineBlock &machine_block, std::size_t stack_arg_bytes,
        AArch64MachineFunction &function) override {
        return callbacks_.prepare_stack_argument_area(machine_block, stack_arg_bytes,
                                                      function);
    }

    void finish_stack_argument_area(AArch64MachineBlock &machine_block,
                                    std::size_t stack_arg_bytes) override {
        callbacks_.finish_stack_argument_area(machine_block, stack_arg_bytes);
    }

    void emit_direct_call(AArch64MachineBlock &machine_block,
                          const std::string &callee_name) override {
        callbacks_.emit_direct_call(machine_block, callee_name);
    }

    bool emit_indirect_call(AArch64MachineBlock &machine_block,
                            const AArch64VirtualReg &callee_reg) override {
        return callbacks_.emit_indirect_call(machine_block, callee_reg);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto
make_aarch64_abi_emission_context(AArch64AbiEmissionContextCallbacks callbacks) {
    return AArch64CallbackAbiEmissionContext(std::move(callbacks));
}

struct AArch64AddressMaterializationContextCallbacks {
    std::function<AArch64VirtualReg(AArch64MachineFunction &)>
        create_pointer_virtual_reg;
    std::function<const CoreIrType *()> create_fake_pointer_type;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_vreg;
    std::function<bool(AArch64MachineBlock &, const CoreIrType *, std::uint64_t,
                       const AArch64VirtualReg &, AArch64MachineFunction &)>
        materialize_integer_constant;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &, long long,
                       AArch64MachineFunction &)>
        add_constant_offset;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *, const CoreIrType *)>
        apply_sign_extend_to_virtual_reg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *, const CoreIrType *)>
        apply_zero_extend_to_virtual_reg;
    std::function<void(const std::string &, AArch64SymbolKind)> record_symbol_reference;
    std::function<bool()> is_position_independent;
    std::function<bool(const std::string &)> is_nonpreemptible_global_symbol;
    std::function<bool(const std::string &)> is_nonpreemptible_function_symbol;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackAddressMaterializationContext final
    : public AArch64AddressMaterializationContext {
  private:
    AArch64AddressMaterializationContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackAddressMaterializationContext(
        AArch64AddressMaterializationContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) override {
        return callbacks_.create_pointer_virtual_reg(function);
    }

    const CoreIrType *create_fake_pointer_type() const override {
        return callbacks_.create_fake_pointer_type();
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_vreg(machine_block, value, out);
    }

    bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                      const CoreIrType *type, std::uint64_t value,
                                      const AArch64VirtualReg &target_reg,
                                      AArch64MachineFunction &function) override {
        return callbacks_.materialize_integer_constant(machine_block, type, value,
                                                       target_reg, function);
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) override {
        return callbacks_.add_constant_offset(machine_block, base_reg, offset,
                                              function);
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                    source_type, target_type);
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                    source_type, target_type);
    }

    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override {
        callbacks_.record_symbol_reference(name, kind);
    }

    bool is_position_independent() const override {
        return callbacks_.is_position_independent();
    }

    bool is_nonpreemptible_global_symbol(const std::string &name) const override {
        return callbacks_.is_nonpreemptible_global_symbol(name);
    }

    bool is_nonpreemptible_function_symbol(const std::string &name) const override {
        return callbacks_.is_nonpreemptible_function_symbol(name);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto make_aarch64_address_materialization_context(
    AArch64AddressMaterializationContextCallbacks callbacks) {
    return AArch64CallbackAddressMaterializationContext(std::move(callbacks));
}

struct AArch64ValueMaterializationContextCallbacks {
    AArch64AddressMaterializationContextCallbacks address_callbacks;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *)>
        apply_truncate_to_virtual_reg;
    std::function<void(AArch64MachineBlock &, unsigned, AArch64VirtualRegKind,
                       const AArch64VirtualReg &)>
        append_copy_to_physical_reg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, unsigned,
                       AArch64VirtualRegKind)>
        append_copy_from_physical_reg;
    std::function<void(AArch64MachineBlock &, const std::string &)> append_helper_call;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, std::size_t,
                       AArch64MachineFunction &)>
        append_frame_address;
    std::function<std::size_t(const CoreIrStackSlot *)> get_stack_slot_offset;
};

class AArch64CallbackValueMaterializationContext final
    : public AArch64ValueMaterializationContext {
  private:
    AArch64ValueMaterializationContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackValueMaterializationContext(
        AArch64ValueMaterializationContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) override {
        return callbacks_.address_callbacks.create_pointer_virtual_reg(function);
    }

    const CoreIrType *create_fake_pointer_type() const override {
        return callbacks_.address_callbacks.create_fake_pointer_type();
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.address_callbacks.ensure_value_in_vreg(machine_block, value,
                                                                 out);
    }

    bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                      const CoreIrType *type, std::uint64_t value,
                                      const AArch64VirtualReg &target_reg,
                                      AArch64MachineFunction &function) override {
        return callbacks_.address_callbacks.materialize_integer_constant(
            machine_block, type, value, target_reg, function);
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) override {
        return callbacks_.address_callbacks.add_constant_offset(machine_block, base_reg,
                                                                offset, function);
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.address_callbacks.apply_sign_extend_to_virtual_reg(
            machine_block, dst_reg, source_type, target_type);
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.address_callbacks.apply_zero_extend_to_virtual_reg(
            machine_block, dst_reg, source_type, target_type);
    }

    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override {
        callbacks_.address_callbacks.record_symbol_reference(name, kind);
    }

    bool is_position_independent() const override {
        return callbacks_.address_callbacks.is_position_independent();
    }

    bool is_nonpreemptible_global_symbol(const std::string &name) const override {
        return callbacks_.address_callbacks.is_nonpreemptible_global_symbol(name);
    }

    bool is_nonpreemptible_function_symbol(const std::string &name) const override {
        return callbacks_.address_callbacks.is_nonpreemptible_function_symbol(name);
    }

    void report_error(const std::string &message) override {
        callbacks_.address_callbacks.report_error(message);
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override {
        callbacks_.apply_truncate_to_virtual_reg(machine_block, reg, type);
    }

    void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                     unsigned physical_reg,
                                     AArch64VirtualRegKind reg_kind,
                                     const AArch64VirtualReg &source_reg) override {
        callbacks_.append_copy_to_physical_reg(machine_block, physical_reg, reg_kind,
                                               source_reg);
    }

    void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &target_reg,
                                       unsigned physical_reg,
                                       AArch64VirtualRegKind reg_kind) override {
        callbacks_.append_copy_from_physical_reg(machine_block, target_reg,
                                                 physical_reg, reg_kind);
    }

    void append_helper_call(AArch64MachineBlock &machine_block,
                            const std::string &symbol_name) override {
        callbacks_.append_helper_call(machine_block, symbol_name);
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override {
        callbacks_.append_frame_address(machine_block, target_reg, offset, function);
    }

    std::size_t
    get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const override {
        return callbacks_.get_stack_slot_offset(stack_slot);
    }
};

inline auto make_aarch64_value_materialization_context(
    AArch64ValueMaterializationContextCallbacks callbacks) {
    return AArch64CallbackValueMaterializationContext(std::move(callbacks));
}

struct AArch64MemoryValueLoweringContextCallbacks {
    std::function<AArch64VirtualReg(AArch64MachineFunction &)>
        create_pointer_virtual_reg;
    std::function<const CoreIrType *()> create_fake_pointer_type;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, std::size_t,
                       AArch64MachineFunction &)>
        append_frame_address;
    std::function<bool(AArch64MachineBlock &, const AArch64VirtualReg &, long long,
                       AArch64MachineFunction &)>
        add_constant_offset;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_vreg;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_memory_address;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        materialize_canonical_memory_address;
    std::function<bool(const CoreIrValue *, AArch64VirtualReg &)>
        require_canonical_vreg;
    std::function<std::size_t(const CoreIrStackSlot *)> get_stack_slot_offset;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackMemoryValueLoweringContext final
    : public AArch64MemoryValueLoweringContext {
  private:
    AArch64MemoryValueLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackMemoryValueLoweringContext(
        AArch64MemoryValueLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) override {
        return callbacks_.create_pointer_virtual_reg(function);
    }

    const CoreIrType *create_fake_pointer_type() const override {
        return callbacks_.create_fake_pointer_type();
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override {
        callbacks_.append_frame_address(machine_block, target_reg, offset, function);
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) override {
        return callbacks_.add_constant_offset(machine_block, base_reg, offset,
                                              function);
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_vreg(machine_block, value, out);
    }

    bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_memory_address(machine_block, value, out);
    }

    bool materialize_canonical_memory_address(AArch64MachineBlock &machine_block,
                                              const CoreIrValue *value,
                                              AArch64VirtualReg &out) override {
        return callbacks_.materialize_canonical_memory_address(machine_block, value,
                                                               out);
    }

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        return callbacks_.require_canonical_vreg(value, out);
    }

    std::size_t
    get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const override {
        return callbacks_.get_stack_slot_offset(stack_slot);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto make_aarch64_memory_value_lowering_context(
    AArch64MemoryValueLoweringContextCallbacks callbacks) {
    return AArch64CallbackMemoryValueLoweringContext(std::move(callbacks));
}

struct AArch64FloatHelperLoweringContextCallbacks {
    std::function<const CoreIrType *()> create_fake_pointer_type;
    std::function<void(AArch64MachineBlock &, unsigned, AArch64VirtualRegKind,
                       const AArch64VirtualReg &)>
        append_copy_to_physical_reg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &, unsigned,
                       AArch64VirtualRegKind)>
        append_copy_from_physical_reg;
    std::function<void(AArch64MachineBlock &, const std::string &)> append_helper_call;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *)>
        apply_truncate_to_virtual_reg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *, const CoreIrType *)>
        apply_sign_extend_to_virtual_reg;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const CoreIrType *, const CoreIrType *)>
        apply_zero_extend_to_virtual_reg;
    std::function<AArch64VirtualReg(AArch64MachineBlock &, const AArch64VirtualReg &,
                                    AArch64MachineFunction &)>
        promote_float16_to_float32;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const AArch64VirtualReg &)>
        demote_float32_to_float16;
    std::function<bool(AArch64MachineBlock &, const CoreIrConstantFloat &,
                       const AArch64VirtualReg &, AArch64MachineFunction &)>
        materialize_float_constant;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackFloatHelperLoweringContext final
    : public AArch64FloatHelperLoweringContext {
  private:
    AArch64FloatHelperLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackFloatHelperLoweringContext(
        AArch64FloatHelperLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    const CoreIrType *create_fake_pointer_type() const override {
        return callbacks_.create_fake_pointer_type();
    }

    void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                     unsigned physical_reg,
                                     AArch64VirtualRegKind reg_kind,
                                     const AArch64VirtualReg &source_reg) override {
        callbacks_.append_copy_to_physical_reg(machine_block, physical_reg, reg_kind,
                                               source_reg);
    }

    void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &target_reg,
                                       unsigned physical_reg,
                                       AArch64VirtualRegKind reg_kind) override {
        callbacks_.append_copy_from_physical_reg(machine_block, target_reg,
                                                 physical_reg, reg_kind);
    }

    void append_helper_call(AArch64MachineBlock &machine_block,
                            const std::string &symbol_name) override {
        callbacks_.append_helper_call(machine_block, symbol_name);
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override {
        callbacks_.apply_truncate_to_virtual_reg(machine_block, reg, type);
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                    source_type, target_type);
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) override {
        callbacks_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                    source_type, target_type);
    }

    AArch64VirtualReg promote_float16_to_float32(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &source_reg,
        AArch64MachineFunction &function) override {
        return callbacks_.promote_float16_to_float32(machine_block, source_reg,
                                                     function);
    }

    void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &source_reg,
                                   const AArch64VirtualReg &target_reg) override {
        callbacks_.demote_float32_to_float16(machine_block, source_reg, target_reg);
    }

    bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                    const CoreIrConstantFloat &constant,
                                    const AArch64VirtualReg &target_reg,
                                    AArch64MachineFunction &function) override {
        return callbacks_.materialize_float_constant(machine_block, constant,
                                                     target_reg, function);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto make_aarch64_float_helper_lowering_context(
    AArch64FloatHelperLoweringContextCallbacks callbacks) {
    return AArch64CallbackFloatHelperLoweringContext(std::move(callbacks));
}

struct AArch64GlobalDataLoweringContextCallbacks {
    std::function<void(const std::string &, AArch64SymbolKind, AArch64SectionKind,
                       bool)>
        record_symbol_definition;
    std::function<void(const std::string &, AArch64SymbolKind)>
        record_symbol_reference;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackGlobalDataLoweringContext final
    : public AArch64GlobalDataLoweringContext {
  private:
    AArch64GlobalDataLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackGlobalDataLoweringContext(
        AArch64GlobalDataLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    void record_symbol_definition(const std::string &name, AArch64SymbolKind kind,
                                  AArch64SectionKind section_kind,
                                  bool is_global_symbol) override {
        callbacks_.record_symbol_definition(name, kind, section_kind,
                                            is_global_symbol);
    }

    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override {
        callbacks_.record_symbol_reference(name, kind);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto make_aarch64_global_data_lowering_context(
    AArch64GlobalDataLoweringContextCallbacks callbacks) {
    return AArch64CallbackGlobalDataLoweringContext(std::move(callbacks));
}

struct AArch64MemoryInstructionLoweringContextCallbacks {
    std::function<bool(const CoreIrStackSlot *)> is_promoted_stack_slot;
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_vreg;
    std::function<bool(const CoreIrValue *, AArch64VirtualReg &)>
        require_canonical_vreg;
    std::function<std::optional<AArch64VirtualReg>(const CoreIrStackSlot *)>
        get_promoted_stack_slot_value;
    std::function<void(const CoreIrStackSlot *, const AArch64VirtualReg &)>
        set_promoted_stack_slot_value;
    std::function<void(AArch64MachineBlock &, const AArch64VirtualReg &,
                       const AArch64VirtualReg &)>
        append_register_copy;
    std::function<void(const std::string &)> report_error;
};

class AArch64CallbackMemoryInstructionLoweringContext final
    : public AArch64MemoryInstructionLoweringContext {
  private:
    AArch64MemoryInstructionLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackMemoryInstructionLoweringContext(
        AArch64MemoryInstructionLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    bool is_promoted_stack_slot(
        const CoreIrStackSlot *stack_slot) const override {
        return callbacks_.is_promoted_stack_slot(stack_slot);
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_vreg(machine_block, value, out);
    }

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        return callbacks_.require_canonical_vreg(value, out);
    }

    std::optional<AArch64VirtualReg>
    get_promoted_stack_slot_value(const CoreIrStackSlot *stack_slot) const override {
        return callbacks_.get_promoted_stack_slot_value(stack_slot);
    }

    void set_promoted_stack_slot_value(
        const CoreIrStackSlot *stack_slot,
        const AArch64VirtualReg &value_reg) override {
        callbacks_.set_promoted_stack_slot_value(stack_slot, value_reg);
    }

    void append_register_copy(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              const AArch64VirtualReg &source_reg) override {
        callbacks_.append_register_copy(machine_block, target_reg, source_reg);
    }

    void report_error(const std::string &message) override {
        callbacks_.report_error(message);
    }
};

inline auto make_aarch64_memory_instruction_lowering_context(
    AArch64MemoryInstructionLoweringContextCallbacks callbacks) {
    return AArch64CallbackMemoryInstructionLoweringContext(std::move(callbacks));
}

struct AArch64ScalarLoweringContextCallbacks {
    std::function<bool(AArch64MachineBlock &, const CoreIrValue *, AArch64VirtualReg &)>
        ensure_value_in_vreg;
    std::function<bool(const CoreIrValue *, AArch64VirtualReg &)>
        require_canonical_vreg;
    std::function<bool(AArch64MachineBlock &, CoreIrBinaryOpcode,
                       const AArch64VirtualReg &, const AArch64VirtualReg &,
                       const AArch64VirtualReg &)>
        emit_float128_binary_helper;
    std::function<bool(AArch64MachineBlock &, CoreIrComparePredicate,
                       const AArch64VirtualReg &, const AArch64VirtualReg &,
                       const AArch64VirtualReg &, AArch64MachineFunction &)>
        emit_float128_compare_helper;
    std::function<bool(AArch64MachineBlock &, const CoreIrCastInst &,
                       const AArch64VirtualReg &, const AArch64VirtualReg &,
                       AArch64MachineFunction &)>
        emit_float128_cast_helper;
    std::function<bool(AArch64MachineBlock &, const CoreIrConstantFloat &,
                       const AArch64VirtualReg &, AArch64MachineFunction &)>
        materialize_float_constant;
    std::function<DiagnosticEngine &()> diagnostic_engine;
};

class AArch64CallbackScalarLoweringContext final
    : public AArch64ScalarLoweringContext {
  private:
    AArch64ScalarLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackScalarLoweringContext(
        AArch64ScalarLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override {
        return callbacks_.ensure_value_in_vreg(machine_block, value, out);
    }

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        return callbacks_.require_canonical_vreg(value, out);
    }

    bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                     CoreIrBinaryOpcode opcode,
                                     const AArch64VirtualReg &lhs_reg,
                                     const AArch64VirtualReg &rhs_reg,
                                     const AArch64VirtualReg &dst_reg) override {
        return callbacks_.emit_float128_binary_helper(machine_block, opcode, lhs_reg,
                                                      rhs_reg, dst_reg);
    }

    bool emit_float128_compare_helper(
        AArch64MachineBlock &machine_block, CoreIrComparePredicate predicate,
        const AArch64VirtualReg &lhs_reg, const AArch64VirtualReg &rhs_reg,
        const AArch64VirtualReg &dst_reg,
        AArch64MachineFunction &function) override {
        return callbacks_.emit_float128_compare_helper(
            machine_block, predicate, lhs_reg, rhs_reg, dst_reg, function);
    }

    bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                                   const CoreIrCastInst &cast,
                                   const AArch64VirtualReg &operand_reg,
                                   const AArch64VirtualReg &dst_reg,
                                   AArch64MachineFunction &function) override {
        return callbacks_.emit_float128_cast_helper(machine_block, cast,
                                                    operand_reg, dst_reg,
                                                    function);
    }

    bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                    const CoreIrConstantFloat &constant,
                                    const AArch64VirtualReg &target_reg,
                                    AArch64MachineFunction &function) override {
        return callbacks_.materialize_float_constant(machine_block, constant,
                                                     target_reg, function);
    }

    DiagnosticEngine &diagnostic_engine() const override {
        return callbacks_.diagnostic_engine();
    }
};

inline auto make_aarch64_scalar_lowering_context(
    AArch64ScalarLoweringContextCallbacks callbacks) {
    return AArch64CallbackScalarLoweringContext(std::move(callbacks));
}

struct AArch64CallReturnLoweringContextCallbacks {
    std::function<AArch64FunctionAbiInfo(const CoreIrCallInst &)> classify_call;
    std::function<const std::vector<std::size_t> *(const CoreIrCallInst &)>
        lookup_indirect_call_copy_offsets;
    std::function<const AArch64FunctionAbiInfo &()> function_abi_info;
    std::function<const AArch64VirtualReg &()> indirect_result_address;
};

class AArch64CallbackCallReturnLoweringContext final
    : public AArch64CallReturnLoweringContext {
  private:
    AArch64CallReturnLoweringContextCallbacks callbacks_;

  public:
    explicit AArch64CallbackCallReturnLoweringContext(
        AArch64CallReturnLoweringContextCallbacks callbacks)
        : callbacks_(std::move(callbacks)) {}

    AArch64FunctionAbiInfo classify_call(const CoreIrCallInst &call) const override {
        return callbacks_.classify_call(call);
    }

    const std::vector<std::size_t> *lookup_indirect_call_copy_offsets(
        const CoreIrCallInst &call) const override {
        return callbacks_.lookup_indirect_call_copy_offsets(call);
    }

    const AArch64FunctionAbiInfo &function_abi_info() const override {
        return callbacks_.function_abi_info();
    }

    const AArch64VirtualReg &indirect_result_address() const override {
        return callbacks_.indirect_result_address();
    }
};

inline auto make_aarch64_call_return_lowering_context(
    AArch64CallReturnLoweringContextCallbacks callbacks) {
    return AArch64CallbackCallReturnLoweringContext(std::move(callbacks));
}

} // namespace sysycc
