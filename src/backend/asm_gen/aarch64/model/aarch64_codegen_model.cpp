#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

AArch64MachineOperand AArch64MachineOperand::make_string_payload_operand(
    AArch64MachineOperandKind kind, std::string text) {
    std::string rendered = text;
    return AArch64MachineOperand(kind, std::move(rendered), std::move(text));
}

AArch64MachineOperand::AArch64MachineOperand(AArch64MachineOperandKind kind,
                                             std::string text, Payload payload)
    : kind_(kind), text_(std::move(text)), payload_(std::move(payload)) {}

AArch64MachineOperand::AArch64MachineOperand(std::string text)
    : AArch64MachineOperand(AArch64MachineOperandKind::RawText, std::move(text),
                            std::monostate{}) {}

AArch64MachineOperand AArch64MachineOperand::raw_text(std::string text) {
    return AArch64MachineOperand(AArch64MachineOperandKind::RawText,
                                 std::move(text), std::monostate{});
}

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

const AArch64MachineVirtualRegOperand *
AArch64MachineOperand::get_virtual_reg_operand() const noexcept {
    return std::get_if<AArch64MachineVirtualRegOperand>(&payload_);
}

const AArch64MachinePhysicalRegOperand *
AArch64MachineOperand::get_physical_reg_operand() const noexcept {
    return std::get_if<AArch64MachinePhysicalRegOperand>(&payload_);
}

AArch64MachineInstr::AArch64MachineInstr(std::string text) {
    auto parsed = parse_machine_instruction_text(std::move(text));
    mnemonic_ = std::move(parsed.first);
    operands_ = std::move(parsed.second);
    explicit_defs_ = collect_explicit_vreg_ids(operands_, true);
    explicit_uses_ = collect_explicit_vreg_ids(operands_, false);
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
