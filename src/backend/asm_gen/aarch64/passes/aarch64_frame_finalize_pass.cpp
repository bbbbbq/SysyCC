#include "backend/asm_gen/aarch64/passes/aarch64_frame_finalize_pass.hpp"

#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

namespace {

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

void allocate_saved_physical_reg_slots(AArch64MachineFunction &function) {
    std::size_t local_size = function.get_frame_info().get_local_size();
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        if (function.get_frame_info().has_saved_physical_reg_offset(reg)) {
            continue;
        }
        local_size = align_to(local_size, 8);
        local_size += 8;
        function.get_frame_info().set_saved_physical_reg_offset(reg, local_size);
    }
    function.get_frame_info().set_local_size(local_size);
    function.get_frame_info().set_frame_size(align_to(local_size, 16));
}

void append_saved_reg_store(std::vector<AArch64MachineInstr> &instructions,
                            unsigned physical_reg, std::size_t offset) {
    append_physical_frame_store(
        instructions, physical_reg,
        is_float_physical_reg(physical_reg) ? AArch64VirtualRegKind::Float64
                                            : AArch64VirtualRegKind::General64,
        8, offset, static_cast<unsigned>(AArch64PhysicalReg::X9));
}

void append_saved_reg_load(std::vector<AArch64MachineInstr> &instructions,
                           unsigned physical_reg, std::size_t offset) {
    append_physical_frame_load(
        instructions, physical_reg,
        is_float_physical_reg(physical_reg) ? AArch64VirtualRegKind::Float64
                                            : AArch64VirtualRegKind::General64,
        8, offset, static_cast<unsigned>(AArch64PhysicalReg::X9));
}

bool is_real_machine_instruction(const AArch64MachineInstr &instruction) {
    return instruction.get_mnemonic().empty() ||
           instruction.get_mnemonic().front() != '.';
}

std::size_t count_real_instruction_bytes(
    const std::vector<AArch64MachineInstr> &instructions) {
    std::size_t bytes = 0;
    for (const AArch64MachineInstr &instruction : instructions) {
        if (is_real_machine_instruction(instruction)) {
            bytes += 4;
        }
    }
    return bytes;
}

void append_frame_record_directive_at(AArch64FrameRecord &frame_record,
                                      std::size_t code_offset,
                                      AArch64CfiDirective directive) {
    directive.code_offset = code_offset;
    frame_record.append_cfi_directive(std::move(directive));
}

} // namespace

void AArch64FrameFinalizePass::run(AArch64MachineFunction &function) const {
    if (function.get_blocks().empty()) {
        return;
    }
    allocate_saved_physical_reg_slots(function);

    const std::vector<AArch64MachineInstr> existing_prologue =
        function.get_blocks().front().get_instructions();
    const std::size_t prologue_prefix_size =
        count_aarch64_standard_prologue_prefix(existing_prologue);

    std::vector<AArch64MachineInstr> prologue;
    const std::size_t frame_size = function.get_frame_info().get_frame_size();
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_prologue_shell(frame_size)) {
        prologue.push_back(op.instruction);
    }
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        append_saved_reg_store(prologue, reg,
                               function.get_frame_info().get_saved_physical_reg_offset(reg));
    }
    for (std::size_t index = prologue_prefix_size; index < existing_prologue.size();
         ++index) {
        prologue.push_back(existing_prologue[index]);
    }
    function.get_blocks().front().get_instructions() = std::move(prologue);

    std::vector<AArch64MachineInstr> epilogue;
    for (auto it = function.get_frame_info().get_saved_physical_regs().rbegin();
         it != function.get_frame_info().get_saved_physical_regs().rend(); ++it) {
        append_saved_reg_load(epilogue, *it,
                              function.get_frame_info().get_saved_physical_reg_offset(*it));
    }
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_epilogue_shell(frame_size)) {
        epilogue.push_back(op.instruction);
    }
    function.get_blocks().back().get_instructions() = std::move(epilogue);

    std::size_t epilogue_start_offset = 0;
    if (function.get_blocks().size() > 1) {
        for (std::size_t index = 0; index + 1 < function.get_blocks().size(); ++index) {
            epilogue_start_offset += count_real_instruction_bytes(
                function.get_blocks()[index].get_instructions());
        }
    }

    AArch64FrameRecord &frame_record = function.get_frame_record();
    frame_record.set_stack_frame_size(frame_size);
    frame_record.clear_cfi_directives();
    append_frame_record_directive_at(
        frame_record, 0,
        AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::StartProcedure});

    std::size_t current_code_offset = 0;
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_prologue_shell(frame_size)) {
        current_code_offset += 4;
        for (const AArch64CfiDirective &directive :
             build_aarch64_standard_shell_cfi_bundle(op.kind, frame_size)
                 .frame_record_directives) {
            append_frame_record_directive_at(frame_record, current_code_offset,
                                             directive);
        }
    }
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        current_code_offset += 4;
        append_frame_record_directive_at(
            frame_record, current_code_offset,
            AArch64CfiDirective{
                .kind = AArch64CfiDirectiveKind::Offset,
                .reg = dwarf_register_number(reg),
                .offset = -static_cast<long long>(
                    16 + function.get_frame_info().get_saved_physical_reg_offset(reg))});
    }

    current_code_offset = epilogue_start_offset;
    for (auto it = function.get_frame_info().get_saved_physical_regs().rbegin();
         it != function.get_frame_info().get_saved_physical_regs().rend(); ++it) {
        current_code_offset += 4;
        append_frame_record_directive_at(
            frame_record, current_code_offset,
            AArch64CfiDirective{
                .kind = AArch64CfiDirectiveKind::Restore,
                .reg = dwarf_register_number(*it)});
    }
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_epilogue_shell(frame_size)) {
        current_code_offset += 4;
        for (const AArch64CfiDirective &directive :
             build_aarch64_standard_shell_cfi_bundle(op.kind, frame_size)
                 .frame_record_directives) {
            append_frame_record_directive_at(frame_record, current_code_offset,
                                             directive);
        }
    }
    append_frame_record_directive_at(
        frame_record, current_code_offset,
        AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::EndProcedure});
}

} // namespace sysycc
