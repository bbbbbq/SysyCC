#include "backend/asm_gen/aarch64/support/aarch64_function_lowering_facade.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

void add_facade_error(DiagnosticEngine &diagnostic_engine,
                      const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

void append_stack_pointer_adjust(AArch64MachineBlock &machine_block,
                                 const char *mnemonic,
                                 std::size_t stack_arg_bytes) {
    constexpr std::size_t kMaxImmediate = 4095;
    std::size_t remaining = stack_arg_bytes;
    while (remaining > 0) {
        const std::size_t chunk = std::min(remaining, kMaxImmediate);
        machine_block.append_instruction(AArch64MachineInstr(
            mnemonic, {stack_pointer_operand(true),
                       stack_pointer_operand(true),
                       AArch64MachineOperand::immediate("#" + std::to_string(chunk))}));
        remaining -= chunk;
    }
}

} // namespace

AArch64GlobalDataLoweringFacade::AArch64GlobalDataLoweringFacade(
    AArch64LoweringFacadeServices &services)
    : services_(services) {}

void AArch64GlobalDataLoweringFacade::record_symbol_definition(
    const std::string &name, AArch64SymbolKind kind,
    AArch64SectionKind section_kind, bool is_global_symbol) {
    services_.record_symbol_definition(name, kind, section_kind,
                                       is_global_symbol);
}

void AArch64GlobalDataLoweringFacade::record_symbol_reference(
    const std::string &name, AArch64SymbolKind kind) {
    services_.record_symbol_reference(name, kind);
}

AArch64SymbolReference AArch64GlobalDataLoweringFacade::make_symbol_reference(
    const std::string &name, AArch64SymbolKind kind,
    AArch64SymbolBinding binding,
    std::optional<AArch64SectionKind> section_kind, long long addend,
    bool is_defined) const {
    return services_.make_symbol_reference(name, kind, binding, section_kind,
                                           addend, is_defined);
}

void AArch64GlobalDataLoweringFacade::report_error(const std::string &message) {
    add_facade_error(services_.diagnostic_engine(), message);
}

AArch64FunctionPlanningFacade::AArch64FunctionPlanningFacade(
    AArch64LoweringFacadeServices &services)
    : services_(services) {}

void AArch64FunctionPlanningFacade::report_error(const std::string &message) {
    add_facade_error(services_.diagnostic_engine(), message);
}

AArch64VirtualReg AArch64FunctionPlanningFacade::create_virtual_reg(
    AArch64MachineFunction &function, const CoreIrType *type) {
    return services_.create_virtual_reg(function, type);
}

AArch64VirtualReg AArch64FunctionPlanningFacade::create_pointer_virtual_reg(
    AArch64MachineFunction &function) {
    return services_.create_pointer_virtual_reg(function);
}

AArch64FunctionAbiInfo
AArch64FunctionPlanningFacade::classify_call(const CoreIrCallInst &call) const {
    return services_.classify_call(call);
}

AArch64FunctionLoweringFacade::AArch64FunctionLoweringFacade(
    AArch64LoweringFacadeServices &services, FunctionState &state)
    : services_(services), state_(state) {}

void AArch64FunctionLoweringFacade::report_error(const std::string &message) {
    add_facade_error(services_.diagnostic_engine(), message);
}

void AArch64FunctionLoweringFacade::report_error(
    const std::string &message) const {
    add_facade_error(services_.diagnostic_engine(), message);
}

AArch64VirtualReg AArch64FunctionLoweringFacade::create_virtual_reg(
    AArch64MachineFunction &function, const CoreIrType *type) {
    return services_.create_virtual_reg(function, type);
}

AArch64VirtualReg AArch64FunctionLoweringFacade::create_pointer_virtual_reg(
    AArch64MachineFunction &function) {
    return services_.create_pointer_virtual_reg(function);
}

const CoreIrType *
AArch64FunctionLoweringFacade::create_fake_pointer_type() const {
    return static_cast<const AArch64MemoryAccessContext &>(services_)
        .create_fake_pointer_type();
}

bool AArch64FunctionLoweringFacade::materialize_integer_constant(
    AArch64MachineBlock &machine_block, const CoreIrType *type,
    std::uint64_t value, const AArch64VirtualReg &target_reg,
    AArch64MachineFunction &function) {
    (void)function;
    return sysycc::materialize_integer_constant(machine_block, services_, type,
                                                value, target_reg);
}

AArch64FunctionAbiInfo
AArch64FunctionLoweringFacade::classify_call(const CoreIrCallInst &call) const {
    return services_.classify_call(call);
}

const std::string &AArch64FunctionLoweringFacade::block_label(
    const CoreIrBasicBlock *block) const {
    return services_.block_label(block);
}

bool AArch64FunctionLoweringFacade::require_canonical_vreg(
    const CoreIrValue *value, AArch64VirtualReg &out) const {
    return services_.require_canonical_vreg(state_, value, out);
}

bool AArch64FunctionLoweringFacade::try_get_value_vreg(
    const CoreIrValue *value, AArch64VirtualReg &out) const {
    const AArch64ValueLocation *location =
        services_.lookup_value_location(state_, value);
    if (location == nullptr ||
        location->kind != AArch64ValueLocationKind::VirtualReg ||
        !location->virtual_reg.is_valid()) {
        return false;
    }
    out = location->virtual_reg;
    return true;
}

bool AArch64FunctionLoweringFacade::materialize_value(
    AArch64MachineBlock &machine_block, const CoreIrValue *value,
    const AArch64VirtualReg &target_reg) {
    return services_.materialize_value(machine_block, value, target_reg, state_,
                                       *this);
}

void AArch64FunctionLoweringFacade::append_frame_address(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
    std::size_t offset, AArch64MachineFunction &function) {
    services_.append_frame_address(machine_block, target_reg, offset, function);
}

bool AArch64FunctionLoweringFacade::add_constant_offset(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &base_reg,
    long long offset, AArch64MachineFunction &function) {
    return services_.add_constant_offset(machine_block, base_reg, offset,
                                         function);
}

void AArch64FunctionLoweringFacade::record_symbol_reference(
    const std::string &name, AArch64SymbolKind kind) {
    services_.record_symbol_reference(name, kind);
}

AArch64SymbolReference AArch64FunctionLoweringFacade::make_symbol_reference(
    const std::string &name, AArch64SymbolKind kind,
    AArch64SymbolBinding binding,
    std::optional<AArch64SectionKind> section_kind, long long addend,
    bool is_defined) const {
    return services_.make_symbol_reference(name, kind, binding, section_kind,
                                           addend, is_defined);
}

bool AArch64FunctionLoweringFacade::is_position_independent() const {
    return services_.is_position_independent();
}

bool AArch64FunctionLoweringFacade::is_nonpreemptible_global_symbol(
    const std::string &name) const {
    return services_.is_nonpreemptible_global_symbol(name);
}

bool AArch64FunctionLoweringFacade::is_nonpreemptible_function_symbol(
    const std::string &name) const {
    return services_.is_nonpreemptible_function_symbol(name);
}

bool AArch64FunctionLoweringFacade::ensure_value_in_vreg(
    AArch64MachineBlock &machine_block, const CoreIrValue *value,
    AArch64VirtualReg &out) {
    if (value == nullptr) {
        report_error("encountered null Core IR value during AArch64 lowering");
        return false;
    }
    if (try_get_value_vreg(value, out)) {
        return true;
    }
    out = services_.create_virtual_reg(*state_.machine_function,
                                       value->get_type());
    return services_.materialize_value(machine_block, value, out, state_,
                                       *this);
}

bool AArch64FunctionLoweringFacade::ensure_value_in_memory_address(
    AArch64MachineBlock &machine_block, const CoreIrValue *value,
    AArch64VirtualReg &out) {
    const AArch64ValueLocation *location =
        services_.lookup_value_location(state_, value);
    if (location == nullptr ||
        location->kind != AArch64ValueLocationKind::MemoryAddress ||
        !location->virtual_reg.is_valid()) {
        report_error(
            "missing canonical AArch64 aggregate memory location for Core IR "
            "value");
        return false;
    }
    return materialize_canonical_memory_address(machine_block, value, out);
}

bool AArch64FunctionLoweringFacade::materialize_canonical_memory_address(
    AArch64MachineBlock &machine_block, const CoreIrValue *value,
    AArch64VirtualReg &out) {
    std::size_t offset = 0;
    if (!services_.require_canonical_memory_address(state_, value, out,
                                                    offset)) {
        return false;
    }
    services_.append_frame_address(machine_block, out, offset,
                                   *state_.machine_function);
    return true;
}

std::size_t AArch64FunctionLoweringFacade::get_stack_slot_offset(
    const CoreIrStackSlot *stack_slot) const {
    return state_.machine_function->get_frame_info().get_stack_slot_offset(
        stack_slot);
}

void AArch64FunctionLoweringFacade::append_copy_from_physical_reg(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
    unsigned physical_reg, AArch64VirtualRegKind reg_kind) {
    services_.append_copy_from_physical_reg(machine_block, target_reg,
                                            physical_reg, reg_kind);
}

void AArch64FunctionLoweringFacade::append_copy_to_physical_reg(
    AArch64MachineBlock &machine_block, unsigned physical_reg,
    AArch64VirtualRegKind reg_kind, const AArch64VirtualReg &source_reg) {
    services_.append_copy_to_physical_reg(machine_block, physical_reg, reg_kind,
                                          source_reg);
}

void AArch64FunctionLoweringFacade::append_load_from_incoming_stack_arg(
    AArch64MachineBlock &machine_block, const CoreIrType *type,
    const AArch64VirtualReg &target_reg, std::size_t offset,
    AArch64MachineFunction &function) {
    services_.append_load_from_incoming_stack_arg(machine_block, type,
                                                  target_reg, offset, function);
}

bool AArch64FunctionLoweringFacade::append_load_from_address(
    AArch64MachineBlock &machine_block, const CoreIrType *type,
    const AArch64VirtualReg &target_reg, const AArch64VirtualReg &address_reg,
    std::size_t offset, AArch64MachineFunction &function) {
    return services_.append_load_from_address(machine_block, type, target_reg,
                                              address_reg, offset, function);
}

bool AArch64FunctionLoweringFacade::append_store_to_address(
    AArch64MachineBlock &machine_block, const CoreIrType *type,
    const AArch64VirtualReg &source_reg, const AArch64VirtualReg &address_reg,
    std::size_t offset, AArch64MachineFunction &function) {
    return services_.append_store_to_address(machine_block, type, source_reg,
                                             address_reg, offset, function);
}

bool AArch64FunctionLoweringFacade::materialize_incoming_stack_address(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
    std::size_t stack_offset, AArch64MachineFunction &function) {
    if (stack_offset <= 4095) {
        machine_block.append_instruction(AArch64MachineInstr(
            "add", {def_vreg_operand(target_reg),
                    AArch64MachineOperand::physical_reg(
                        static_cast<unsigned>(AArch64PhysicalReg::X29),
                        AArch64VirtualRegKind::General64),
                    AArch64MachineOperand::immediate("#" +
                                                     std::to_string(stack_offset))}));
        return true;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand(target_reg),
                AArch64MachineOperand::physical_reg(
                    static_cast<unsigned>(AArch64PhysicalReg::X29),
                    AArch64VirtualRegKind::General64)}));
    return services_.add_constant_offset(machine_block, target_reg,
                                         static_cast<long long>(stack_offset),
                                         function);
}

void AArch64FunctionLoweringFacade::apply_truncate_to_virtual_reg(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &reg,
    const CoreIrType *type) {
    sysycc::apply_truncate_to_virtual_reg(machine_block, reg, type);
}

bool AArch64FunctionLoweringFacade::emit_memory_copy(
    AArch64MachineBlock &machine_block,
    const AArch64VirtualReg &destination_address,
    const AArch64VirtualReg &source_address, const CoreIrType *type,
    AArch64MachineFunction &function) {
    return services_.emit_memory_copy(machine_block, destination_address,
                                      source_address, type, function);
}

std::optional<AArch64VirtualReg>
AArch64FunctionLoweringFacade::prepare_stack_argument_area(
    AArch64MachineBlock &machine_block, std::size_t stack_arg_bytes,
    AArch64MachineFunction &function) {
    if (stack_arg_bytes == 0) {
        return std::nullopt;
    }
    append_stack_pointer_adjust(machine_block, "sub", stack_arg_bytes);
    const AArch64VirtualReg stack_base = create_pointer_virtual_reg(function);
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand(stack_base),
                stack_pointer_operand(true)}));
    return stack_base;
}

void AArch64FunctionLoweringFacade::finish_stack_argument_area(
    AArch64MachineBlock &machine_block, std::size_t stack_arg_bytes) {
    if (stack_arg_bytes == 0) {
        return;
    }
    append_stack_pointer_adjust(machine_block, "add", stack_arg_bytes);
}

void AArch64FunctionLoweringFacade::emit_direct_call(
    AArch64MachineBlock &machine_block, const std::string &callee_name) {
    services_.record_symbol_reference(callee_name, AArch64SymbolKind::Function);
    const AArch64SymbolReference callee_symbol = services_.make_symbol_reference(
        callee_name, AArch64SymbolKind::Function, AArch64SymbolBinding::Global);
    machine_block.append_instruction(
        AArch64MachineInstr("bl", {AArch64MachineOperand::symbol(callee_symbol)},
                            AArch64InstructionFlags{.is_call = true}, {}, {},
                            make_default_aarch64_call_clobber_mask()));
}

bool AArch64FunctionLoweringFacade::emit_indirect_call(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &callee_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "blr", {AArch64MachineOperand::use_virtual_reg(callee_reg)},
        AArch64InstructionFlags{.is_call = true}, {}, {},
        make_default_aarch64_call_clobber_mask()));
    return true;
}

void AArch64FunctionLoweringFacade::append_helper_call(
    AArch64MachineBlock &machine_block, const std::string &symbol_name) {
    services_.record_symbol_reference(symbol_name, AArch64SymbolKind::Helper);
    const AArch64SymbolReference helper_symbol = services_.make_symbol_reference(
        symbol_name, AArch64SymbolKind::Helper, AArch64SymbolBinding::Global);
    machine_block.append_instruction(
        AArch64MachineInstr("bl", {AArch64MachineOperand::symbol(helper_symbol)},
                            AArch64InstructionFlags{.is_call = true}, {}, {},
                            make_default_aarch64_call_clobber_mask()));
}

void AArch64FunctionLoweringFacade::apply_sign_extend_to_virtual_reg(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
    const CoreIrType *source_type, const CoreIrType *target_type) {
    services_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                               source_type, target_type);
}

void AArch64FunctionLoweringFacade::apply_zero_extend_to_virtual_reg(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
    const CoreIrType *source_type, const CoreIrType *target_type) {
    services_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                               source_type, target_type);
}

AArch64VirtualReg AArch64FunctionLoweringFacade::promote_float16_to_float32(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &source_reg,
    AArch64MachineFunction &function) {
    return sysycc::promote_float16_to_float32(machine_block, source_reg,
                                              function);
}

void AArch64FunctionLoweringFacade::demote_float32_to_float16(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &source_reg,
    const AArch64VirtualReg &target_reg) {
    sysycc::demote_float32_to_float16(machine_block, source_reg, target_reg);
}

bool AArch64FunctionLoweringFacade::materialize_float_constant(
    AArch64MachineBlock &machine_block, const CoreIrConstantFloat &constant,
    const AArch64VirtualReg &target_reg, AArch64MachineFunction &function) {
    return sysycc::materialize_float_constant(machine_block, services_,
                                              constant, target_reg, function);
}

bool AArch64FunctionLoweringFacade::is_promoted_stack_slot(
    const CoreIrStackSlot *stack_slot) const {
    return state_.value_state.promoted_stack_slots.find(stack_slot) !=
           state_.value_state.promoted_stack_slots.end();
}

std::optional<AArch64VirtualReg>
AArch64FunctionLoweringFacade::get_promoted_stack_slot_value(
    const CoreIrStackSlot *stack_slot) const {
    const auto it =
        state_.value_state.promoted_stack_slot_values.find(stack_slot);
    if (it == state_.value_state.promoted_stack_slot_values.end()) {
        return std::nullopt;
    }
    return it->second;
}

void AArch64FunctionLoweringFacade::set_promoted_stack_slot_value(
    const CoreIrStackSlot *stack_slot, const AArch64VirtualReg &value_reg) {
    state_.value_state.promoted_stack_slot_values[stack_slot] = value_reg;
}

void AArch64FunctionLoweringFacade::append_register_copy(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
    const AArch64VirtualReg &source_reg) {
    sysycc::append_register_copy(machine_block, target_reg, source_reg);
}

bool AArch64FunctionLoweringFacade::emit_float128_binary_helper(
    AArch64MachineBlock &machine_block, CoreIrBinaryOpcode opcode,
    const AArch64VirtualReg &lhs_reg, const AArch64VirtualReg &rhs_reg,
    const AArch64VirtualReg &dst_reg) {
    return sysycc::emit_float128_binary_helper(machine_block, *this, opcode,
                                               lhs_reg, rhs_reg, dst_reg);
}

bool AArch64FunctionLoweringFacade::emit_float128_compare_helper(
    AArch64MachineBlock &machine_block, CoreIrComparePredicate predicate,
    const AArch64VirtualReg &lhs_reg, const AArch64VirtualReg &rhs_reg,
    const AArch64VirtualReg &dst_reg, AArch64MachineFunction &function) {
    return sysycc::emit_float128_compare_helper(
        machine_block, *this, predicate, lhs_reg, rhs_reg, dst_reg, function);
}

bool AArch64FunctionLoweringFacade::emit_float128_cast_helper(
    AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
    const AArch64VirtualReg &operand_reg, const AArch64VirtualReg &dst_reg,
    AArch64MachineFunction &function) {
    return sysycc::emit_float128_cast_helper(machine_block, *this, cast,
                                             operand_reg, dst_reg, function);
}

DiagnosticEngine &AArch64FunctionLoweringFacade::diagnostic_engine() const {
    return services_.diagnostic_engine();
}

const std::vector<std::size_t> *
AArch64FunctionLoweringFacade::lookup_indirect_call_copy_offsets(
    const CoreIrCallInst &call) const {
    const auto it =
        state_.call_state.indirect_call_argument_copy_offsets.find(&call);
    if (it == state_.call_state.indirect_call_argument_copy_offsets.end()) {
        return nullptr;
    }
    return &it->second;
}

const AArch64FunctionAbiInfo &
AArch64FunctionLoweringFacade::function_abi_info() const {
    return state_.call_state.abi_info;
}

const AArch64VirtualReg &
AArch64FunctionLoweringFacade::indirect_result_address() const {
    return state_.value_state.indirect_result_address;
}

void AArch64FunctionLoweringFacade::emit_debug_location(
    AArch64MachineBlock &machine_block, const SourceSpan &source_span,
    FunctionState &state) {
    services_.emit_debug_location(machine_block, source_span, state);
}

std::string AArch64FunctionLoweringFacade::resolve_branch_target_label(
    const FunctionState &state, const CoreIrBasicBlock *predecessor,
    const CoreIrBasicBlock *successor) const {
    return services_.resolve_branch_target_label(state, predecessor, successor);
}

bool AArch64FunctionLoweringFacade::emit_load(
    AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
    FunctionState &state) {
    (void)state;
    return sysycc::emit_load_instruction(machine_block, *this, *this, load,
                                         *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_store(
    AArch64MachineBlock &machine_block, const CoreIrStoreInst &store,
    FunctionState &state) {
    (void)state;
    return sysycc::emit_store_instruction(machine_block, *this, *this, store,
                                          *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_binary(
    AArch64MachineBlock &machine_block, const CoreIrBinaryInst &binary,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_binary_instruction(machine_block, *this, binary,
                                           *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_unary(
    AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_unary_instruction(machine_block, *this, unary,
                                          *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_compare(
    AArch64MachineBlock &machine_block, const CoreIrCompareInst &compare,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_compare_instruction(machine_block, *this, compare,
                                            *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_cast(
    AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_cast_instruction(machine_block, *this, cast,
                                         *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_call(
    AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_call_instruction(machine_block, *this, *this, call,
                                         *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_cond_jump(
    AArch64MachineBlock &machine_block, const CoreIrCondJumpInst &cond_jump,
    const FunctionState &state, const CoreIrBasicBlock *current_block) {
    (void)state;
    AArch64VirtualReg condition_reg;
    if (!ensure_value_in_vreg(machine_block, cond_jump.get_condition(),
                              condition_reg)) {
        return false;
    }
    machine_block.append_instruction(
        AArch64MachineInstr(
            "cbnz",
            {AArch64MachineOperand::use_virtual_reg(condition_reg),
             AArch64MachineOperand::label(resolve_branch_target_label(
                 state_, current_block, cond_jump.get_true_block()))}));
    machine_block.append_instruction(AArch64MachineInstr(
        "b",
        {AArch64MachineOperand::label(resolve_branch_target_label(
            state_, current_block, cond_jump.get_false_block()))}));
    return true;
}

bool AArch64FunctionLoweringFacade::emit_return(
    AArch64MachineFunction &machine_function,
    AArch64MachineBlock &machine_block, const CoreIrReturnInst &return_inst,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_return_instruction(machine_function, machine_block,
                                           *this, *this, return_inst);
}

bool AArch64FunctionLoweringFacade::emit_address_of_stack_slot(
    AArch64MachineBlock &machine_block,
    const CoreIrAddressOfStackSlotInst &inst, const FunctionState &state) {
    (void)state;
    AArch64VirtualReg target_reg;
    if (!require_canonical_vreg(&inst, target_reg)) {
        return false;
    }
    return sysycc::emit_address_of_stack_slot_value(
        machine_block, *this, inst, target_reg, *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_address_of_global(
    AArch64MachineBlock &machine_block, const CoreIrAddressOfGlobalInst &inst,
    const FunctionState &state) {
    (void)state;
    AArch64VirtualReg target_reg;
    if (!require_canonical_vreg(&inst, target_reg)) {
        return false;
    }
    return sysycc::emit_address_of_global_value(machine_block, *this, inst,
                                                target_reg);
}

bool AArch64FunctionLoweringFacade::emit_address_of_function(
    AArch64MachineBlock &machine_block, const CoreIrAddressOfFunctionInst &inst,
    const FunctionState &state) {
    (void)state;
    AArch64VirtualReg target_reg;
    if (!require_canonical_vreg(&inst, target_reg)) {
        return false;
    }
    return sysycc::emit_address_of_function_value(machine_block, *this, inst,
                                                  target_reg);
}

bool AArch64FunctionLoweringFacade::emit_getelementptr(
    AArch64MachineBlock &machine_block, const CoreIrGetElementPtrInst &gep,
    const FunctionState &state) {
    (void)state;
    AArch64VirtualReg target_reg;
    if (!require_canonical_vreg(&gep, target_reg)) {
        return false;
    }
    return sysycc::emit_getelementptr_value(
        machine_block, *this, gep, target_reg, *state_.machine_function);
}

} // namespace sysycc
