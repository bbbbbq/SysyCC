#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.hpp"

namespace sysycc {

class DiagnosticEngine;

struct EncodedFloatReg {
    unsigned code = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::Float64;
};

struct AArch64MemoryInstructionEncodingContext {
    std::function<std::optional<EncodedGeneralReg>(
        const AArch64MachineOperand &, const AArch64MachineFunction &, bool, bool,
        DiagnosticEngine &, const std::string &)>
        resolve_general_reg_operand;
    std::function<std::optional<EncodedFloatReg>(
        const AArch64MachineOperand &, const AArch64MachineFunction &,
        DiagnosticEngine &, const std::string &)>
        resolve_float_reg_operand;
    std::function<std::optional<AArch64MachineSymbolReference>(
        const AArch64MachineOperand &)>
        get_symbol_reference_operand;
};

std::optional<EncodedInstruction> encode_memory_family_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine,
    const AArch64MemoryInstructionEncodingContext &context);

} // namespace sysycc
