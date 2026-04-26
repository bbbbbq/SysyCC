#include "backend/asm_gen/aarch64/support/aarch64_function_lowering_facade.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
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

std::string integer_condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
        return "lt";
    case CoreIrComparePredicate::SignedLessEqual:
        return "le";
    case CoreIrComparePredicate::SignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
        return "ge";
    case CoreIrComparePredicate::UnsignedLess:
        return "lo";
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::UnsignedGreater:
        return "hi";
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "hs";
    }
    return "eq";
}

std::string float_condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
    case CoreIrComparePredicate::UnsignedLess:
        return "mi";
    case CoreIrComparePredicate::SignedLessEqual:
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::SignedGreater:
    case CoreIrComparePredicate::UnsignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "ge";
    }
    return "eq";
}

bool is_compare_only_used_by_cond_jump(const CoreIrCompareInst &compare,
                                       const CoreIrCondJumpInst &cond_jump) {
    const auto &uses = compare.get_uses();
    return uses.size() == 1 && uses.front().get_user() == &cond_jump &&
           uses.front().get_operand_index() == 0;
}

std::optional<long long>
try_get_compare_immediate(const CoreIrValue *value) {
    if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
        return 0;
    }
    const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
    if (constant_int == nullptr || constant_int->get_value() > 0xfffU) {
        return std::nullopt;
    }
    return static_cast<long long>(constant_int->get_value());
}

std::uint64_t bitmask_for_width(unsigned width) {
    return width >= 64U ? ~0ULL : ((1ULL << width) - 1ULL);
}

std::uint64_t rotate_right_masked(std::uint64_t value, unsigned amount,
                                  unsigned width) {
    const unsigned normalized = amount % width;
    const std::uint64_t mask = bitmask_for_width(width);
    value &= mask;
    if (normalized == 0) {
        return value;
    }
    if (width == 64U) {
        return ((value >> normalized) | (value << (64U - normalized))) & mask;
    }
    return ((value >> normalized) | (value << (width - normalized))) & mask;
}

std::uint64_t replicate_element_pattern(std::uint64_t element, unsigned element_width,
                                        unsigned reg_width) {
    const std::uint64_t masked_element = element & bitmask_for_width(element_width);
    if (element_width == reg_width) {
        return masked_element;
    }
    std::uint64_t value = 0;
    for (unsigned offset = 0; offset < reg_width; offset += element_width) {
        value |= masked_element << offset;
    }
    return value & bitmask_for_width(reg_width);
}

bool can_encode_logical_immediate(std::uint64_t value, unsigned reg_width) {
    const std::uint64_t mask = bitmask_for_width(reg_width);
    value &= mask;
    if (value == 0 || value == mask) {
        return false;
    }

    for (unsigned element_width = 2; element_width <= reg_width; element_width <<= 1U) {
        const std::uint64_t element_mask = bitmask_for_width(element_width);
        const std::uint64_t element = value & element_mask;
        if (replicate_element_pattern(element, element_width, reg_width) != value) {
            continue;
        }
        for (unsigned ones = 1; ones < element_width; ++ones) {
            const std::uint64_t one_run = bitmask_for_width(ones);
            for (unsigned rotation = 0; rotation < element_width; ++rotation) {
                if (rotate_right_masked(one_run, rotation, element_width) ==
                    element) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool compare_uses_foldable_tst_mask(const CoreIrCompareInst &compare,
                                    const CoreIrBinaryInst *&and_binary,
                                    std::uint64_t &mask_value,
                                    const CoreIrValue *&tested_value) {
    if ((compare.get_predicate() != CoreIrComparePredicate::Equal &&
         compare.get_predicate() != CoreIrComparePredicate::NotEqual) ||
        dynamic_cast<const CoreIrConstantNull *>(compare.get_rhs()) == nullptr) {
        const auto *constant_int =
            dynamic_cast<const CoreIrConstantInt *>(compare.get_rhs());
        if (constant_int == nullptr || constant_int->get_value() != 0) {
            return false;
        }
    }

    and_binary = dynamic_cast<const CoreIrBinaryInst *>(compare.get_lhs());
    if (and_binary == nullptr ||
        and_binary->get_binary_opcode() != CoreIrBinaryOpcode::And ||
        and_binary->get_uses().size() != 1) {
        return false;
    }

    const auto try_constant_mask =
        [](const CoreIrValue *value) -> std::optional<std::uint64_t> {
        if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
            return 0;
        }
        const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
        if (constant_int == nullptr) {
            return std::nullopt;
        }
        return constant_int->get_value();
    };

    tested_value = nullptr;
    if (const auto mask = try_constant_mask(and_binary->get_rhs());
        mask.has_value()) {
        mask_value = *mask;
        tested_value = and_binary->get_lhs();
        return true;
    }
    if (const auto mask = try_constant_mask(and_binary->get_lhs());
        mask.has_value()) {
        mask_value = *mask;
        tested_value = and_binary->get_rhs();
        return true;
    }
    return false;
}

std::string make_block_integer_constant_key(const AArch64MachineBlock &machine_block,
                                            const CoreIrType *type,
                                            std::uint64_t value) {
    return machine_block.get_label() + "|" +
           std::to_string(static_cast<std::uintptr_t>(
               reinterpret_cast<std::uintptr_t>(type))) +
           "|" + std::to_string(value);
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
    if (value != 0) {
        const std::string cache_key =
            make_block_integer_constant_key(machine_block, type, value);
        const auto cached = block_integer_constant_vregs_.find(cache_key);
        if (cached != block_integer_constant_vregs_.end() &&
            cached->second.is_valid() && cached->second.get_kind() == target_reg.get_kind() &&
            cached->second.get_id() != target_reg.get_id()) {
            machine_block.append_instruction(AArch64MachineInstr(
                "mov", {def_vreg_operand(target_reg), use_vreg_operand(cached->second)}));
            return true;
        }
    }

    const bool ok = sysycc::materialize_integer_constant(machine_block, services_, type,
                                                         value, target_reg);
    if (ok && value != 0) {
        block_integer_constant_vregs_[make_block_integer_constant_key(
            machine_block, type, value)] = target_reg;
    }
    return ok;
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

bool AArch64FunctionLoweringFacade::materialize_noncanonical_value(
    AArch64MachineBlock &machine_block, const CoreIrValue *value,
    const AArch64VirtualReg &target_reg) {
    return services_.materialize_noncanonical_value(machine_block, value,
                                                    target_reg, state_, *this);
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
    if (const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
        constant_int != nullptr && constant_int->get_value() != 0) {
        const auto cached = block_integer_constant_vregs_.find(
            make_block_integer_constant_key(machine_block, value->get_type(),
                                            constant_int->get_value()));
        if (cached != block_integer_constant_vregs_.end() &&
            cached->second.is_valid()) {
            out = cached->second;
            return true;
        }
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
        std::string detail =
            "missing canonical AArch64 aggregate memory location for Core IR value";
        if (value != nullptr && !value->get_name().empty()) {
            detail += " '" + value->get_name() + "'";
        }
        if (const auto *instruction =
                dynamic_cast<const CoreIrInstruction *>(value);
            instruction != nullptr) {
            detail += " (opcode=" +
                      std::to_string(static_cast<int>(instruction->get_opcode())) +
                      ")";
        }
        report_error(detail);
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
    state_.machine_function->set_has_calls(true);
    machine_block.append_instruction(
        AArch64MachineInstr("bl", {AArch64MachineOperand::symbol(callee_symbol)},
                            AArch64InstructionFlags{.is_call = true}, {}, {},
                            make_default_aarch64_call_clobber_mask()));
}

bool AArch64FunctionLoweringFacade::emit_indirect_call(
    AArch64MachineBlock &machine_block, const AArch64VirtualReg &callee_reg) {
    state_.machine_function->set_has_calls(true);
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
    state_.machine_function->set_has_calls(true);
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

AArch64VariadicVaListState
AArch64FunctionLoweringFacade::variadic_va_list_state() const {
    AArch64VariadicVaListState result;
    const auto &frame_info = state_.machine_function->get_frame_info();
    result.is_variadic_function =
        frame_info.get_variadic_gpr_save_area_offset().has_value() ||
        frame_info.get_variadic_fpr_save_area_offset().has_value();
    result.incoming_stack_offset = frame_info.get_variadic_incoming_stack_offset();
    result.named_gpr_slots = frame_info.get_variadic_named_gpr_slots();
    result.named_fpr_slots = frame_info.get_variadic_named_fpr_slots();
    result.gpr_save_area_offset = frame_info.get_variadic_gpr_save_area_offset();
    result.fpr_save_area_offset = frame_info.get_variadic_fpr_save_area_offset();
    return result;
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
    if (compare.get_uses().size() == 1) {
        const CoreIrUse &use = compare.get_uses().front();
        if (use.get_operand_index() == 0 &&
            dynamic_cast<const CoreIrCondJumpInst *>(use.get_user()) != nullptr) {
            return true;
        }
    }
    return sysycc::emit_compare_instruction(machine_block, *this, compare,
                                            *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_select(
    AArch64MachineBlock &machine_block, const CoreIrSelectInst &select,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_select_instruction(machine_block, *this, select,
                                           *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_extract_element(
    AArch64MachineBlock &machine_block, const CoreIrExtractElementInst &extract,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_extract_element_instruction(
        machine_block, *this, extract, *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_insert_element(
    AArch64MachineBlock &machine_block, const CoreIrInsertElementInst &insert,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_insert_element_instruction(
        machine_block, *this, insert, *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_shuffle_vector(
    AArch64MachineBlock &machine_block, const CoreIrShuffleVectorInst &shuffle,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_shuffle_vector_instruction(
        machine_block, *this, shuffle, *state_.machine_function);
}

bool AArch64FunctionLoweringFacade::emit_vector_reduce_add(
    AArch64MachineBlock &machine_block, const CoreIrVectorReduceAddInst &reduce,
    const FunctionState &state) {
    (void)state;
    return sysycc::emit_vector_reduce_add_instruction(
        machine_block, *this, reduce, *state_.machine_function);
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
    if (const auto *compare =
            dynamic_cast<const CoreIrCompareInst *>(cond_jump.get_condition());
        compare != nullptr && is_compare_only_used_by_cond_jump(*compare, cond_jump)) {
        AArch64VirtualReg lhs_reg;
        if (!ensure_value_in_vreg(machine_block, compare->get_lhs(), lhs_reg)) {
            return false;
        }

        if (is_float_type(compare->get_lhs()->get_type())) {
            AArch64VirtualReg rhs_reg;
            if (!ensure_value_in_vreg(machine_block, compare->get_rhs(), rhs_reg)) {
                return false;
            }
            if (lhs_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                lhs_reg = promote_float16_to_float32(machine_block, lhs_reg,
                                                     *state_.machine_function);
                rhs_reg = promote_float16_to_float32(machine_block, rhs_reg,
                                                     *state_.machine_function);
            }
            machine_block.append_instruction(
                AArch64MachineInstr("fcmp", {use_vreg_operand(lhs_reg),
                                             use_vreg_operand(rhs_reg)}));
            machine_block.append_instruction(AArch64MachineInstr(
                "b." + float_condition_code(compare->get_predicate()),
                {AArch64MachineOperand::label(resolve_branch_target_label(
                    state_, current_block, cond_jump.get_true_block()))}));
        } else {
            const CoreIrBinaryInst *folded_and = nullptr;
            std::uint64_t mask_value = 0;
            const CoreIrValue *tested_value = nullptr;
            if (compare_uses_foldable_tst_mask(*compare, folded_and, mask_value,
                                               tested_value) &&
                folded_and != nullptr && tested_value != nullptr &&
                can_encode_logical_immediate(mask_value,
                                             is_pointer_type(
                                                 tested_value->get_type()) ||
                                                     get_storage_size(
                                                         tested_value->get_type()) > 4
                                                 ? 64U
                                                 : 32U)) {
                AArch64VirtualReg tested_reg;
                if (!ensure_value_in_vreg(machine_block, tested_value, tested_reg)) {
                    return false;
                }
                machine_block.append_instruction(AArch64MachineInstr(
                    "tst", {use_vreg_operand(tested_reg),
                            AArch64MachineOperand::immediate(
                                "#" + std::to_string(mask_value))}));
                machine_block.append_instruction(AArch64MachineInstr(
                    "b." + integer_condition_code(compare->get_predicate()),
                    {AArch64MachineOperand::label(resolve_branch_target_label(
                        state_, current_block, cond_jump.get_true_block()))}));
                machine_block.append_instruction(AArch64MachineInstr(
                    "b",
                    {AArch64MachineOperand::label(resolve_branch_target_label(
                        state_, current_block, cond_jump.get_false_block()))}));
                return true;
            }

            if (const auto rhs_immediate =
                    try_get_compare_immediate(compare->get_rhs());
                rhs_immediate.has_value()) {
                machine_block.append_instruction(AArch64MachineInstr(
                    "cmp", {use_vreg_operand(lhs_reg),
                            AArch64MachineOperand::immediate(
                                "#" + std::to_string(*rhs_immediate))}));
            } else {
                AArch64VirtualReg rhs_reg;
                if (!ensure_value_in_vreg(machine_block, compare->get_rhs(),
                                          rhs_reg)) {
                    return false;
                }
                machine_block.append_instruction(
                    AArch64MachineInstr("cmp", {use_vreg_operand(lhs_reg),
                                                use_vreg_operand(rhs_reg)}));
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "b." + integer_condition_code(compare->get_predicate()),
                {AArch64MachineOperand::label(resolve_branch_target_label(
                    state_, current_block, cond_jump.get_true_block()))}));
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "b",
            {AArch64MachineOperand::label(resolve_branch_target_label(
                state_, current_block, cond_jump.get_false_block()))}));
        return true;
    }

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

bool AArch64FunctionLoweringFacade::emit_indirect_jump(
    AArch64MachineBlock &machine_block, const CoreIrIndirectJumpInst &indirect_jump,
    const FunctionState &state, const CoreIrBasicBlock *current_block) {
    (void)state;
    AArch64VirtualReg address_reg;
    if (!ensure_value_in_vreg(machine_block, indirect_jump.get_address(),
                              address_reg)) {
        return false;
    }

    bool needs_dispatch_chain = false;
    for (const CoreIrBasicBlock *target : indirect_jump.get_target_blocks()) {
        if (target == nullptr) {
            report_error("encountered null indirect branch target in the AArch64 native backend");
            return false;
        }
        if (resolve_branch_target_label(state_, current_block, target) !=
            block_label(target)) {
            needs_dispatch_chain = true;
            break;
        }
    }

    if (!needs_dispatch_chain) {
        machine_block.append_instruction(
            AArch64MachineInstr("br",
                                {AArch64MachineOperand::use_virtual_reg(address_reg)}));
        return true;
    }

    for (const CoreIrBasicBlock *target : indirect_jump.get_target_blocks()) {
        const std::string original_label = block_label(target);
        const std::string lowered_label =
            resolve_branch_target_label(state_, current_block, target);
        const AArch64VirtualReg target_reg =
            create_pointer_virtual_reg(*state_.machine_function);
        if (!materialize_global_address(machine_block, *this, original_label, target_reg,
                                        AArch64SymbolKind::Label)) {
            return false;
        }
        machine_block.append_instruction(
            AArch64MachineInstr("cmp", {AArch64MachineOperand::use_virtual_reg(address_reg),
                                         AArch64MachineOperand::use_virtual_reg(target_reg)}));
        machine_block.append_instruction(
            AArch64MachineInstr("b.eq",
                                {AArch64MachineOperand::label(lowered_label)}));
    }

    // LLVM indirectbr is undefined if the address does not match any listed target.
    machine_block.append_instruction(
        AArch64MachineInstr("br",
                            {AArch64MachineOperand::use_virtual_reg(address_reg)}));
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
