#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

namespace {

std::string render_memory_address_text(const std::string &base_text,
                                       const std::string &offset_text,
                                       AArch64MachineMemoryAddressOperand::AddressMode
                                           address_mode) {
    if (offset_text.empty()) {
        return "[" + base_text + "]";
    }
    switch (address_mode) {
    case AArch64MachineMemoryAddressOperand::AddressMode::Offset:
        return "[" + base_text + ", " + offset_text + "]";
    case AArch64MachineMemoryAddressOperand::AddressMode::PreIndex:
        return "[" + base_text + ", " + offset_text + "]!";
    case AArch64MachineMemoryAddressOperand::AddressMode::PostIndex:
        return "[" + base_text + "], " + offset_text;
    }
    return "[" + base_text + ", " + offset_text + "]";
}

AArch64VirtualReg memory_base_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::General64);
}

} // namespace

AArch64MachineOperand AArch64MachineOperand::make_string_payload_operand(
    AArch64MachineOperandKind kind, std::string text) {
    std::string rendered = text;
    return AArch64MachineOperand(kind, std::move(rendered), std::move(text));
}

AArch64MachineOperand::AArch64MachineOperand(AArch64MachineOperandKind kind,
                                             std::string text, Payload payload)
    : kind_(kind), text_(std::move(text)), payload_(std::move(payload)) {}

AArch64MachineOperand
AArch64MachineOperand::use_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg, use_vreg(reg),
                                 AArch64MachineVirtualRegOperand{reg, false});
}

AArch64MachineOperand
AArch64MachineOperand::def_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg, def_vreg(reg),
                                 AArch64MachineVirtualRegOperand{reg, true});
}

AArch64MachineOperand
AArch64MachineOperand::physical_reg(unsigned reg_number,
                                    AArch64VirtualRegKind kind) {
    return AArch64MachineOperand(AArch64MachineOperandKind::PhysicalReg,
                                 render_physical_register(reg_number, kind),
                                 AArch64MachinePhysicalRegOperand{reg_number, kind});
}

AArch64MachineOperand AArch64MachineOperand::immediate(std::string text) {
    return make_string_payload_operand(AArch64MachineOperandKind::Immediate,
                                       std::move(text));
}

AArch64MachineOperand AArch64MachineOperand::symbol(std::string text) {
    return make_string_payload_operand(AArch64MachineOperandKind::Symbol,
                                       std::move(text));
}

AArch64MachineOperand AArch64MachineOperand::label(std::string text) {
    return make_string_payload_operand(AArch64MachineOperandKind::Label,
                                       std::move(text));
}

AArch64MachineOperand AArch64MachineOperand::condition_code(std::string code) {
    std::string rendered = code;
    return AArch64MachineOperand(
        AArch64MachineOperandKind::ConditionCode, std::move(rendered),
        AArch64MachineConditionCodeOperand{std::move(code)});
}

AArch64MachineOperand AArch64MachineOperand::zero_register(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::ZeroRegister,
                                 zero_register_name(use_64bit),
                                 AArch64MachineZeroRegisterOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::shift(std::string mnemonic,
                                                   unsigned amount) {
    const std::string rendered =
        mnemonic + " #" + std::to_string(amount);
    return AArch64MachineOperand(AArch64MachineOperandKind::Shift, rendered,
                                 AArch64MachineShiftOperand{std::move(mnemonic),
                                                            amount});
}

AArch64MachineOperand AArch64MachineOperand::stack_pointer(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::StackPointer,
                                 stack_pointer_name(use_64bit),
                                 AArch64MachineStackPointerOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_virtual_reg(
    const AArch64VirtualReg &reg, std::string offset_text,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    const AArch64VirtualReg base_reg = memory_base_virtual_reg(reg);
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        render_memory_address_text(use_vreg(base_reg), offset_text, address_mode),
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg,
            .virtual_reg = base_reg,
            .physical_reg = 0,
            .stack_pointer_use_64bit = true,
            .offset_text = std::move(offset_text),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg(
    unsigned reg_number, std::string offset_text,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        render_memory_address_text(render_physical_register(reg_number, true),
                                   offset_text, address_mode),
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = reg_number,
            .stack_pointer_use_64bit = true,
            .offset_text = std::move(offset_text),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_stack_pointer(
    std::string offset_text, bool use_64bit,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        render_memory_address_text(stack_pointer_name(use_64bit), offset_text,
                                   address_mode),
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::StackPointer,
            .virtual_reg = {},
            .physical_reg = 0,
            .stack_pointer_use_64bit = use_64bit,
            .offset_text = std::move(offset_text),
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
