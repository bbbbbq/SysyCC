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
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfa,
                            static_cast<unsigned>(AArch64PhysicalReg::X29), 16});
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                            static_cast<unsigned>(AArch64PhysicalReg::X29), -16});
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                            static_cast<unsigned>(AArch64PhysicalReg::X30), -8});
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaRegister,
                            static_cast<unsigned>(AArch64PhysicalReg::X29), 0});
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaOffset,
                            static_cast<unsigned>(AArch64PhysicalReg::X29),
                            to_signed_offset(frame_size + 16)});
    function.get_frame_record().append_cfi_directive(
        AArch64CfiDirective{AArch64CfiDirectiveKind::EndProcedure});
}

void append_aarch64_standard_prologue(AArch64MachineBlock &block,
                                      std::size_t frame_size) {
    block.append_instruction("stp x29, x30, [sp, #-16]!");
    block.append_instruction("mov x29, sp");
    if (frame_size > 0) {
        block.append_instruction("sub sp, sp, #" + std::to_string(frame_size));
    }
}

void append_aarch64_standard_epilogue(AArch64MachineBlock &block,
                                      std::size_t frame_size) {
    if (frame_size > 0) {
        block.append_instruction("add sp, sp, #" + std::to_string(frame_size));
    }
    block.append_instruction("ldp x29, x30, [sp], #16");
    block.append_instruction("ret");
}

} // namespace sysycc
