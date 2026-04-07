#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

std::string strip_floating_literal_suffix(std::string value_text) {
    while (!value_text.empty()) {
        const char last = value_text.back();
        if (last == 'f' || last == 'F' || last == 'l' || last == 'L') {
            value_text.pop_back();
            continue;
        }
        break;
    }
    return value_text;
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
    if (literal_text.find("0x") != std::string::npos ||
        literal_text.find("0X") != std::string::npos ||
        literal_text.find('e') != std::string::npos ||
        literal_text.find('E') != std::string::npos ||
        literal_text.find('p') != std::string::npos ||
        literal_text.find('P') != std::string::npos) {
        return false;
    }

    std::string normalized = literal_text;
    if (!normalized.empty() &&
        (normalized.front() == '+' || normalized.front() == '-')) {
        normalized.erase(normalized.begin());
    }
    if (normalized.empty()) {
        return false;
    }

    const std::size_t dot = normalized.find('.');
    std::string integral_text = normalized;
    if (dot != std::string::npos) {
        integral_text = normalized.substr(0, dot);
        const std::string fractional_text = normalized.substr(dot + 1);
        if (fractional_text.empty()) {
            return false;
        }
        if (fractional_text.find_first_not_of('0') != std::string::npos) {
            return false;
        }
    }
    if (integral_text.empty()) {
        integral_text = "0";
    }
    if (integral_text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }

    char *value_end = nullptr;
    const long double value = std::strtold(integral_text.c_str(), &value_end);
    if (value_end == nullptr || *value_end != '\0' || !std::isfinite(value)) {
        return false;
    }
    return std::fabs(value) <= 9007199254740992.0L;
}

bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                  AArch64ConstantMaterializationContext &context,
                                  const CoreIrType *type, std::uint64_t value,
                                  const AArch64VirtualReg &target_reg) {
    const unsigned pieces = target_reg.get_use_64bit() ? 4U : 2U;
    bool emitted = false;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (!emitted) {
            machine_block.append_instruction(
                "movz " + def_vreg(target_reg) + ", #" +
                std::to_string(imm16) + ", lsl #" +
                std::to_string(piece * 16U));
            emitted = true;
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        machine_block.append_instruction(
            "movk " + use_vreg(target_reg) + ", #" +
            std::to_string(imm16) + ", lsl #" +
            std::to_string(piece * 16U));
    }
    if (!emitted) {
        machine_block.append_instruction(AArch64MachineInstr(
            "mov",
            {def_vreg_operand(target_reg), zero_register_operand(target_reg.get_use_64bit())}));
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
            const float parsed = std::stof(literal_text);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
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
                "fcvtn", {def_vreg_operand(target_reg),
                          use_vreg_operand(temp_float)}));
            return true;
        }
        case CoreIrFloatKind::Float32: {
            static CoreIrIntegerType i32_type(32);
            const float parsed = std::stof(literal_text);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
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
            const double parsed = std::stod(literal_text);
            std::uint64_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
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
            if (!float128_literal_is_supported_by_helper_path(literal_text)) {
                context.report_error(
                    "float128 literal is not exactly representable by the current "
                    "AArch64 helper-based materialization path");
                return false;
            }
            static CoreIrIntegerType i64_type(64);
            const double parsed = std::stod(literal_text);
            std::uint64_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
            const AArch64VirtualReg temp_double =
                function.create_virtual_reg(AArch64VirtualRegKind::Float64);
            const AArch64VirtualReg temp_bits =
                function.create_virtual_reg(AArch64VirtualRegKind::General64);
            if (!materialize_integer_constant(machine_block, context, &i64_type, bits,
                                             temp_bits)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "fmov", {def_vreg_operand(temp_double), use_vreg_operand(temp_bits)}));
            context.append_copy_to_physical_reg(
                machine_block, static_cast<unsigned>(AArch64PhysicalReg::V0),
                AArch64VirtualRegKind::Float64, temp_double);
            context.append_helper_call(machine_block, "__extenddftf2");
            context.append_copy_from_physical_reg(
                machine_block, target_reg,
                static_cast<unsigned>(AArch64PhysicalReg::V0),
                AArch64VirtualRegKind::Float128);
            return true;
        }
        }
        context.report_error(
            "failed to materialize floating constant in the AArch64 native backend");
        return false;
    } catch (...) {
        context.report_error(
            "failed to parse floating literal for the AArch64 native backend");
        return false;
    }
}

} // namespace sysycc
