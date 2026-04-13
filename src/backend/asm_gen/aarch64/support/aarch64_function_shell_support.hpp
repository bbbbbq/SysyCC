#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrFunction;

enum class AArch64StandardFrameShellOpKind : unsigned char {
    SaveFrameRecord,
    EstablishFramePointer,
    AllocateLocalFrame,
    DeallocateLocalFrame,
    RestoreFrameRecord,
    Return,
};

struct AArch64StandardFrameShellOp {
    AArch64StandardFrameShellOpKind kind =
        AArch64StandardFrameShellOpKind::SaveFrameRecord;
    AArch64MachineInstr instruction;
};

struct AArch64StandardFrameShellCfiBundle {
    std::vector<AArch64CfiDirective> frame_record_directives;
    std::vector<AArch64MachineInstr> asm_instructions;
};

std::string sanitize_aarch64_label_fragment(const std::string &text);
std::string make_aarch64_function_epilogue_label(const std::string &function_name);
std::string make_aarch64_function_block_label(const std::string &function_name,
                                              const std::string &block_name);
std::string make_aarch64_function_block_label(const std::string &function_name,
                                              const CoreIrBasicBlock &block);
std::unordered_map<const CoreIrBasicBlock *, std::string>
build_aarch64_function_block_labels(const CoreIrFunction &function,
                                    const std::string &function_name);
void initialize_aarch64_function_frame_record(AArch64MachineFunction &function,
                                              std::size_t frame_size);
std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_prologue_shell(std::size_t frame_size);
std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_epilogue_shell(std::size_t frame_size);
AArch64StandardFrameShellCfiBundle
build_aarch64_standard_shell_cfi_bundle(AArch64StandardFrameShellOpKind op_kind,
                                        std::size_t frame_size);
void append_aarch64_frame_record_cfi_for_shell_op(
    AArch64FrameRecord &frame_record, AArch64StandardFrameShellOpKind op_kind,
    std::size_t frame_size);
void append_aarch64_asm_cfi_for_shell_op(
    std::vector<AArch64MachineInstr> &instructions,
    AArch64StandardFrameShellOpKind op_kind, std::size_t frame_size);
std::size_t count_aarch64_standard_prologue_prefix(
    const std::vector<AArch64MachineInstr> &instructions);
void append_aarch64_standard_prologue(AArch64MachineBlock &block,
                                      std::size_t frame_size);
void append_aarch64_standard_epilogue(AArch64MachineBlock &block,
                                      std::size_t frame_size);

} // namespace sysycc
