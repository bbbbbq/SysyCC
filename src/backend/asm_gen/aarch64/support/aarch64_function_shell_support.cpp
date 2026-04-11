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

bool same_symbol_descriptor(const AArch64SymbolDescriptor &actual,
                            const AArch64SymbolDescriptor &expected) {
    return actual.name == expected.name && actual.kind == expected.kind &&
           actual.section_kind == expected.section_kind &&
           actual.binding == expected.binding &&
           actual.is_defined == expected.is_defined;
}

bool same_symbol_reference(const AArch64SymbolReference &actual,
                           const AArch64SymbolReference &expected) {
    return same_symbol_descriptor(actual.symbol, expected.symbol) &&
           actual.addend == expected.addend;
}

bool same_machine_symbol_reference(const AArch64MachineSymbolReference &actual,
                                   const AArch64MachineSymbolReference &expected) {
    return same_symbol_reference(actual.target, expected.target) &&
           actual.modifier == expected.modifier;
}

AArch64MachineInstr cfi_instruction(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands = {}) {
    return AArch64MachineInstr(std::move(mnemonic), std::move(operands));
}

bool has_exact_instruction_shape(const AArch64MachineInstr &instruction,
                                 const AArch64MachineInstr &expected) {
    if (instruction.get_mnemonic() != expected.get_mnemonic() ||
        instruction.get_operands().size() != expected.get_operands().size()) {
        return false;
    }
    for (std::size_t index = 0; index < instruction.get_operands().size(); ++index) {
        const AArch64MachineOperand &actual_operand = instruction.get_operands()[index];
        const AArch64MachineOperand &expected_operand = expected.get_operands()[index];
        if (actual_operand.get_kind() != expected_operand.get_kind()) {
            return false;
        }
        if (const auto *actual_virtual = actual_operand.get_virtual_reg_operand();
            actual_virtual != nullptr) {
            const auto *expected_virtual = expected_operand.get_virtual_reg_operand();
            if (expected_virtual == nullptr ||
                actual_virtual->reg.get_id() != expected_virtual->reg.get_id() ||
                actual_virtual->reg.get_kind() != expected_virtual->reg.get_kind() ||
                actual_virtual->is_def != expected_virtual->is_def) {
                return false;
            }
            continue;
        }
        if (const auto *actual_physical = actual_operand.get_physical_reg_operand();
            actual_physical != nullptr) {
            const auto *expected_physical = expected_operand.get_physical_reg_operand();
            if (expected_physical == nullptr ||
                actual_physical->reg_number != expected_physical->reg_number ||
                actual_physical->kind != expected_physical->kind) {
                return false;
            }
            continue;
        }
        if (const auto *actual_immediate = actual_operand.get_immediate_operand();
            actual_immediate != nullptr) {
            const auto *expected_immediate = expected_operand.get_immediate_operand();
            if (expected_immediate == nullptr ||
                actual_immediate->asm_text != expected_immediate->asm_text) {
                return false;
            }
            continue;
        }
        if (const auto *actual_symbol = actual_operand.get_symbol_operand();
            actual_symbol != nullptr) {
            const auto *expected_symbol = expected_operand.get_symbol_operand();
            if (expected_symbol == nullptr ||
                !same_machine_symbol_reference(actual_symbol->reference,
                                               expected_symbol->reference)) {
                return false;
            }
            continue;
        }
        if (const auto *actual_label = actual_operand.get_label_operand();
            actual_label != nullptr) {
            const auto *expected_label = expected_operand.get_label_operand();
            if (expected_label == nullptr ||
                actual_label->label_text != expected_label->label_text) {
                return false;
            }
            continue;
        }
        if (const auto *actual_condition = actual_operand.get_condition_code_operand();
            actual_condition != nullptr) {
            const auto *expected_condition =
                expected_operand.get_condition_code_operand();
            if (expected_condition == nullptr ||
                actual_condition->code != expected_condition->code) {
                return false;
            }
            continue;
        }
        if (const auto *actual_zero = actual_operand.get_zero_register_operand();
            actual_zero != nullptr) {
            const auto *expected_zero = expected_operand.get_zero_register_operand();
            if (expected_zero == nullptr ||
                actual_zero->use_64bit != expected_zero->use_64bit) {
                return false;
            }
            continue;
        }
        if (const auto *actual_shift = actual_operand.get_shift_operand();
            actual_shift != nullptr) {
            const auto *expected_shift = expected_operand.get_shift_operand();
            if (expected_shift == nullptr ||
                actual_shift->kind != expected_shift->kind ||
                actual_shift->amount != expected_shift->amount) {
                return false;
            }
            continue;
        }
        if (const auto *actual_stack = actual_operand.get_stack_pointer_operand();
            actual_stack != nullptr) {
            const auto *expected_stack = expected_operand.get_stack_pointer_operand();
            if (expected_stack == nullptr ||
                actual_stack->use_64bit != expected_stack->use_64bit) {
                return false;
            }
            continue;
        }
        if (const auto *actual_memory = actual_operand.get_memory_address_operand();
            actual_memory != nullptr) {
            const auto *expected_memory = expected_operand.get_memory_address_operand();
            if (expected_memory == nullptr ||
                actual_memory->base_kind != expected_memory->base_kind ||
                actual_memory->virtual_reg.get_id() !=
                    expected_memory->virtual_reg.get_id() ||
                actual_memory->virtual_reg.get_kind() !=
                    expected_memory->virtual_reg.get_kind() ||
                actual_memory->physical_reg != expected_memory->physical_reg ||
                actual_memory->stack_pointer_use_64bit !=
                    expected_memory->stack_pointer_use_64bit ||
                actual_memory->address_mode != expected_memory->address_mode) {
                return false;
            }
            if (actual_memory->get_immediate_offset() !=
                expected_memory->get_immediate_offset()) {
                return false;
            }
            const AArch64MachineSymbolReference *actual_symbolic =
                actual_memory->get_symbolic_offset();
            const AArch64MachineSymbolReference *expected_symbolic =
                expected_memory->get_symbolic_offset();
            if ((actual_symbolic == nullptr) != (expected_symbolic == nullptr) ||
                (actual_symbolic != nullptr &&
                 !same_machine_symbol_reference(*actual_symbolic,
                                               *expected_symbolic))) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

bool is_stack_pointer_operand(const AArch64MachineOperand &operand) {
    if (const auto *stack_pointer = operand.get_stack_pointer_operand();
        stack_pointer != nullptr) {
        return stack_pointer->use_64bit;
    }
    return false;
}

bool is_frame_adjust_instruction(const AArch64MachineInstr &instruction,
                                 const char *mnemonic) {
    if (instruction.get_mnemonic() != mnemonic ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    const auto *immediate = instruction.get_operands()[2].get_immediate_operand();
    return is_stack_pointer_operand(instruction.get_operands()[0]) &&
           is_stack_pointer_operand(instruction.get_operands()[1]) &&
           immediate != nullptr && !immediate->asm_text.empty() &&
           immediate->asm_text.front() == '#';
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
        AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::StartProcedure});
    // This frame record seeds shell-only CFI from the same shared shell semantics
    // that both lowering and frame-finalize consume. Final emitted unwind text is
    // still authoritative in frame-finalize once saved-register slots are known.
    for (const AArch64StandardFrameShellOp &op :
         build_aarch64_standard_prologue_shell(frame_size)) {
        append_aarch64_frame_record_cfi_for_shell_op(function.get_frame_record(),
                                                     op.kind, frame_size);
    }
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::EndProcedure});
}

std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_prologue_shell(std::size_t frame_size) {
    std::vector<AArch64StandardFrameShellOp> shell;
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::SaveFrameRecord,
        AArch64MachineInstr(
            "stp",
            {AArch64MachineOperand::physical_reg(
                 static_cast<unsigned>(AArch64PhysicalReg::X29),
                 AArch64VirtualRegKind::General64),
             AArch64MachineOperand::physical_reg(
                 static_cast<unsigned>(AArch64PhysicalReg::X30),
                 AArch64VirtualRegKind::General64),
             AArch64MachineOperand::memory_address_stack_pointer(
                 -16, true,
                 AArch64MachineMemoryAddressOperand::AddressMode::PreIndex)})});
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::EstablishFramePointer,
        AArch64MachineInstr(
            "mov",
            {AArch64MachineOperand::physical_reg(
                 static_cast<unsigned>(AArch64PhysicalReg::X29),
                 AArch64VirtualRegKind::General64),
             AArch64MachineOperand::stack_pointer(true)})});
    if (frame_size > 0) {
        shell.push_back(AArch64StandardFrameShellOp{
            AArch64StandardFrameShellOpKind::AllocateLocalFrame,
            AArch64MachineInstr(
                "sub",
                {AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::immediate("#" +
                                                  std::to_string(frame_size))})});
    }
    return shell;
}

std::vector<AArch64StandardFrameShellOp>
build_aarch64_standard_epilogue_shell(std::size_t frame_size) {
    std::vector<AArch64StandardFrameShellOp> shell;
    if (frame_size > 0) {
        shell.push_back(AArch64StandardFrameShellOp{
            AArch64StandardFrameShellOpKind::DeallocateLocalFrame,
            AArch64MachineInstr(
                "add",
                {AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::immediate("#" +
                                                  std::to_string(frame_size))})});
    }
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::RestoreFrameRecord,
        AArch64MachineInstr(
            "ldp",
            {AArch64MachineOperand::physical_reg(
                 static_cast<unsigned>(AArch64PhysicalReg::X29),
                 AArch64VirtualRegKind::General64),
             AArch64MachineOperand::physical_reg(
                 static_cast<unsigned>(AArch64PhysicalReg::X30),
                 AArch64VirtualRegKind::General64),
             AArch64MachineOperand::memory_address_stack_pointer(
                 16, true,
                 AArch64MachineMemoryAddressOperand::AddressMode::PostIndex)})});
    shell.push_back(AArch64StandardFrameShellOp{
        AArch64StandardFrameShellOpKind::Return, AArch64MachineInstr("ret", {})});
    return shell;
}

AArch64StandardFrameShellCfiBundle
build_aarch64_standard_shell_cfi_bundle(AArch64StandardFrameShellOpKind op_kind,
                                        std::size_t frame_size) {
    AArch64StandardFrameShellCfiBundle bundle;
    switch (op_kind) {
    case AArch64StandardFrameShellOpKind::SaveFrameRecord:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::DefCfa,
                                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29),
                                .offset = 16});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::Offset,
                                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29),
                                .offset = -16});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::Offset,
                                .reg = static_cast<unsigned>(AArch64PhysicalReg::X30),
                                .offset = -8});
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_def_cfa_offset",
                            {AArch64MachineOperand::immediate("16")}));
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_offset",
                            {AArch64MachineOperand::immediate("29"),
                             AArch64MachineOperand::immediate("-16")}));
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_offset",
                            {AArch64MachineOperand::immediate("30"),
                             AArch64MachineOperand::immediate("-8")}));
        break;
    case AArch64StandardFrameShellOpKind::EstablishFramePointer:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{
                .kind = AArch64CfiDirectiveKind::DefCfaRegister,
                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29)});
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_def_cfa_register",
                            {AArch64MachineOperand::immediate("29")}));
        break;
    case AArch64StandardFrameShellOpKind::AllocateLocalFrame:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{
                .kind = AArch64CfiDirectiveKind::DefCfaOffset,
                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29),
                .offset = to_signed_offset(frame_size + 16)});
        bundle.asm_instructions.push_back(cfi_instruction(
            ".cfi_def_cfa_offset",
            {AArch64MachineOperand::immediate(std::to_string(frame_size + 16))}));
        break;
    case AArch64StandardFrameShellOpKind::DeallocateLocalFrame:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{
                .kind = AArch64CfiDirectiveKind::DefCfaOffset,
                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29),
                .offset = 16});
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_def_cfa_offset",
                            {AArch64MachineOperand::immediate("16")}));
        break;
    case AArch64StandardFrameShellOpKind::RestoreFrameRecord:
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::Restore,
                                .reg = static_cast<unsigned>(AArch64PhysicalReg::X29)});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::Restore,
                                .reg = static_cast<unsigned>(AArch64PhysicalReg::X30)});
        bundle.frame_record_directives.push_back(
            AArch64CfiDirective{.kind = AArch64CfiDirectiveKind::DefCfa,
                                .reg = 31,
                                .offset = 0});
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_restore",
                            {AArch64MachineOperand::immediate("29")}));
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_restore",
                            {AArch64MachineOperand::immediate("30")}));
        bundle.asm_instructions.push_back(
            cfi_instruction(".cfi_def_cfa",
                            {AArch64MachineOperand::stack_pointer(true),
                             AArch64MachineOperand::immediate("0")}));
        break;
    case AArch64StandardFrameShellOpKind::Return:
        bundle.asm_instructions.push_back(cfi_instruction(".cfi_endproc"));
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
