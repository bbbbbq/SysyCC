#include "backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.hpp"

#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_branch_instruction_encoding_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_fp_instruction_encoding_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_encoding_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

bool starts_with(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::optional<long long> parse_signed_immediate_text(const std::string &text) {
    std::string trimmed;
    trimmed.reserve(text.size());
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            trimmed.push_back(ch);
        }
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }
    if (trimmed.front() == '#') {
        trimmed.erase(trimmed.begin());
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        const long long value = std::stoll(trimmed, &parsed, 0);
        if (parsed != trimmed.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<unsigned> parse_unsigned_immediate_text(const std::string &text) {
    const auto value = parse_signed_immediate_text(text);
    if (!value.has_value() || *value < 0 ||
        *value > static_cast<long long>(std::numeric_limits<unsigned>::max())) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*value);
}

std::optional<AArch64MachineSymbolReference>
get_symbol_reference_operand(const AArch64MachineOperand &operand) {
    const auto *symbol = operand.get_symbol_operand();
    if (symbol == nullptr) {
        return std::nullopt;
    }
    return symbol->reference;
}

std::optional<EncodedGeneralReg>
resolve_general_reg_operand(const AArch64MachineOperand &operand,
                            const AArch64MachineFunction &function,
                            bool allow_stack_pointer, bool allow_zero_register,
                            DiagnosticEngine &diagnostic_engine,
                            const std::string &context) {
    if (const auto *stack_pointer = operand.get_stack_pointer_operand();
        stack_pointer != nullptr) {
        if (!allow_stack_pointer) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected stack pointer in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{31, stack_pointer->use_64bit, true, false};
    }
    if (const auto *zero_register = operand.get_zero_register_operand();
        zero_register != nullptr) {
        if (!allow_zero_register) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected zero register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{31, zero_register->use_64bit, false, true};
    }
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        if (is_float_physical_reg(physical_reg->reg_number)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{physical_reg->reg_number,
                                 uses_general_64bit_register(physical_reg->kind),
                                 false, false};
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (!virtual_reg->reg.is_general()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved virtual register in " +
                    context);
            return std::nullopt;
        }
        if (is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: virtual register resolved to floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{*physical_reg, virtual_reg->reg.get_use_64bit(),
                                 false, false};
    }

    diagnostic_engine.add_error(
        DiagnosticStage::Compiler,
        "AArch64 direct object writer: expected general register operand in " +
            context);
    return std::nullopt;
}

std::optional<EncodedFloatReg>
resolve_float_reg_operand(const AArch64MachineOperand &operand,
                          const AArch64MachineFunction &function,
                          DiagnosticEngine &diagnostic_engine,
                          const std::string &context) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        if (!is_float_physical_reg(physical_reg->reg_number)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: expected floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedFloatReg{
            physical_reg->reg_number -
                static_cast<unsigned>(AArch64PhysicalReg::V0),
            physical_reg->kind};
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (!virtual_reg->reg.is_floating_point()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: expected floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        if (!is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point virtual register resolved to general register in " +
                    context);
            return std::nullopt;
        }
        return EncodedFloatReg{*physical_reg -
                                   static_cast<unsigned>(AArch64PhysicalReg::V0),
                               virtual_reg->reg.get_kind()};
    }

    diagnostic_engine.add_error(
        DiagnosticStage::Compiler,
        "AArch64 direct object writer: expected floating-point register operand in " +
            context);
    return std::nullopt;
}

bool operand_is_float_reg_like(const AArch64MachineOperand &operand) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        return is_float_physical_reg(physical_reg->reg_number);
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return virtual_reg->reg.is_floating_point();
    }
    return false;
}

bool encode_fp_reg_reg(std::uint32_t base, unsigned rd, unsigned rn,
                       EncodedInstruction &encoded) {
    encoded.word = base | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
    return true;
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

unsigned invert_condition_code(unsigned code) { return code ^ 1U; }

std::optional<long long>
parse_operand_immediate(const AArch64MachineOperand &operand) {
    const auto *immediate = operand.get_immediate_operand();
    if (immediate == nullptr) {
        return std::nullopt;
    }
    return parse_signed_immediate_text(immediate->asm_text);
}

std::optional<unsigned> parse_shift_amount(const AArch64MachineOperand &operand,
                                           AArch64ShiftKind &kind) {
    const auto *shift = operand.get_shift_operand();
    if (shift == nullptr) {
        return std::nullopt;
    }
    kind = shift->kind;
    return shift->amount;
}

unsigned shift_type_bits(AArch64ShiftKind kind) {
    if (kind == AArch64ShiftKind::Lsl)
        return 0;
    if (kind == AArch64ShiftKind::Lsr)
        return 1;
    if (kind == AArch64ShiftKind::Asr)
        return 2;
    return 0xffU;
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

std::uint32_t encode_add_sub_immediate_word(bool is_sub, bool use_64bit,
                                            unsigned rd, unsigned rn,
                                            unsigned imm12, unsigned shift12) {
    const std::uint32_t base =
        use_64bit ? (is_sub ? 0xD1000000U : 0x91000000U)
                  : (is_sub ? 0x51000000U : 0x11000000U);
    return base | ((shift12 & 0x1U) << 22) | ((imm12 & 0xfffU) << 10) |
           ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_add_sub_register_word(bool is_sub, bool use_64bit,
                                           unsigned rd, unsigned rn,
                                           unsigned rm, unsigned shift_type,
                                           unsigned amount) {
    const std::uint32_t base =
        use_64bit ? (is_sub ? 0xCB000000U : 0x8B000000U)
                  : (is_sub ? 0x4B000000U : 0x0B000000U);
    return base | ((rm & 0x1fU) << 16) | ((shift_type & 0x3U) << 22) |
           ((amount & 0x3fU) << 10) | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_logical_register_word(std::uint32_t base, unsigned rd,
                                           unsigned rn, unsigned rm,
                                           unsigned shift_type, unsigned amount) {
    return base | ((rm & 0x1fU) << 16) | ((shift_type & 0x3U) << 22) |
           ((amount & 0x3fU) << 10) | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_wide_move_word(std::uint32_t base, unsigned rd,
                                    unsigned imm16, unsigned hw) {
    return base | ((hw & 0x3U) << 21) | ((imm16 & 0xffffU) << 5) |
           (rd & 0x1fU);
}

std::uint32_t encode_load_store_unsigned_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, unsigned imm12) {
    return base | ((imm12 & 0xfffU) << 10) | ((rn & 0x1fU) << 5) |
           (rt & 0x1fU);
}

std::uint32_t encode_load_store_unscaled_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, long long imm9) {
    return base | ((static_cast<std::uint32_t>(imm9) & 0x1ffU) << 12) |
           ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

std::uint32_t encode_pair_word(std::uint32_t base, unsigned rt, unsigned rt2,
                               unsigned rn, long long scaled_imm7) {
    return base | ((static_cast<std::uint32_t>(scaled_imm7) & 0x7fU) << 15) |
           ((rt2 & 0x1fU) << 10) | ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

} // namespace

bool is_real_text_instruction(const AArch64MachineInstr &instruction) noexcept {
    return !instruction.is_asm_directive();
}

bool is_loc_instruction(const AArch64MachineInstr &instruction) noexcept {
    return instruction.get_opcode() == AArch64MachineOpcode::DirectiveLoc;
}

bool is_cfi_instruction(const AArch64MachineInstr &instruction) noexcept {
    return instruction.get_opcode() == AArch64MachineOpcode::DirectiveCfi ||
           starts_with(instruction.get_mnemonic(), ".cfi_");
}

FunctionScanInfo scan_function_layout(const AArch64MachineFunction &function,
                                      DiagnosticEngine &diagnostic_engine) {
    (void)diagnostic_engine;
    FunctionScanInfo info;
    std::size_t current_pc = 0;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        info.label_offsets[block.get_label()] = current_pc;
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            if (is_real_text_instruction(instruction)) {
                if (instruction.get_debug_location().has_value()) {
                    info.source_locations.push_back(SourceLocationNote{
                        current_pc, instruction.get_debug_location()->file_id,
                        instruction.get_debug_location()->line,
                        instruction.get_debug_location()->column});
                }
                current_pc += 4;
            }
        }
    }
    info.code_size = current_pc;
    return info;
}

std::optional<EncodedInstruction> encode_machine_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine) {
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
        opcode == AArch64MachineOpcode::BranchLink ||
        opcode == AArch64MachineOpcode::BranchRegister ||
        opcode == AArch64MachineOpcode::BranchLinkRegister ||
        opcode == AArch64MachineOpcode::BranchConditional ||
        opcode == AArch64MachineOpcode::CompareBranchZero ||
        opcode == AArch64MachineOpcode::CompareBranchNonZero) {
        return encode_branch_family_instruction(
            instruction, function, scan_info, pc_offset, diagnostic_engine,
            AArch64BranchInstructionEncodingContext{
                .resolve_general_reg_operand =
                    [&](const AArch64MachineOperand &operand,
                        const AArch64MachineFunction &encoded_function,
                        bool allow_stack_pointer, bool allow_zero_register,
                        DiagnosticEngine &encoded_diagnostics,
                        const std::string &context)
                    -> std::optional<EncodedGeneralReg> {
                    return resolve_general_reg_operand(
                        operand, encoded_function, allow_stack_pointer,
                        allow_zero_register, encoded_diagnostics, context);
                },
                .get_symbol_reference_operand =
                    [&](const AArch64MachineOperand &operand)
                    -> std::optional<AArch64MachineSymbolReference> {
                    return get_symbol_reference_operand(operand);
                }});
    }

    if (opcode == AArch64MachineOpcode::Return) {
        unsigned reg = 30;
        if (!operands.empty()) {
            const auto rn = resolve_general_reg_operand(
                operands[0], function, false, false, diagnostic_engine, "ret");
            if (!rn.has_value()) {
                return std::nullopt;
            }
            reg = rn->code;
        }
        encoded.word = 0xD65F0000U | ((reg & 0x1fU) << 5);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::MoveWideZero ||
        opcode == AArch64MachineOpcode::MoveWideKeep) {
        if (operands.size() < 2) {
            return unsupported("wide move operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic);
        const auto imm = parse_operand_immediate(operands[1]);
        if (!rd.has_value() || !imm.has_value() ||
            !check_signed_range(*imm, 0, 0xffff, diagnostic_engine,
                                "AArch64 direct object writer: wide move immediate out of range")) {
            return std::nullopt;
        }
        unsigned hw = 0;
        if (operands.size() >= 3) {
            AArch64ShiftKind shift_kind = AArch64ShiftKind::Lsl;
            const auto amount = parse_shift_amount(operands[2], shift_kind);
            if (!amount.has_value() || shift_kind != AArch64ShiftKind::Lsl ||
                (*amount % 16) != 0 ||
                *amount > 48) {
                return unsupported("wide move shift");
            }
            hw = *amount / 16;
        }
        encoded.word = encode_wide_move_word(
            rd->use_64bit
                ? (opcode == AArch64MachineOpcode::MoveWideKeep ? 0xF2800000U
                                                               : 0xD2800000U)
                : (opcode == AArch64MachineOpcode::MoveWideKeep ? 0x72800000U
                                                               : 0x52800000U),
            rd->code, static_cast<unsigned>(*imm), hw);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::Move) {
        if (operands.size() != 2) {
            return unsupported("mov operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, true, false, diagnostic_engine, "mov dst");
        if (!rd.has_value()) {
            return std::nullopt;
        }
        if (operands[1].get_stack_pointer_operand() != nullptr || rd->is_stack_pointer) {
            const auto rn = resolve_general_reg_operand(
                operands[1], function, true, false, diagnostic_engine, "mov src");
            if (!rn.has_value() || rd->use_64bit != rn->use_64bit) {
                return std::nullopt;
            }
            encoded.word = encode_add_sub_immediate_word(false, rd->use_64bit,
                                                         rd->code, rn->code, 0, 0);
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[1], function, false, true, diagnostic_engine, "mov src");
        if (!rm.has_value() || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        encoded.word = encode_logical_register_word(
            rd->use_64bit ? 0xAA000000U : 0x2A000000U, rd->code, 31, rm->code, 0,
            0);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::Add ||
        opcode == AArch64MachineOpcode::Sub) {
        if (operands.size() < 3 || operands.size() > 4) {
            return unsupported("add/sub operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, true, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, true, true, diagnostic_engine, mnemonic + " lhs");
        if (!rd.has_value() || !rn.has_value() || rd->use_64bit != rn->use_64bit) {
            return std::nullopt;
        }
        if (const auto imm = parse_operand_immediate(operands[2]); imm.has_value()) {
            unsigned shift12 = 0;
            if (*imm < 0 || *imm > 0xfff) {
                if ((*imm & 0xfffLL) != 0 || *imm < 0 || *imm > 0xfff000) {
                    return unsupported("add/sub immediate");
                }
                shift12 = 1;
            }
            encoded.word = encode_add_sub_immediate_word(
                opcode == AArch64MachineOpcode::Sub, rd->use_64bit, rd->code,
                rn->code,
                shift12 == 0 ? static_cast<unsigned>(*imm)
                             : static_cast<unsigned>(*imm >> 12),
                shift12);
            return encoded;
        }
        if (const auto symbol = get_symbol_reference_operand(operands[2]);
            symbol.has_value()) {
            if (operands.size() != 3 || opcode != AArch64MachineOpcode::Add) {
                return unsupported("symbolic add/sub");
            }
            if (symbol->modifier != AArch64MachineSymbolReference::Modifier::Lo12) {
                return unsupported("unsupported add symbol modifier");
            }
            encoded.word = encode_add_sub_immediate_word(false, rd->use_64bit,
                                                         rd->code, rn->code, 0, 0);
            encoded.relocations.push_back(AArch64RelocationRecord{
                AArch64RelocationKind::PageOffset12, symbol->target, pc_offset});
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, true, diagnostic_engine, mnemonic + " rhs");
        if (!rm.has_value() || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        unsigned shift_type = 0;
        unsigned amount = 0;
        if (operands.size() == 4) {
            AArch64ShiftKind shift_kind = AArch64ShiftKind::Lsl;
            const auto shift_amount = parse_shift_amount(operands[3], shift_kind);
            if (!shift_amount.has_value()) {
                return unsupported("add/sub shift");
            }
            shift_type = shift_type_bits(shift_kind);
            amount = *shift_amount;
        }
        encoded.word = encode_add_sub_register_word(
            opcode == AArch64MachineOpcode::Sub, rd->use_64bit, rd->code,
            rn->code, rm->code, shift_type, amount);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::And ||
        opcode == AArch64MachineOpcode::Orr ||
        opcode == AArch64MachineOpcode::Eor) {
        if (operands.size() < 3 || operands.size() > 4) {
            return unsupported("logical operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, true, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, mnemonic + " rhs");
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->use_64bit != rn->use_64bit || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        unsigned shift_type = 0;
        unsigned amount = 0;
        if (operands.size() >= 4) {
            AArch64ShiftKind shift_kind = AArch64ShiftKind::Lsl;
            const auto shift_amount = parse_shift_amount(operands[3], shift_kind);
            if (!shift_amount.has_value()) {
                return unsupported("logical shift");
            }
            shift_type = shift_type_bits(shift_kind);
            amount = *shift_amount;
        }
        std::uint32_t base = rd->use_64bit ? 0x8A000000U : 0x0A000000U;
        if (opcode == AArch64MachineOpcode::Orr) {
            base = rd->use_64bit ? 0xAA000000U : 0x2A000000U;
        } else if (opcode == AArch64MachineOpcode::Eor) {
            base = rd->use_64bit ? 0xCA000000U : 0x4A000000U;
        }
        encoded.word =
            encode_logical_register_word(base, rd->code, rn->code, rm->code,
                                         shift_type, amount);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::Mul ||
        opcode == AArch64MachineOpcode::SignedDiv ||
        opcode == AArch64MachineOpcode::UnsignedDiv ||
        opcode == AArch64MachineOpcode::ShiftLeft ||
        opcode == AArch64MachineOpcode::ShiftRightLogical ||
        opcode == AArch64MachineOpcode::ShiftRightArithmetic) {
        if (operands.size() != 3) {
            return unsupported("arithmetic operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, mnemonic + " rhs");
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->use_64bit != rn->use_64bit || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        if (opcode == AArch64MachineOpcode::Mul) {
            encoded.word = (rd->use_64bit ? 0x9B007C00U : 0x1B007C00U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        if (opcode == AArch64MachineOpcode::SignedDiv) {
            encoded.word = (rd->use_64bit ? 0x9AC00C00U : 0x1AC00C00U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        if (opcode == AArch64MachineOpcode::UnsignedDiv) {
            encoded.word = (rd->use_64bit ? 0x9AC00800U : 0x1AC00800U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        const std::uint32_t base = rd->use_64bit
                                       ? (opcode == AArch64MachineOpcode::ShiftLeft
                                              ? 0x9AC02000U
                                              : opcode ==
                                                        AArch64MachineOpcode::
                                                            ShiftRightLogical
                                                    ? 0x9AC02400U
                                                    : 0x9AC02800U)
                                       : (opcode == AArch64MachineOpcode::ShiftLeft
                                              ? 0x1AC02000U
                                              : opcode ==
                                                        AArch64MachineOpcode::
                                                            ShiftRightLogical
                                                    ? 0x1AC02400U
                                                    : 0x1AC02800U);
        encoded.word = base | ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::Compare) {
        if (operands.size() != 2) {
            return unsupported("cmp operand shape");
        }
        const auto rn = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "cmp lhs");
        if (!rn.has_value()) {
            return std::nullopt;
        }
        if (const auto imm = parse_operand_immediate(operands[1]); imm.has_value()) {
            if (!check_signed_range(*imm, 0, 0xfff, diagnostic_engine,
                                    "AArch64 direct object writer: cmp immediate out of range")) {
                return std::nullopt;
            }
            encoded.word = (rn->use_64bit ? 0xF100001FU : 0x7100001FU) |
                           ((static_cast<unsigned>(*imm) & 0xfffU) << 10) |
                           ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, "cmp rhs");
        if (!rm.has_value() || rn->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        encoded.word = (rn->use_64bit ? 0xEB00001FU : 0x6B00001FU) |
                       ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::ConditionalSelect) {
        if (operands.size() != 4) {
            return unsupported("csel operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "csel dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, "csel lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, "csel rhs");
        const auto *condition = operands[3].get_condition_code_operand();
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            condition == nullptr || rd->use_64bit != rn->use_64bit ||
            rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        const unsigned cond = encode_condition_code(condition->code);
        if (cond == 0xffU) {
            return unsupported("csel condition");
        }
        encoded.word = (rd->use_64bit ? 0x9A800000U : 0x1A800000U) |
                       ((rm->code & 0x1fU) << 16) | ((cond & 0xfU) << 12) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::ConditionalSet) {
        if (operands.size() != 2) {
            return unsupported("cset operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "cset dst");
        const auto *condition = operands[1].get_condition_code_operand();
        if (!rd.has_value() || condition == nullptr) {
            return std::nullopt;
        }
        const unsigned cond = encode_condition_code(condition->code);
        if (cond == 0xffU) {
            return unsupported("cset condition");
        }
        encoded.word = (rd->use_64bit ? 0x9A800400U : 0x1A800400U) |
                       (31U << 16) | ((invert_condition_code(cond) & 0xfU) << 12) |
                       (31U << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::Adrp) {
        if (operands.size() != 2) {
            return unsupported("adrp operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "adrp dst");
        const auto symbol = get_symbol_reference_operand(operands[1]);
        if (!rd.has_value() || !rd->use_64bit || !symbol.has_value()) {
            return std::nullopt;
        }
        encoded.word = 0x90000000U | (rd->code & 0x1fU);
        encoded.relocations.push_back(AArch64RelocationRecord{
            symbol->modifier == AArch64MachineSymbolReference::Modifier::Got
                ? AArch64RelocationKind::GotPage21
                : AArch64RelocationKind::Page21,
            symbol->target, pc_offset});
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::StorePair ||
        opcode == AArch64MachineOpcode::LoadPair ||
        opcode == AArch64MachineOpcode::Load ||
        opcode == AArch64MachineOpcode::Store ||
        opcode == AArch64MachineOpcode::LoadByte ||
        opcode == AArch64MachineOpcode::StoreByte ||
        opcode == AArch64MachineOpcode::LoadHalf ||
        opcode == AArch64MachineOpcode::StoreHalf ||
        opcode == AArch64MachineOpcode::LoadUnscaled ||
        opcode == AArch64MachineOpcode::StoreUnscaled ||
        opcode == AArch64MachineOpcode::LoadByteUnscaled ||
        opcode == AArch64MachineOpcode::StoreByteUnscaled ||
        opcode == AArch64MachineOpcode::LoadHalfUnscaled ||
        opcode == AArch64MachineOpcode::StoreHalfUnscaled) {
        return encode_memory_family_instruction(
            instruction, function, scan_info, pc_offset, diagnostic_engine,
            AArch64MemoryInstructionEncodingContext{
                .resolve_general_reg_operand =
                    [&](const AArch64MachineOperand &operand,
                        const AArch64MachineFunction &encoded_function,
                        bool allow_stack_pointer, bool allow_zero_register,
                        DiagnosticEngine &encoded_diagnostics,
                        const std::string &context)
                    -> std::optional<EncodedGeneralReg> {
                    return resolve_general_reg_operand(
                        operand, encoded_function, allow_stack_pointer,
                        allow_zero_register, encoded_diagnostics, context);
                },
                .resolve_float_reg_operand =
                    [&](const AArch64MachineOperand &operand,
                        const AArch64MachineFunction &encoded_function,
                        DiagnosticEngine &encoded_diagnostics,
                        const std::string &context)
                    -> std::optional<EncodedFloatReg> {
                    return resolve_float_reg_operand(
                        operand, encoded_function, encoded_diagnostics, context);
                },
                .get_symbol_reference_operand =
                    [&](const AArch64MachineOperand &operand)
                    -> std::optional<AArch64MachineSymbolReference> {
                    return get_symbol_reference_operand(operand);
                }});
    }

    if (opcode == AArch64MachineOpcode::FloatMove) {
        if (operands.size() != 2) {
            return unsupported("fmov operand shape");
        }
        if (operand_is_float_reg_like(operands[0])) {
            const auto dst_float = resolve_float_reg_operand(
                operands[0], function, diagnostic_engine, "fmov dst");
            if (!dst_float.has_value()) {
                return std::nullopt;
            }
            if (operand_is_float_reg_like(operands[1])) {
                const auto src_float = resolve_float_reg_operand(
                    operands[1], function, diagnostic_engine, "fmov src");
                if (!src_float.has_value()) {
                    return std::nullopt;
                }
                if (!is_supported_scalar_fp_kind(dst_float->kind) ||
                    dst_float->kind != src_float->kind) {
                    return unsupported("unsupported scalar fmov register kind");
                }
                const auto base = fp_reg_move_base(dst_float->kind);
                if (!base.has_value()) {
                    return unsupported("unsupported scalar fmov register kind");
                }
                encoded.word = *base | ((src_float->code & 0x1fU) << 5) |
                               (dst_float->code & 0x1fU);
                return encoded;
            }
            const auto src_general = resolve_general_reg_operand(
                operands[1], function, false, false, diagnostic_engine, "fmov src");
            const auto base =
                src_general.has_value()
                    ? fp_gp_move_base(dst_float->kind, src_general->use_64bit, true)
                    : std::nullopt;
            if (!src_general.has_value() || !base.has_value()) {
                return std::nullopt;
            }
            encoded.word = *base | ((src_general->code & 0x1fU) << 5) |
                           (dst_float->code & 0x1fU);
            return encoded;
        }
        const auto dst_general = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "fmov dst");
        const auto src_float = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, "fmov src");
        const auto base = (dst_general.has_value() && src_float.has_value())
                              ? fp_gp_move_base(src_float->kind, dst_general->use_64bit,
                                                false)
                              : std::nullopt;
        if (!dst_general.has_value() || !src_float.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((src_float->code & 0x1fU) << 5) |
                       (dst_general->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::FloatAdd ||
        opcode == AArch64MachineOpcode::FloatSub ||
        opcode == AArch64MachineOpcode::FloatMul ||
        opcode == AArch64MachineOpcode::FloatDiv) {
        if (operands.size() != 3) {
            return unsupported("floating binary operand shape");
        }
        const auto rd = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_float_reg_operand(
            operands[2], function, diagnostic_engine, mnemonic + " rhs");
        const auto base = (rd.has_value() && rn.has_value() && rm.has_value())
                              ? fp_binary_base(opcode, rd->kind)
                              : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->kind != rn->kind || rd->kind != rm->kind ||
            !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::FloatCompare) {
        if (operands.size() != 2) {
            return unsupported("fcmp operand shape");
        }
        const auto rn = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, "fcmp lhs");
        const auto base = rn.has_value() ? fcmp_base(rn->kind) : std::nullopt;
        if (!rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        if (const auto rm = resolve_float_reg_operand(
                operands[1], function, diagnostic_engine, "fcmp rhs");
            rm.has_value()) {
            if (rm->kind != rn->kind) {
                return std::nullopt;
            }
            encoded.word = *base | ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        const auto imm = parse_operand_immediate(operands[1]);
        const auto *immediate = operands[1].get_immediate_operand();
        if ((imm.has_value() && *imm == 0) ||
            (immediate != nullptr &&
             immediate->asm_text.find("0.0") != std::string::npos)) {
            encoded.word = *base | ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        return unsupported("fcmp immediate");
    }

    if (opcode == AArch64MachineOpcode::SignedIntToFloat ||
        opcode == AArch64MachineOpcode::UnsignedIntToFloat ||
        opcode == AArch64MachineOpcode::FloatToSignedInt ||
        opcode == AArch64MachineOpcode::FloatToUnsignedInt) {
        if (operands.size() != 2) {
            return unsupported("int/float conversion operand shape");
        }
        if (opcode == AArch64MachineOpcode::SignedIntToFloat ||
            opcode == AArch64MachineOpcode::UnsignedIntToFloat) {
            const auto rd = resolve_float_reg_operand(
                operands[0], function, diagnostic_engine, mnemonic + " dst");
            const auto rn = resolve_general_reg_operand(
                operands[1], function, false, false, diagnostic_engine,
                mnemonic + " src");
            const auto base = (rd.has_value() && rn.has_value())
                                  ? int_to_fp_base(opcode, rd->kind,
                                                   rn->use_64bit)
                                  : std::nullopt;
            if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
                return std::nullopt;
            }
            encoded.word =
                *base | ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, mnemonic + " src");
        const auto base = (rd.has_value() && rn.has_value())
                              ? fp_to_int_base(opcode, rn->kind,
                                               rd->use_64bit)
                              : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word =
            *base | ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (opcode == AArch64MachineOpcode::FloatConvert) {
        if (operands.size() != 2) {
            return unsupported("fcvt operand shape");
        }
        const auto rd = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, "fcvt dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, "fcvt src");
        const auto base = (rd.has_value() && rn.has_value())
                              ? fp_convert_base(rd->kind, rn->kind)
                              : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    return unsupported("encoder coverage gap");
}

} // namespace sysycc
