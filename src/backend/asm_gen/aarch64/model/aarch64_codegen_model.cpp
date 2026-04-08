#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

namespace {

AArch64VirtualReg memory_base_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::General64);
}

} // namespace

AArch64MachineOperand::AArch64MachineOperand(AArch64MachineOperandKind kind,
                                             Payload payload)
    : kind_(kind), payload_(std::move(payload)) {}

AArch64MachineOperand
AArch64MachineOperand::use_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg,
                                 AArch64MachineVirtualRegOperand{reg, false});
}

AArch64MachineOperand
AArch64MachineOperand::def_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg,
                                 AArch64MachineVirtualRegOperand{reg, true});
}

AArch64MachineOperand
AArch64MachineOperand::physical_reg(unsigned reg_number,
                                    AArch64VirtualRegKind kind) {
    return AArch64MachineOperand(AArch64MachineOperandKind::PhysicalReg,
                                 AArch64MachinePhysicalRegOperand{reg_number, kind});
}

AArch64MachineOperand AArch64MachineOperand::immediate(std::string text) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Immediate,
                                 AArch64MachineImmediateOperand{std::move(text)});
}

AArch64MachineOperand AArch64MachineOperand::symbol(std::string text) {
    return AArch64MachineOperand::symbol(
        AArch64MachineSymbolReference::plain(std::move(text)));
}

AArch64MachineOperand
AArch64MachineOperand::symbol(AArch64MachineSymbolReference reference) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Symbol,
                                 AArch64MachineSymbolOperand{std::move(reference)});
}

AArch64MachineOperand AArch64MachineOperand::label(std::string text) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Label,
                                 AArch64MachineLabelOperand{std::move(text)});
}

AArch64MachineOperand AArch64MachineOperand::condition_code(std::string code) {
    return AArch64MachineOperand(AArch64MachineOperandKind::ConditionCode,
                                 AArch64MachineConditionCodeOperand{
                                     std::move(code)});
}

AArch64MachineOperand AArch64MachineOperand::zero_register(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::ZeroRegister,
                                 AArch64MachineZeroRegisterOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::shift(std::string mnemonic,
                                                   unsigned amount) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Shift,
                                 AArch64MachineShiftOperand{std::move(mnemonic),
                                                            amount});
}

AArch64MachineOperand AArch64MachineOperand::stack_pointer(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::StackPointer,
                                 AArch64MachineStackPointerOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_virtual_reg(
    const AArch64VirtualReg &reg, std::optional<long long> immediate_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    const AArch64VirtualReg base_reg = memory_base_virtual_reg(reg);
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg,
            .virtual_reg = base_reg,
            .physical_reg = 0,
            .stack_pointer_use_64bit = true,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg(
    unsigned reg_number, std::optional<long long> immediate_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = reg_number,
            .stack_pointer_use_64bit = true,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_stack_pointer(
    std::optional<long long> immediate_offset, bool use_64bit,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::StackPointer,
            .virtual_reg = {},
            .physical_reg = 0,
            .stack_pointer_use_64bit = use_64bit,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_virtual_reg(
    const AArch64VirtualReg &reg, AArch64MachineSymbolReference symbolic_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    const AArch64VirtualReg base_reg = memory_base_virtual_reg(reg);
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg,
            .virtual_reg = base_reg,
            .physical_reg = 0,
            .stack_pointer_use_64bit = true,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg(
    unsigned reg_number, AArch64MachineSymbolReference symbolic_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = reg_number,
            .stack_pointer_use_64bit = true,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_stack_pointer(
    AArch64MachineSymbolReference symbolic_offset, bool use_64bit,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::StackPointer,
            .virtual_reg = {},
            .physical_reg = 0,
            .stack_pointer_use_64bit = use_64bit,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

const AArch64MachineVirtualRegOperand *
AArch64MachineOperand::get_virtual_reg_operand() const noexcept {
    return std::get_if<AArch64MachineVirtualRegOperand>(&payload_);
}

const AArch64MachinePhysicalRegOperand *
AArch64MachineOperand::get_physical_reg_operand() const noexcept {
    return std::get_if<AArch64MachinePhysicalRegOperand>(&payload_);
}

const AArch64MachineConditionCodeOperand *
AArch64MachineOperand::get_condition_code_operand() const noexcept {
    return std::get_if<AArch64MachineConditionCodeOperand>(&payload_);
}

const AArch64MachineImmediateOperand *
AArch64MachineOperand::get_immediate_operand() const noexcept {
    return std::get_if<AArch64MachineImmediateOperand>(&payload_);
}

const AArch64MachineSymbolOperand *
AArch64MachineOperand::get_symbol_operand() const noexcept {
    return std::get_if<AArch64MachineSymbolOperand>(&payload_);
}

const AArch64MachineLabelOperand *
AArch64MachineOperand::get_label_operand() const noexcept {
    return std::get_if<AArch64MachineLabelOperand>(&payload_);
}

const AArch64MachineZeroRegisterOperand *
AArch64MachineOperand::get_zero_register_operand() const noexcept {
    return std::get_if<AArch64MachineZeroRegisterOperand>(&payload_);
}

const AArch64MachineShiftOperand *
AArch64MachineOperand::get_shift_operand() const noexcept {
    return std::get_if<AArch64MachineShiftOperand>(&payload_);
}

const AArch64MachineStackPointerOperand *
AArch64MachineOperand::get_stack_pointer_operand() const noexcept {
    return std::get_if<AArch64MachineStackPointerOperand>(&payload_);
}

const AArch64MachineMemoryAddressOperand *
AArch64MachineOperand::get_memory_address_operand() const noexcept {
    return std::get_if<AArch64MachineMemoryAddressOperand>(&payload_);
}

AArch64MachineInstr::AArch64MachineInstr(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands,
    AArch64InstructionFlags flags, std::vector<std::size_t> implicit_defs,
    std::vector<std::size_t> implicit_uses,
    std::optional<AArch64CallClobberMask> call_clobber_mask)
    : mnemonic_(std::move(mnemonic)),
      operands_(std::move(operands)),
      flags_(flags),
      explicit_defs_(collect_explicit_vreg_ids(operands_, true)),
      explicit_uses_(collect_explicit_vreg_ids(operands_, false)),
      implicit_defs_(std::move(implicit_defs)),
      implicit_uses_(std::move(implicit_uses)),
      call_clobber_mask_(std::move(call_clobber_mask)) {}

} // namespace sysycc
