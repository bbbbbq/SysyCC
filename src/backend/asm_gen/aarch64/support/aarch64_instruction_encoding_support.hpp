#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

namespace sysycc {

class DiagnosticEngine;

struct SourceLocationNote {
    std::size_t pc_offset = 0;
    unsigned file_id = 0;
    int line = 0;
    int column = 0;
};

struct FunctionScanInfo {
    std::unordered_map<std::string, std::size_t> label_offsets;
    std::vector<SourceLocationNote> source_locations;
    std::size_t code_size = 0;
};

struct EncodedInstruction {
    std::uint32_t word = 0;
    std::vector<AArch64RelocationRecord> relocations;
};

bool is_real_text_instruction(const AArch64MachineInstr &instruction) noexcept;
bool is_loc_instruction(const AArch64MachineInstr &instruction) noexcept;
bool is_cfi_instruction(const AArch64MachineInstr &instruction) noexcept;

FunctionScanInfo scan_function_layout(const AArch64MachineFunction &function,
                                      DiagnosticEngine &diagnostic_engine);

std::optional<EncodedInstruction> encode_machine_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine);

} // namespace sysycc
