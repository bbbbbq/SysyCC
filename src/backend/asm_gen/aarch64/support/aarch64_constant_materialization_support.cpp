#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "backend/asm_gen/aarch64/support/aarch64_float_literal_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

std::optional<std::uint64_t> parse_raw_hex_float_bits(const std::string &literal_text,
                                                      unsigned bit_width) {
    if ((bit_width != 32U && bit_width != 64U) || literal_text.size() < 3 ||
        literal_text[0] != '0' ||
        (literal_text[1] != 'x' && literal_text[1] != 'X') ||
        literal_text.find('p') != std::string::npos ||
        literal_text.find('P') != std::string::npos) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const std::uint64_t bits =
            static_cast<std::uint64_t>(std::stoull(literal_text, &consumed, 16));
        if (consumed != literal_text.size()) {
            return std::nullopt;
        }
        return bit_width == 32U ? (bits & 0xffffffffU) : bits;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t>
parse_scalar_float_bits_with_apfloat(const std::string &literal_text,
                                     CoreIrFloatKind kind) {
    llvm::APFloat value(kind == CoreIrFloatKind::Float16
                            ? llvm::APFloat::IEEEhalf()
                            : (kind == CoreIrFloatKind::Float32
                                   ? llvm::APFloat::IEEEsingle()
                                   : llvm::APFloat::IEEEdouble()));
    auto status = value.convertFromString(
        llvm::StringRef(literal_text), llvm::APFloat::rmNearestTiesToEven);
    if (!status) {
        llvm::consumeError(status.takeError());
        return std::nullopt;
    }
    return value.bitcastToAPInt().getZExtValue();
}

bool is_named_nonfinite_literal(std::string_view value_text) {
    std::string normalized(value_text);
    for (char &ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (normalized == "inf" || normalized == "+inf" || normalized == "-inf") {
        return true;
    }
    if (normalized == "nan" || normalized == "+nan" || normalized == "-nan") {
        return true;
    }
    return false;
}

std::string strip_floating_literal_suffix(std::string value_text) {
    return is_named_nonfinite_literal(value_text)
               ? value_text
               : strip_floating_literal_suffix_preserving_hex(
                     std::move(value_text));
}

std::string format_bits_literal(std::uint64_t bits, unsigned hex_digits) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setfill('0')
           << std::setw(static_cast<int>(hex_digits)) << bits;
    return stream.str();
}

std::uint16_t float32_to_float16_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const std::uint16_t sign =
        static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    std::uint32_t mantissa = bits & 0x007fffffU;
    const std::uint32_t exponent = (bits >> 23U) & 0xffU;

    if (exponent == 0xffU) {
        if (mantissa == 0) {
            return static_cast<std::uint16_t>(sign | 0x7c00U);
        }
        std::uint16_t payload =
            static_cast<std::uint16_t>(mantissa >> 13U);
        if (payload == 0) {
            payload = 1;
        }
        return static_cast<std::uint16_t>(sign | 0x7c00U | payload);
    }

    int adjusted_exponent = static_cast<int>(exponent) - 127 + 15;
    if (adjusted_exponent >= 0x1f) {
        return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    if (adjusted_exponent <= 0) {
        if (adjusted_exponent < -10) {
            return sign;
        }
        mantissa = (mantissa | 0x00800000U) >> (1 - adjusted_exponent);
        if ((mantissa & 0x00001000U) != 0) {
            mantissa += 0x00002000U;
        }
        return static_cast<std::uint16_t>(sign | (mantissa >> 13U));
    }

    if ((mantissa & 0x00001000U) != 0) {
        mantissa += 0x00002000U;
        if ((mantissa & 0x00800000U) != 0) {
            mantissa = 0;
            ++adjusted_exponent;
            if (adjusted_exponent >= 0x1f) {
                return static_cast<std::uint16_t>(sign | 0x7c00U);
            }
        }
    }

    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint16_t>(adjusted_exponent) << 10U) |
        static_cast<std::uint16_t>(mantissa >> 13U));
}

bool floating_literal_is_zero(const std::string &literal_text) {
    char *end = nullptr;
    const long double value = std::strtold(literal_text.c_str(), &end);
    return end != literal_text.c_str() && end != nullptr && *end == '\0' &&
           value == 0.0L;
}

bool float128_literal_is_supported_by_helper_path(const std::string &literal_text) {
    if (aarch64_literal_has_raw_hex_bit_pattern(literal_text)) {
        return false;
    }
    char *value_end = nullptr;
    const long double value = std::strtold(literal_text.c_str(), &value_end);
    if (value_end == nullptr || *value_end != '\0' || !std::isfinite(value)) {
        return false;
    }
    const double narrowed = static_cast<double>(value);
    if (!std::isfinite(narrowed)) {
        return false;
    }
    return static_cast<long double>(narrowed) == value;
}

bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                  AArch64ConstantMaterializationContext &context,
                                  const CoreIrType *type, std::uint64_t value,
                                  const AArch64VirtualReg &target_reg) {
    if (value == 0) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov",
            {def_vreg_operand(target_reg), zero_register_operand(target_reg.get_use_64bit())}));
        context.apply_truncate_to_virtual_reg(machine_block, target_reg, type);
        return true;
    }

    const unsigned pieces = target_reg.get_use_64bit() ? 4U : 2U;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (piece == 0) {
            machine_block.append_instruction(AArch64MachineInstr(
                "movz", {def_vreg_operand(target_reg),
                         AArch64MachineOperand::immediate("#" + std::to_string(imm16)),
                         shift_operand("lsl", piece * 16U)}));
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "movk", {use_vreg_operand(target_reg),
                     AArch64MachineOperand::immediate("#" + std::to_string(imm16)),
                     shift_operand("lsl", piece * 16U)}));
    }
    context.apply_truncate_to_virtual_reg(machine_block, target_reg, type);
    return true;
}

bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                AArch64ConstantMaterializationContext &context,
                                const CoreIrConstantFloat &constant,
                                const AArch64VirtualReg &target_reg,
                                AArch64MachineFunction &function) {
    const auto *float_type = as_float_type(constant.get_type());
    if (float_type == nullptr) {
        return false;
    }

    const std::string literal_text =
        strip_floating_literal_suffix(constant.get_literal_text());
    try {
        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16: {
            static CoreIrIntegerType i32_type(32);
            const auto bits_or =
                parse_scalar_float_bits_with_apfloat(literal_text,
                                                     CoreIrFloatKind::Float32);
            if (!bits_or.has_value()) {
                context.report_error(
                    "failed to parse floating literal for the AArch64 native "
                    "backend: " +
                    literal_text);
                return false;
            }
            std::uint32_t bits = static_cast<std::uint32_t>(*bits_or);
            const AArch64VirtualReg temp_bits =
                function.create_virtual_reg(AArch64VirtualRegKind::General32);
            const AArch64VirtualReg temp_float =
                function.create_virtual_reg(AArch64VirtualRegKind::Float32);
            if (!materialize_integer_constant(machine_block, context, &i32_type, bits,
                                             temp_bits)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "fmov", {def_vreg_operand(temp_float), use_vreg_operand(temp_bits)}));
            machine_block.append_instruction(AArch64MachineInstr(
                "fcvt", {def_vreg_operand(target_reg),
                          use_vreg_operand(temp_float)}));
            return true;
        }
        case CoreIrFloatKind::Float32: {
            static CoreIrIntegerType i32_type(32);
            std::uint32_t bits = 0;
            if (const auto raw_bits = parse_raw_hex_float_bits(literal_text, 32U);
                raw_bits.has_value()) {
                bits = static_cast<std::uint32_t>(*raw_bits);
            } else {
                const auto bits_or =
                    parse_scalar_float_bits_with_apfloat(literal_text,
                                                         CoreIrFloatKind::Float32);
                if (!bits_or.has_value()) {
                    context.report_error(
                        "failed to parse floating literal for the AArch64 native "
                        "backend: " +
                        literal_text);
                    return false;
                }
                bits = static_cast<std::uint32_t>(*bits_or);
            }
            const AArch64VirtualReg temp =
                function.create_virtual_reg(AArch64VirtualRegKind::General32);
            if (!materialize_integer_constant(machine_block, context, &i32_type, bits,
                                             temp)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "fmov", {def_vreg_operand(target_reg), use_vreg_operand(temp)}));
            return true;
        }
        case CoreIrFloatKind::Float64: {
            static CoreIrIntegerType i64_type(64);
            std::uint64_t bits = 0;
            if (const auto raw_bits = parse_raw_hex_float_bits(literal_text, 64U);
                raw_bits.has_value()) {
                bits = *raw_bits;
            } else {
                const auto bits_or =
                    parse_scalar_float_bits_with_apfloat(literal_text,
                                                         CoreIrFloatKind::Float64);
                if (!bits_or.has_value()) {
                    context.report_error(
                        "failed to parse floating literal for the AArch64 native "
                        "backend: " +
                        literal_text);
                    return false;
                }
                bits = *bits_or;
            }
            const AArch64VirtualReg temp =
                function.create_virtual_reg(AArch64VirtualRegKind::General64);
            if (!materialize_integer_constant(machine_block, context, &i64_type, bits,
                                             temp)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "fmov", {def_vreg_operand(target_reg), use_vreg_operand(temp)}));
            return true;
        }
        case CoreIrFloatKind::Float128: {
            const auto words = encode_fp128_literal_words(literal_text);
            if (!words.has_value()) {
                context.report_error(
                    "failed to encode float128 literal for AArch64 exact "
                    "materialization");
                return false;
            }
            static CoreIrIntegerType i64_type(64);
            const AArch64VirtualReg low_bits =
                function.create_virtual_reg(AArch64VirtualRegKind::General64);
            const AArch64VirtualReg high_bits =
                function.create_virtual_reg(AArch64VirtualRegKind::General64);
            if (!materialize_integer_constant(machine_block, context, &i64_type,
                                             words->first, low_bits) ||
                !materialize_integer_constant(machine_block, context, &i64_type,
                                             words->second, high_bits)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "sub",
                {AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::immediate("#16")}));
            machine_block.append_instruction(AArch64MachineInstr(
                "str", {use_vreg_operand(low_bits),
                        AArch64MachineOperand::memory_address_stack_pointer()}));
            machine_block.append_instruction(AArch64MachineInstr(
                "str", {use_vreg_operand(high_bits),
                        AArch64MachineOperand::memory_address_stack_pointer(8)}));
            machine_block.append_instruction(AArch64MachineInstr(
                "ldr", {def_vreg_operand(target_reg),
                        AArch64MachineOperand::memory_address_stack_pointer()}));
            machine_block.append_instruction(AArch64MachineInstr(
                "add",
                {AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::stack_pointer(true),
                 AArch64MachineOperand::immediate("#16")}));
            return true;
        }
        }
        context.report_error(
            "failed to materialize floating constant in the AArch64 native backend");
        return false;
    } catch (...) {
        context.report_error("failed to parse floating literal for the AArch64 native "
                             "backend: " +
                             literal_text);
        return false;
    }
}

} // namespace sysycc
