#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

namespace {

long long to_signed_offset(std::size_t value) {
    return static_cast<long long>(value);
}

bool has_exact_instruction_shape(const AArch64MachineInstr &instruction,
                                 const AArch64MachineInstr &expected) {
    if (instruction.get_mnemonic() != expected.get_mnemonic() ||
        instruction.get_operands().size() != expected.get_operands().size()) {
        return false;
    }
    for (std::size_t index = 0; index < instruction.get_operands().size(); ++index) {
        if (instruction.get_operands()[index].get_text() !=
            expected.get_operands()[index].get_text()) {
            return false;
        }
    }
    return true;
}

bool is_frame_adjust_instruction(const AArch64MachineInstr &instruction,
                                 const char *mnemonic) {
    if (instruction.get_mnemonic() != mnemonic ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    return instruction.get_operands()[0].get_text() == "sp" &&
           instruction.get_operands()[1].get_text() == "sp" &&
           !instruction.get_operands()[2].get_text().empty() &&
           instruction.get_operands()[2].get_text().front() == '#';
}

} // namespace

std::string sanitize_aarch64_label_fragment(const std::string &text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "unnamed";
    }
    return sanitized;
}

std::string make_aarch64_function_epilogue_label(const std::string &function_name) {
    return ".L" + sanitize_aarch64_label_fragment(function_name) + "_epilogue";
}

std::string make_aarch64_function_block_label(const std::string &function_name,
                                              const CoreIrBasicBlock &block) {
    return ".L" + sanitize_aarch64_label_fragment(function_name) + "_" +
           sanitize_aarch64_label_fragment(block.get_name());
}

std::unordered_map<const CoreIrBasicBlock *, std::string>
build_aarch64_function_block_labels(const CoreIrFunction &function,
                                    const std::string &function_name) {
    std::unordered_map<const CoreIrBasicBlock *, std::string> labels;
    for (const auto &basic_block : function.get_basic_blocks()) {
        if (basic_block == nullptr) {
            continue;
        }
        labels.emplace(basic_block.get(),
                       make_aarch64_function_block_label(function_name,
                                                         *basic_block));
    }
    return labels;
}

void initialize_aarch64_function_frame_record(AArch64MachineFunction &function,
                                              std::size_t frame_size) {
    function.get_frame_record().set_stack_frame_size(frame_size);
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::StartProcedure});
    // This frame record seeds shell-only CFI from the same shared shell semantics
    // that both lowering and frame-finalize consume. Final emitted unwind text is
    // still authoritative in frame-finalize once saved-register slots are known.
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_prologue_shell(frame_size)) {
        append_aarch64_frame_record_cfi_for_shell_op(function.get_frame_record(),
                                                     op.kind, frame_size);
    }
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::EndProcedure});
}

std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_prologue_shell(std::size_t frame_size) {
    std::vector<AArch64StandardFrameShellOp> shell;
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::SaveFrameRecord,
        AArch64MachineInstr("stp x29, x30, [sp, #-16]!")});
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::EstablishFramePointer,
        AArch64MachineInstr("mov x29, sp")});
    if (frame_size > 0) {
        shell.push_back(AArch64StandardFrameShellOp{
            AArch64StandardFrameShellOpKind::AllocateLocalFrame,
            AArch64MachineInstr("sub sp, sp, #" + std::to_string(frame_size))});
    }
    return shell;
}

std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_epilogue_shell(std::size_t frame_size) {
    std::vector<AArch64StandardFrameShellOp> shell;
    if (frame_size > 0) {
        shell.push_back(AArch64StandardFrameShellOp{
            AArch64StandardFrameShellOpKind::DeallocateLocalFrame,
            AArch64MachineInstr("add sp, sp, #" + std::to_string(frame_size))});
    }
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::RestoreFrameRecord,
        AArch64MachineInstr("ldp x29, x30, [sp], #16")});
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::Return, AArch64MachineInstr("ret")});
    return shell;
}

AArch64StandardFrameShellCfiBundle
build_aarch64_standard_shell_cfi_bundle(AArch64StandardFrameShellOpKind op_kind,
                                        std::size_t frame_size) {
    AArch64StandardFrameShellCfiBundle bundle;
    switch (op_kind) {
    case AArch64StandardFrameShellOpKind::SaveFrameRecord:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfa,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), 16});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), -16});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                                static_cast<unsigned>(AArch64PhysicalReg::X30), -8});
        bundle.asm_instructions.emplace_back(".cfi_def_cfa_offset 16");
        bundle.asm_instructions.emplace_back(".cfi_offset 29, -16");
        bundle.asm_instructions.emplace_back(".cfi_offset 30, -8");
        break;
    case AArch64StandardFrameShellOpKind::EstablishFramePointer:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaRegister,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), 0});
        bundle.asm_instructions.emplace_back(".cfi_def_cfa_register 29");
        break;
    case AArch64StandardFrameShellOpKind::AllocateLocalFrame:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaOffset,
                                static_cast<unsigned>(AArch64PhysicalReg::X29),
                                to_signed_offset(frame_size + 16)});
        bundle.asm_instructions.emplace_back(".cfi_def_cfa_offset " +
                                             std::to_string(frame_size + 16));
        break;
    case AArch64StandardFrameShellOpKind::DeallocateLocalFrame:
        bundle.asm_instructions.emplace_back(".cfi_def_cfa_offset 16");
        break;
    case AArch64StandardFrameShellOpKind::RestoreFrameRecord:
        bundle.asm_instructions.emplace_back(".cfi_restore 29");
        bundle.asm_instructions.emplace_back(".cfi_restore 30");
        bundle.asm_instructions.emplace_back(".cfi_def_cfa sp, 0");
        break;
    case AArch64StandardFrameShellOpKind::Return:
        bundle.asm_instructions.emplace_back(".cfi_endproc");
        break;
    }
    return bundle;
}

void append_aarch64_frame_record_cfi_for_shell_op(
    AArch64FrameRecord &frame_record, AArch64StandardFrameShellOpKind op_kind,
    std::size_t frame_size) {
    for (const AArch64CfiDirective &directive :
         build_aarch64_standard_shell_cfi_bundle(op_kind, frame_size)
             .frame_record_directives) {
        frame_record.append_cfi_directive(directive);
    }
}

void append_aarch64_asm_cfi_for_shell_op(
    std::vector<AArch64MachineInstr> &instructions,
    AArch64StandardFrameShellOpKind op_kind, std::size_t frame_size) {
    for (const AArch64MachineInstr &directive :
         build_aarch64_standard_shell_cfi_bundle(op_kind, frame_size)
             .asm_instructions) {
        instructions.push_back(directive);
    }
}

std::size_t count_aarch64_standard_prologue_prefix(
    const std::vector<AArch64MachineInstr> &instructions) {
    const std::vector<AArch64StandardFrameShellOp> shell_without_allocation =
        build_aarch64_standard_prologue_shell(0);
    if (instructions.size() < shell_without_allocation.size()) {
        return 0;
    }
    for (std::size_t index = 0; index < shell_without_allocation.size(); ++index) {
        if (!has_exact_instruction_shape(instructions[index],
                                         shell_without_allocation[index].instruction)) {
            return 0;
        }
    }
    std::size_t matched = shell_without_allocation.size();
    if (instructions.size() > matched &&
        is_frame_adjust_instruction(instructions[matched], "sub")) {
        ++matched;
    }
    return matched;
}

void append_aarch64_standard_prologue(AArch64MachineBlock &block,
                                      std::size_t frame_size) {
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_prologue_shell(frame_size)) {
        block.append_instruction(op.instruction);
    }
}

void append_aarch64_standard_epilogue(AArch64MachineBlock &block,
                                      std::size_t frame_size) {
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_epilogue_shell(frame_size)) {
        block.append_instruction(op.instruction);
    }
}

} // namespace sysycc
