#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_lambda_context_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_lowering_context_adapter_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

template <typename Session, typename FunctionState>
class AArch64LoweringContextFactory {
  private:
    Session &session_;
    const FunctionState *const_state_ = nullptr;
    FunctionState *mutable_state_ = nullptr;

    const FunctionState &state() const { return *const_state_; }

    FunctionState &mutable_state() const { return *mutable_state_; }

    void report_error(const std::string &message) const {
        session_.diagnostic_engine_.add_error(DiagnosticStage::Compiler, message);
    }

  public:
    explicit AArch64LoweringContextFactory(Session &session) : session_(session) {}

    AArch64LoweringContextFactory(Session &session, const FunctionState &state)
        : session_(session), const_state_(&state) {}

    AArch64LoweringContextFactory(Session &session, FunctionState &state)
        : session_(session), const_state_(&state), mutable_state_(&state) {}

    auto make_function_planning_context() {
        Session *session = &session_;
        return make_aarch64_function_planning_context(
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            },
            [session](AArch64MachineFunction &function, const CoreIrType *type) {
                return session->create_virtual_reg(function, type);
            },
            [session](AArch64MachineFunction &function) {
                return session->create_pointer_virtual_reg(function);
            },
            [session](const CoreIrCallInst &call) {
                return session->abi_lowering_pass_.classify_call(call);
            });
    }

    auto make_phi_plan_context() {
        Session *session = &session_;
        return make_aarch64_phi_plan_context(
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            },
            [session](const CoreIrBasicBlock *block) -> const std::string & {
                return session->block_labels_.at(block);
            });
    }

    auto make_phi_copy_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        return make_aarch64_phi_copy_lowering_context(
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            },
            [session, const_state](const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->require_canonical_vreg(*const_state, value, out);
            },
            [session, const_state](const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                if (const AArch64ValueLocation *location =
                        session->lookup_value_location(*const_state, value);
                    location != nullptr &&
                    location->kind == AArch64ValueLocationKind::VirtualReg &&
                    location->virtual_reg.is_valid()) {
                    out = location->virtual_reg;
                    return true;
                }
                return false;
            },
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   const AArch64VirtualReg &target_reg) {
                return session->materialize_value(machine_block, value,
                                                  target_reg, *const_state);
            });
    }

    auto make_instruction_dispatch_context() {
        Session *session = &session_;
        AArch64InstructionDispatchContextCallbacks<FunctionState> callbacks;
        callbacks.emit_debug_location =
            [session](AArch64MachineBlock &machine_block,
                      const SourceSpan &source_span, FunctionState &state) {
                session->emit_debug_location(machine_block, source_span, state);
            };
        callbacks.resolve_branch_target_label =
            [session](const FunctionState &state,
                      const CoreIrBasicBlock *predecessor,
                      const CoreIrBasicBlock *successor) {
                return session->resolve_branch_target_label(state, predecessor,
                                                            successor);
            };
        callbacks.emit_load =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrLoadInst &load, FunctionState &state) {
                return session->emit_load(machine_block, load, state);
            };
        callbacks.emit_store =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrStoreInst &store, FunctionState &state) {
                return session->emit_store(machine_block, store, state);
            };
        callbacks.emit_binary =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrBinaryInst &binary,
                      const FunctionState &state) {
                return session->emit_binary(machine_block, binary, state);
            };
        callbacks.emit_unary =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrUnaryInst &unary,
                      const FunctionState &state) {
                return session->emit_unary(machine_block, unary, state);
            };
        callbacks.emit_compare =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
                return session->emit_compare(machine_block, compare, state);
            };
        callbacks.emit_cast =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrCastInst &cast,
                      const FunctionState &state) {
                return session->emit_cast(machine_block, cast, state);
            };
        callbacks.emit_call =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrCallInst &call,
                      const FunctionState &state) {
                return session->emit_call(machine_block, call, state);
            };
        callbacks.emit_cond_jump =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrCondJumpInst &cond_jump,
                      const FunctionState &state,
                      const CoreIrBasicBlock *current_block) {
                return session->emit_cond_jump(machine_block, cond_jump, state,
                                               current_block);
            };
        callbacks.emit_return =
            [session](AArch64MachineFunction &machine_function,
                      AArch64MachineBlock &machine_block,
                      const CoreIrReturnInst &return_inst,
                      const FunctionState &state) {
                return session->emit_return(machine_function, machine_block,
                                            return_inst, state);
            };
        callbacks.emit_address_of_stack_slot =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
                      const FunctionState &state) {
                return session->emit_address_of_stack_slot(machine_block,
                                                           address_of_stack_slot,
                                                           state);
            };
        callbacks.emit_address_of_global =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrAddressOfGlobalInst &address_of_global,
                      const FunctionState &state) {
                return session->emit_address_of_global(machine_block,
                                                       address_of_global, state);
            };
        callbacks.emit_address_of_function =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrAddressOfFunctionInst &address_of_function,
                      const FunctionState &state) {
                return session->emit_address_of_function(machine_block,
                                                         address_of_function,
                                                         state);
            };
        callbacks.emit_getelementptr =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrGetElementPtrInst &gep,
                      const FunctionState &state) {
                return session->emit_getelementptr(machine_block, gep, state);
            };
        return make_aarch64_instruction_dispatch_context(std::move(callbacks));
    }

    auto make_abi_emission_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64AbiEmissionContextCallbacks callbacks;
        callbacks.create_pointer_virtual_reg =
            [session](AArch64MachineFunction &function) {
                return session->create_pointer_virtual_reg(function);
            };
        callbacks.create_fake_pointer_type =
            [session]() { return session->create_fake_pointer_type(); };
        callbacks.append_frame_address =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &target_reg, std::size_t offset,
                      AArch64MachineFunction &function) {
                session->append_frame_address(machine_block, target_reg, offset,
                                              function);
            };
        callbacks.add_constant_offset =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &base_reg, long long offset,
                      AArch64MachineFunction &function) {
                return session->add_constant_offset(machine_block, base_reg,
                                                    offset, function);
            };
        callbacks.ensure_value_in_vreg =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_vreg(machine_block, value,
                                                     *const_state, out);
            };
        callbacks.ensure_value_in_memory_address =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_memory_address(machine_block,
                                                               value,
                                                               *const_state, out);
            };
        callbacks.materialize_canonical_memory_address =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->materialize_canonical_memory_address(
                    machine_block, *const_state, value, out);
            };
        callbacks.require_canonical_vreg =
            [session, const_state](const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->require_canonical_vreg(*const_state, value, out);
            };
        callbacks.append_copy_from_physical_reg =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &target_reg, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind) {
                ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                        physical_reg, reg_kind);
            };
        callbacks.append_copy_to_physical_reg =
            [](AArch64MachineBlock &machine_block, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind,
               const AArch64VirtualReg &source_reg) {
                ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                                      reg_kind, source_reg);
            };
        callbacks.append_load_from_incoming_stack_arg =
            [session](AArch64MachineBlock &machine_block, const CoreIrType *type,
                      const AArch64VirtualReg &target_reg,
                      std::size_t stack_offset,
                      AArch64MachineFunction &function) {
                sysycc::append_load_from_incoming_stack_arg(
                    machine_block, *session, type, target_reg, stack_offset,
                    function);
            };
        callbacks.append_load_from_address =
            [session](AArch64MachineBlock &machine_block, const CoreIrType *type,
                      const AArch64VirtualReg &target_reg,
                      const AArch64VirtualReg &address_reg,
                      std::size_t offset, AArch64MachineFunction &function) {
                return sysycc::append_load_from_address(
                    machine_block, *session, type, target_reg, address_reg,
                    offset, function);
            };
        callbacks.append_store_to_address =
            [session](AArch64MachineBlock &machine_block, const CoreIrType *type,
                      const AArch64VirtualReg &source_reg,
                      const AArch64VirtualReg &address_reg,
                      std::size_t offset, AArch64MachineFunction &function) {
                return sysycc::append_store_to_address(
                    machine_block, *session, type, source_reg, address_reg,
                    offset, function);
            };
        callbacks.materialize_incoming_stack_address =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &target_reg,
                      std::size_t stack_offset,
                      AArch64MachineFunction &function) {
                machine_block.append_instruction("mov " + def_vreg(target_reg) +
                                                 ", x29");
                return session->add_constant_offset(
                    machine_block, target_reg,
                    static_cast<long long>(stack_offset), function);
            };
        callbacks.apply_truncate_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &reg, const CoreIrType *type) {
                session->apply_truncate_to_virtual_reg(machine_block, reg, type);
            };
        callbacks.emit_memory_copy =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &destination_address,
                      const AArch64VirtualReg &source_address,
                      const CoreIrType *value_type,
                      AArch64MachineFunction &function) {
                return session->emit_memory_copy(machine_block,
                                                 destination_address,
                                                 source_address, value_type,
                                                 function);
            };
        callbacks.prepare_stack_argument_area =
            [session](AArch64MachineBlock &machine_block,
                      std::size_t stack_arg_bytes,
                      AArch64MachineFunction &function)
            -> std::optional<AArch64VirtualReg> {
                if (stack_arg_bytes == 0) {
                    return std::nullopt;
                }
                machine_block.append_instruction("sub sp, sp, #" +
                                                 std::to_string(stack_arg_bytes));
                AArch64VirtualReg stack_base =
                    session->create_pointer_virtual_reg(function);
                machine_block.append_instruction("mov " + def_vreg(stack_base) +
                                                 ", sp");
                return stack_base;
            };
        callbacks.finish_stack_argument_area =
            [](AArch64MachineBlock &machine_block, std::size_t stack_arg_bytes) {
                if (stack_arg_bytes > 0) {
                    machine_block.append_instruction("add sp, sp, #" +
                                                     std::to_string(stack_arg_bytes));
                }
            };
        callbacks.emit_direct_call =
            [session](AArch64MachineBlock &machine_block,
                      const std::string &callee_name) {
                session->record_symbol_reference(callee_name,
                                                 AArch64SymbolKind::Function);
                machine_block.append_instruction(AArch64MachineInstr(
                    "bl", {AArch64MachineOperand(callee_name)},
                    AArch64InstructionFlags{.is_call = true}, {}, {},
                    make_default_aarch64_call_clobber_mask()));
            };
        callbacks.emit_indirect_call =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &callee_reg) {
                machine_block.append_instruction(AArch64MachineInstr(
                    "blr", {AArch64MachineOperand(use_vreg(callee_reg))},
                    AArch64InstructionFlags{.is_call = true}, {}, {},
                    make_default_aarch64_call_clobber_mask()));
                return true;
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return make_aarch64_abi_emission_context(std::move(callbacks));
    }

    auto make_address_materialization_callbacks() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64AddressMaterializationContextCallbacks callbacks;
        callbacks.create_pointer_virtual_reg =
            [session](AArch64MachineFunction &function) {
                return session->create_pointer_virtual_reg(function);
            };
        callbacks.create_fake_pointer_type =
            [session]() { return session->create_fake_pointer_type(); };
        callbacks.ensure_value_in_vreg =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_vreg(machine_block, value,
                                                     *const_state, out);
            };
        callbacks.materialize_integer_constant =
            [session](AArch64MachineBlock &machine_block, const CoreIrType *type,
                      std::uint64_t value, const AArch64VirtualReg &target_reg,
                      AArch64MachineFunction &) {
                return sysycc::materialize_integer_constant(
                    machine_block, *session, type, value, target_reg);
            };
        callbacks.add_constant_offset =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &base_reg, long long offset,
                      AArch64MachineFunction &function) {
                return session->add_constant_offset(machine_block, base_reg,
                                                    offset, function);
            };
        callbacks.apply_sign_extend_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &dst_reg,
                      const CoreIrType *source_type,
                      const CoreIrType *target_type) {
                session->apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                          source_type, target_type);
            };
        callbacks.apply_zero_extend_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &dst_reg,
                      const CoreIrType *source_type,
                      const CoreIrType *target_type) {
                session->apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                          source_type, target_type);
            };
        callbacks.record_symbol_reference =
            [session](const std::string &name, AArch64SymbolKind kind) {
                session->record_symbol_reference(name, kind);
            };
        callbacks.is_position_independent = [session]() {
            return session->backend_options_.get_position_independent();
        };
        callbacks.is_nonpreemptible_global_symbol =
            [session](const std::string &name) {
                return session->is_nonpreemptible_global_symbol(name);
            };
        callbacks.is_nonpreemptible_function_symbol =
            [session](const std::string &name) {
                return session->is_nonpreemptible_function_symbol(name);
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return callbacks;
    }

    auto make_address_materialization_context() {
        return make_aarch64_address_materialization_context(
            make_address_materialization_callbacks());
    }

    auto make_value_materialization_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64ValueMaterializationContextCallbacks callbacks;
        callbacks.address_callbacks = make_address_materialization_callbacks();
        callbacks.apply_truncate_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &reg, const CoreIrType *type) {
                session->apply_truncate_to_virtual_reg(machine_block, reg, type);
            };
        callbacks.append_copy_to_physical_reg =
            [](AArch64MachineBlock &machine_block, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind,
               const AArch64VirtualReg &source_reg) {
                ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                                      reg_kind, source_reg);
            };
        callbacks.append_copy_from_physical_reg =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &target_reg, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind) {
                ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                        physical_reg, reg_kind);
            };
        callbacks.append_helper_call =
            [session](AArch64MachineBlock &machine_block,
                      const std::string &symbol_name) {
                session->append_helper_call(machine_block, symbol_name);
            };
        callbacks.append_frame_address =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &target_reg, std::size_t offset,
                      AArch64MachineFunction &function) {
                session->append_frame_address(machine_block, target_reg, offset,
                                              function);
            };
        callbacks.get_stack_slot_offset =
            [const_state](const CoreIrStackSlot *stack_slot) {
                return const_state->machine_function->get_frame_info()
                    .get_stack_slot_offset(stack_slot);
            };
        return make_aarch64_value_materialization_context(std::move(callbacks));
    }

    auto make_memory_value_lowering_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64MemoryValueLoweringContextCallbacks callbacks;
        callbacks.create_pointer_virtual_reg =
            [session](AArch64MachineFunction &function) {
                return session->create_pointer_virtual_reg(function);
            };
        callbacks.create_fake_pointer_type =
            [session]() { return session->create_fake_pointer_type(); };
        callbacks.append_frame_address =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &target_reg, std::size_t offset,
                      AArch64MachineFunction &function) {
                session->append_frame_address(machine_block, target_reg, offset,
                                              function);
            };
        callbacks.add_constant_offset =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &base_reg, long long offset,
                      AArch64MachineFunction &function) {
                return session->add_constant_offset(machine_block, base_reg,
                                                    offset, function);
            };
        callbacks.ensure_value_in_vreg =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_vreg(machine_block, value,
                                                     *const_state, out);
            };
        callbacks.ensure_value_in_memory_address =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_memory_address(machine_block,
                                                               value,
                                                               *const_state, out);
            };
        callbacks.materialize_canonical_memory_address =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->materialize_canonical_memory_address(
                    machine_block, *const_state, value, out);
            };
        callbacks.require_canonical_vreg =
            [session, const_state](const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->require_canonical_vreg(*const_state, value, out);
            };
        callbacks.get_stack_slot_offset =
            [const_state](const CoreIrStackSlot *stack_slot) {
                return const_state->machine_function->get_frame_info()
                    .get_stack_slot_offset(stack_slot);
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return make_aarch64_memory_value_lowering_context(std::move(callbacks));
    }

    auto make_float_helper_lowering_context() {
        Session *session = &session_;
        AArch64FloatHelperLoweringContextCallbacks callbacks;
        callbacks.create_fake_pointer_type =
            [session]() { return session->create_fake_pointer_type(); };
        callbacks.append_copy_to_physical_reg =
            [](AArch64MachineBlock &machine_block, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind,
               const AArch64VirtualReg &source_reg) {
                ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                                      reg_kind, source_reg);
            };
        callbacks.append_copy_from_physical_reg =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &target_reg, unsigned physical_reg,
               AArch64VirtualRegKind reg_kind) {
                ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                        physical_reg, reg_kind);
            };
        callbacks.append_helper_call =
            [session](AArch64MachineBlock &machine_block,
                      const std::string &symbol_name) {
                session->append_helper_call(machine_block, symbol_name);
            };
        callbacks.apply_truncate_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &reg, const CoreIrType *type) {
                session->apply_truncate_to_virtual_reg(machine_block, reg, type);
            };
        callbacks.apply_sign_extend_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &dst_reg,
                      const CoreIrType *source_type,
                      const CoreIrType *target_type) {
                session->apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                          source_type, target_type);
            };
        callbacks.apply_zero_extend_to_virtual_reg =
            [session](AArch64MachineBlock &machine_block,
                      const AArch64VirtualReg &dst_reg,
                      const CoreIrType *source_type,
                      const CoreIrType *target_type) {
                session->apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                          source_type, target_type);
            };
        callbacks.promote_float16_to_float32 =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &source_reg,
               AArch64MachineFunction &function) {
                return sysycc::promote_float16_to_float32(machine_block,
                                                          source_reg, function);
            };
        callbacks.demote_float32_to_float16 =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &source_reg,
               const AArch64VirtualReg &target_reg) {
                sysycc::demote_float32_to_float16(machine_block, source_reg,
                                                  target_reg);
            };
        callbacks.materialize_float_constant =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrConstantFloat &constant,
                      const AArch64VirtualReg &target_reg,
                      AArch64MachineFunction &function) {
                return sysycc::materialize_float_constant(
                    machine_block, *session, constant, target_reg, function);
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return make_aarch64_float_helper_lowering_context(std::move(callbacks));
    }

    AArch64CallbackGlobalDataLoweringContext make_global_data_lowering_context() {
        Session *session = &session_;
        AArch64GlobalDataLoweringContextCallbacks callbacks;
        callbacks.record_symbol_definition =
            [session](const std::string &name, AArch64SymbolKind kind,
                      AArch64SectionKind section_kind, bool is_global_symbol) {
                session->record_symbol_definition(name, kind, section_kind,
                                                  is_global_symbol);
            };
        callbacks.record_symbol_reference =
            [session](const std::string &name, AArch64SymbolKind kind) {
                session->record_symbol_reference(name, kind);
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return make_aarch64_global_data_lowering_context(std::move(callbacks));
    }

    auto make_memory_instruction_lowering_context() {
        Session *session = &session_;
        FunctionState *mutable_state = mutable_state_;
        AArch64MemoryInstructionLoweringContextCallbacks callbacks;
        callbacks.is_promoted_stack_slot =
            [session, mutable_state](const CoreIrStackSlot *stack_slot) {
                return session->is_promoted_stack_slot(*mutable_state, stack_slot);
            };
        callbacks.ensure_value_in_vreg =
            [session, mutable_state](AArch64MachineBlock &machine_block,
                                     const CoreIrValue *value,
                                     AArch64VirtualReg &out) {
                return session->ensure_value_in_vreg(machine_block, value,
                                                     *mutable_state, out);
            };
        callbacks.require_canonical_vreg =
            [session, mutable_state](const CoreIrValue *value,
                                     AArch64VirtualReg &out) {
                return session->require_canonical_vreg(*mutable_state, value, out);
            };
        callbacks.get_promoted_stack_slot_value =
            [mutable_state](const CoreIrStackSlot *stack_slot)
            -> std::optional<AArch64VirtualReg> {
                const auto it = mutable_state
                                    ->value_state.promoted_stack_slot_values.find(
                                        stack_slot);
                if (it ==
                    mutable_state->value_state.promoted_stack_slot_values.end()) {
                    return std::nullopt;
                }
                return it->second;
            };
        callbacks.set_promoted_stack_slot_value =
            [mutable_state](const CoreIrStackSlot *stack_slot,
                            const AArch64VirtualReg &value_reg) {
                mutable_state->value_state.promoted_stack_slot_values[stack_slot] =
                    value_reg;
            };
        callbacks.append_register_copy =
            [](AArch64MachineBlock &machine_block,
               const AArch64VirtualReg &target_reg,
               const AArch64VirtualReg &source_reg) {
                ::sysycc::append_register_copy(machine_block, target_reg,
                                               source_reg);
            };
        callbacks.report_error =
            [session](const std::string &message) {
                session->diagnostic_engine_.add_error(DiagnosticStage::Compiler,
                                                      message);
            };
        return make_aarch64_memory_instruction_lowering_context(std::move(callbacks));
    }

    auto make_scalar_lowering_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64ScalarLoweringContextCallbacks callbacks;
        callbacks.ensure_value_in_vreg =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->ensure_value_in_vreg(machine_block, value,
                                                     *const_state, out);
            };
        callbacks.require_canonical_vreg =
            [session, const_state](const CoreIrValue *value,
                                   AArch64VirtualReg &out) {
                return session->require_canonical_vreg(*const_state, value, out);
            };
        callbacks.emit_float128_binary_helper =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   CoreIrBinaryOpcode opcode,
                                   const AArch64VirtualReg &lhs_reg,
                                   const AArch64VirtualReg &rhs_reg,
                                   const AArch64VirtualReg &dst_reg) {
                AArch64LoweringContextFactory nested_factory(*session,
                                                            *const_state);
                auto float_context =
                    nested_factory.make_float_helper_lowering_context();
                return sysycc::emit_float128_binary_helper(
                    machine_block, float_context, opcode, lhs_reg, rhs_reg,
                    dst_reg);
            };
        callbacks.emit_float128_compare_helper =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   CoreIrComparePredicate predicate,
                                   const AArch64VirtualReg &lhs_reg,
                                   const AArch64VirtualReg &rhs_reg,
                                   const AArch64VirtualReg &dst_reg,
                                   AArch64MachineFunction &function) {
                AArch64LoweringContextFactory nested_factory(*session,
                                                            *const_state);
                auto float_context =
                    nested_factory.make_float_helper_lowering_context();
                return sysycc::emit_float128_compare_helper(
                    machine_block, float_context, predicate, lhs_reg, rhs_reg,
                    dst_reg, function);
            };
        callbacks.emit_float128_cast_helper =
            [session, const_state](AArch64MachineBlock &machine_block,
                                   const CoreIrCastInst &cast,
                                   const AArch64VirtualReg &operand_reg,
                                   const AArch64VirtualReg &dst_reg,
                                   AArch64MachineFunction &function) {
                AArch64LoweringContextFactory nested_factory(*session,
                                                            *const_state);
                auto float_context =
                    nested_factory.make_float_helper_lowering_context();
                return sysycc::emit_float128_cast_helper(
                    machine_block, float_context, cast, operand_reg, dst_reg,
                    function);
            };
        callbacks.materialize_float_constant =
            [session](AArch64MachineBlock &machine_block,
                      const CoreIrConstantFloat &constant,
                      const AArch64VirtualReg &target_reg,
                      AArch64MachineFunction &function) {
                return sysycc::materialize_float_constant(
                    machine_block, *session, constant, target_reg, function);
            };
        callbacks.diagnostic_engine =
            [session]() -> DiagnosticEngine & { return session->diagnostic_engine_; };
        return make_aarch64_scalar_lowering_context(std::move(callbacks));
    }

    auto make_call_return_lowering_context() {
        Session *session = &session_;
        const FunctionState *const_state = const_state_;
        AArch64CallReturnLoweringContextCallbacks callbacks;
        callbacks.classify_call = [session](const CoreIrCallInst &call) {
            return session->abi_lowering_pass_.classify_call(call);
        };
        callbacks.lookup_indirect_call_copy_offsets =
            [const_state](const CoreIrCallInst &call)
            -> const std::vector<std::size_t> * {
                const auto it = const_state
                                    ->call_state.indirect_call_argument_copy_offsets
                                    .find(&call);
                return it ==
                               const_state
                                   ->call_state.indirect_call_argument_copy_offsets
                                   .end()
                           ? nullptr
                           : &it->second;
            };
        callbacks.function_abi_info =
            [const_state]() -> const AArch64FunctionAbiInfo & {
                return const_state->call_state.abi_info;
            };
        callbacks.indirect_result_address =
            [const_state]() -> const AArch64VirtualReg & {
                return const_state->value_state.indirect_result_address;
            };
        return make_aarch64_call_return_lowering_context(std::move(callbacks));
    }
};

template <typename Session, typename FunctionState>
auto make_aarch64_lowering_context_factory(Session &session) {
    return AArch64LoweringContextFactory<Session, FunctionState>(session);
}

template <typename Session, typename FunctionState>
auto make_aarch64_lowering_context_factory(Session &session,
                                           const FunctionState &state) {
    return AArch64LoweringContextFactory<Session, FunctionState>(session, state);
}

template <typename Session, typename FunctionState>
auto make_aarch64_lowering_context_factory(Session &session,
                                           FunctionState &state) {
    return AArch64LoweringContextFactory<Session, FunctionState>(session, state);
}

} // namespace sysycc
