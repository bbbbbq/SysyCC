#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_machine_ir.hpp"

namespace sysycc {

class DiagnosticEngine;
struct EncodedInstruction;
struct FunctionScanInfo;

struct EncodedGeneralReg {
    unsigned code = 0;
    bool use_64bit = false;
    bool is_stack_pointer = false;
    bool is_zero_register = false;
};

struct AArch64BranchInstructionEncodingContext {
    std::function<std::optional<EncodedGeneralReg>(
        const AArch64MachineOperand &, const AArch64MachineFunction &, bool, bool,
        DiagnosticEngine &, const std::string &)>
        resolve_general_reg_operand;
    std::function<std::optional<AArch64MachineSymbolReference>(
        const AArch64MachineOperand &)>
        get_symbol_reference_operand;
};

std::optional<EncodedInstruction> encode_branch_family_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine,
    const AArch64BranchInstructionEncodingContext &context);

} // namespace sysycc
