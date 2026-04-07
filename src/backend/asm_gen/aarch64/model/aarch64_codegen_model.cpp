#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

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
