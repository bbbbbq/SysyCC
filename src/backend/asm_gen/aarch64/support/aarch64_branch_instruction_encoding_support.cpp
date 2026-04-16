#include "backend/asm_gen/aarch64/support/aarch64_branch_instruction_encoding_support.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

std::optional<long long> resolve_branch_delta(const std::string &label,
                                              std::size_t pc_offset,
                                              const FunctionScanInfo &scan_info,
                                              DiagnosticEngine &diagnostic_engine,
                                              const std::string &context) {
    const auto it = scan_info.label_offsets.find(label);
    if (it == scan_info.label_offsets.end()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer: unknown branch label '" + label +
                "' in " + context);
        return std::nullopt;
    }
    return static_cast<long long>(it->second) - static_cast<long long>(pc_offset);
}

bool check_signed_range(long long value, long long min_value, long long max_value,
                        DiagnosticEngine &diagnostic_engine,
                        const std::string &message) {
    if (value < min_value || value > max_value) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
        return false;
    }
    return true;
}

std::uint32_t encode_unconditional_branch_word(std::uint32_t base,
                                               long long delta) {
    const std::uint32_t imm26 =
        static_cast<std::uint32_t>((delta >> 2) & 0x03ffffffU);
    return base | imm26;
}

std::uint32_t encode_conditional_branch_word(unsigned condition_bits,
                                             long long delta) {
    const std::uint32_t imm19 =
        static_cast<std::uint32_t>((delta >> 2) & 0x7ffffU);
    return 0x54000000U | (imm19 << 5) | (condition_bits & 0xfU);
}

std::uint32_t encode_compare_branch_word(std::uint32_t base, unsigned rt,
                                         long long delta) {
    const std::uint32_t imm19 =
        static_cast<std::uint32_t>((delta >> 2) & 0x7ffffU);
    return base | (imm19 << 5) | (rt & 0x1fU);
}

unsigned encode_condition_code(AArch64ConditionCode code) {
    switch (code) {
    case AArch64ConditionCode::Eq:
        return 0;
    case AArch64ConditionCode::Ne:
        return 1;
    case AArch64ConditionCode::Hs:
        return 2;
    case AArch64ConditionCode::Lo:
        return 3;
    case AArch64ConditionCode::Mi:
        return 4;
    case AArch64ConditionCode::Pl:
        return 5;
    case AArch64ConditionCode::Vs:
        return 6;
    case AArch64ConditionCode::Vc:
        return 7;
    case AArch64ConditionCode::Hi:
        return 8;
    case AArch64ConditionCode::Ls:
        return 9;
    case AArch64ConditionCode::Ge:
        return 10;
    case AArch64ConditionCode::Lt:
        return 11;
    case AArch64ConditionCode::Gt:
        return 12;
    case AArch64ConditionCode::Le:
        return 13;
    case AArch64ConditionCode::Al:
        return 14;
    }
    return 0xffU;
}

} // namespace

std::optional<EncodedInstruction> encode_branch_family_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine,
    const AArch64BranchInstructionEncodingContext &context) {
    EncodedInstruction encoded;
    const auto unsupported = [&](const std::string &detail)
        -> std::optional<EncodedInstruction> {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer: unsupported instruction '" +
                instruction.get_mnemonic() + "' (" + detail + ")");
        return std::nullopt;
    };

    const AArch64MachineOpcode opcode = instruction.get_opcode();
    const std::string &mnemonic = instruction.get_mnemonic();
    const auto &operands = instruction.get_operands();

    if (opcode == AArch64MachineOpcode::Branch ||
        opcode == AArch64MachineOpcode::BranchLink) {
        if (operands.size() != 1) {
            return unsupported("branch operand shape");
        }
        const std::uint32_t base =
            opcode == AArch64MachineOpcode::BranchLink ? 0x94000000U
                                                       : 0x14000000U;
        if (const auto *label = operands[0].get_label_operand(); label != nullptr) {
            const auto delta = resolve_branch_delta(label->label_text, pc_offset,
                                                    scan_info, diagnostic_engine,
                                                    mnemonic);
            if (!delta.has_value() || (*delta % 4) != 0 ||
                !check_signed_range(*delta >> 2, -(1 << 25), (1 << 25) - 1,
                                    diagnostic_engine,
                                    "AArch64 direct object writer: branch target out of range")) {
                return std::nullopt;
            }
            encoded.word = encode_unconditional_branch_word(base, *delta);
            return encoded;
        }
        const auto symbol =
            context.get_symbol_reference_operand == nullptr
                ? std::optional<AArch64MachineSymbolReference>{}
                : context.get_symbol_reference_operand(operands[0]);
        if (!symbol.has_value()) {
            return unsupported("branch target operand");
        }
        encoded.word = base;
        encoded.relocations.push_back(AArch64RelocationRecord{
            opcode == AArch64MachineOpcode::BranchLink
                ? AArch64RelocationKind::Call26
                : AArch64RelocationKind::Branch26,
            symbol->target, pc_offset});
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::BranchRegister ||
        opcode == AArch64MachineOpcode::BranchLinkRegister) {
        if (operands.size() != 1) {
            return unsupported(opcode == AArch64MachineOpcode::BranchRegister
                                   ? "indirect branch operand shape"
                                   : "indirect branch-link operand shape");
        }
        const auto rn =
            context.resolve_general_reg_operand == nullptr
                ? std::optional<EncodedGeneralReg>{}
                : context.resolve_general_reg_operand(
                      operands[0], function, false, false, diagnostic_engine,
                      opcode == AArch64MachineOpcode::BranchRegister ? "br"
                                                                     : "blr");
        if (!rn.has_value()) {
            return std::nullopt;
        }
        encoded.word =
            (opcode == AArch64MachineOpcode::BranchRegister ? 0xD61F0000U
                                                            : 0xD63F0000U) |
            ((rn->code & 0x1fU) << 5);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::BranchConditional) {
        if (operands.size() != 1 || operands[0].get_label_operand() == nullptr) {
            return unsupported("conditional branch operand shape");
        }
        const auto cond = parse_aarch64_condition_code(mnemonic.substr(2));
        if (!cond.has_value()) {
            return unsupported("conditional branch condition");
        }
        const auto delta = resolve_branch_delta(
            operands[0].get_label_operand()->label_text, pc_offset, scan_info,
            diagnostic_engine, mnemonic);
        if (!delta.has_value() || (*delta % 4) != 0 ||
            !check_signed_range(*delta >> 2, -(1 << 18), (1 << 18) - 1,
                                diagnostic_engine,
                                "AArch64 direct object writer: conditional branch target out of range")) {
            return std::nullopt;
        }
        encoded.word = encode_conditional_branch_word(
            encode_condition_code(*cond), *delta);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::CompareBranchZero ||
        opcode == AArch64MachineOpcode::CompareBranchNonZero) {
        if (operands.size() != 2 || operands[1].get_label_operand() == nullptr) {
            return unsupported("compare-and-branch operand shape");
        }
        const auto rt =
            context.resolve_general_reg_operand == nullptr
                ? std::optional<EncodedGeneralReg>{}
                : context.resolve_general_reg_operand(
                      operands[0], function, false, false, diagnostic_engine,
                      mnemonic + " value");
        if (!rt.has_value()) {
            return std::nullopt;
        }
        const auto delta = resolve_branch_delta(
            operands[1].get_label_operand()->label_text, pc_offset, scan_info,
            diagnostic_engine, mnemonic);
        if (!delta.has_value() || (*delta % 4) != 0 ||
            !check_signed_range(*delta >> 2, -(1 << 18), (1 << 18) - 1,
                                diagnostic_engine,
                                "AArch64 direct object writer: compare-and-branch target out of range")) {
            return std::nullopt;
        }
        std::uint32_t base = 0x34000000U;
        if (opcode == AArch64MachineOpcode::CompareBranchZero) {
            base = rt->use_64bit ? 0xB4000000U : 0x34000000U;
        } else {
            base = rt->use_64bit ? 0xB5000000U : 0x35000000U;
        }
        encoded.word = encode_compare_branch_word(base, rt->code, *delta);
        return encoded;
    }

    return std::nullopt;
}

} // namespace sysycc
