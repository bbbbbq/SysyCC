#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.hpp"

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace sysycc {

namespace {

std::string format_float_literal(long double value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    if (value == 0.0L) {
        return std::signbit(static_cast<double>(value)) ? "-0.0" : "0.0";
    }
    std::ostringstream stream;
    stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
    stream << std::setprecision(std::numeric_limits<long double>::max_digits10)
           << value;
    return stream.str();
}

std::optional<std::string> canonicalize_float_literal_text(
    const AArch64LlvmImportType &type, const std::string &literal_text) {
    const std::string trimmed = llvm_import_trim_copy(literal_text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (llvm_import_starts_with(trimmed, "0x") ||
        llvm_import_starts_with(trimmed, "-0x") ||
        llvm_import_starts_with(trimmed, "+0x") ||
        llvm_import_starts_with(trimmed, "0X") ||
        llvm_import_starts_with(trimmed, "-0X") ||
        llvm_import_starts_with(trimmed, "+0X")) {
        if (trimmed.find('p') != std::string::npos ||
            trimmed.find('P') != std::string::npos) {
            char *end = nullptr;
            const long double parsed = std::strtold(trimmed.c_str(), &end);
            if (end == nullptr || *end != '\0') {
                return std::nullopt;
            }
            return format_float_literal(parsed);
        }

        try {
            std::size_t consumed = 0;
            const std::uint64_t bits =
                static_cast<std::uint64_t>(std::stoull(trimmed, &consumed, 16));
            if (consumed != trimmed.size()) {
                return std::nullopt;
            }
            if (type.kind == AArch64LlvmImportTypeKind::Float16) {
                const std::uint16_t half_bits =
                    static_cast<std::uint16_t>(bits & 0xffffU);
                const int sign = (half_bits & 0x8000U) != 0 ? -1 : 1;
                const int exponent = (half_bits >> 10U) & 0x1fU;
                const int mantissa = half_bits & 0x3ffU;
                long double value = 0.0L;
                if (exponent == 0x1f) {
                    return mantissa == 0
                               ? std::optional<std::string>(
                                     sign < 0 ? "-inf" : "inf")
                               : std::optional<std::string>("nan");
                }
                if (exponent == 0) {
                    value = std::ldexp(static_cast<long double>(mantissa),
                                       -24);
                } else {
                    value = std::ldexp(
                        1.0L + static_cast<long double>(mantissa) / 1024.0L,
                        exponent - 15);
                }
                return format_float_literal(sign < 0 ? -value : value);
            }
            if (type.kind == AArch64LlvmImportTypeKind::Float32) {
                const std::uint32_t float_bits =
                    static_cast<std::uint32_t>(bits & 0xffffffffU);
                float value = 0.0F;
                std::memcpy(&value, &float_bits, sizeof(value));
                return format_float_literal(static_cast<long double>(value));
            }
            if (type.kind == AArch64LlvmImportTypeKind::Float64) {
                double value = 0.0;
                std::memcpy(&value, &bits, sizeof(value));
                return format_float_literal(static_cast<long double>(value));
            }
        } catch (...) {
            return std::nullopt;
        }
        return trimmed;
    }

    char *end = nullptr;
    const long double parsed = std::strtold(trimmed.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return format_float_literal(parsed);
}

std::optional<AArch64LlvmImportConstant> parse_constant_impl(
    const AArch64LlvmImportType &type, const std::string &text) {
    const std::string trimmed = llvm_import_trim_copy(text);
    if (trimmed == "zeroinitializer") {
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::ZeroInitializer;
        return constant;
    }

    switch (type.kind) {
    case AArch64LlvmImportTypeKind::Integer: {
        const auto value = llvm_import_parse_integer_literal(trimmed);
        if (!value.has_value()) {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::Integer;
        constant.integer_value = *value;
        return constant;
    }
    case AArch64LlvmImportTypeKind::Float16:
    case AArch64LlvmImportTypeKind::Float32:
    case AArch64LlvmImportTypeKind::Float64:
    case AArch64LlvmImportTypeKind::Float128: {
        const auto value = canonicalize_float_literal_text(type, trimmed);
        if (!value.has_value()) {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::Float;
        constant.float_text = *value;
        return constant;
    }
    case AArch64LlvmImportTypeKind::Pointer: {
        if (trimmed != "null") {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::NullPointer;
        return constant;
    }
    case AArch64LlvmImportTypeKind::Array: {
        const bool square_bracketed =
            trimmed.size() >= 2 && trimmed.front() == '[' &&
            trimmed.back() == ']';
        const bool angle_bracketed =
            trimmed.size() >= 2 && trimmed.front() == '<' &&
            trimmed.back() == '>';
        if (!square_bracketed && !angle_bracketed || type.element_types.size() != 1) {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::Aggregate;
        const AArch64LlvmImportType &element_type = type.element_types.front();
        const std::string inner =
            llvm_import_trim_copy(trimmed.substr(1, trimmed.size() - 2));
        for (const std::string &element_entry :
             llvm_import_split_top_level(inner, ',')) {
            if (element_entry.empty()) {
                continue;
            }
            std::size_t position = 0;
            const auto element_type_text =
                llvm_import_consume_type_token(element_entry, position);
            if (element_type_text.has_value()) {
                const auto parsed_type =
                    parse_llvm_import_type_text(*element_type_text);
                if (!parsed_type.has_value()) {
                    return std::nullopt;
                }
            }
            const auto element = parse_constant_impl(
                element_type,
                llvm_import_trim_copy(element_entry.substr(position)));
            if (!element.has_value()) {
                return std::nullopt;
            }
            constant.elements.push_back(*element);
        }
        return constant;
    }
    case AArch64LlvmImportTypeKind::Struct: {
        if (trimmed.size() < 2 || trimmed.front() != '{' || trimmed.back() != '}') {
            return std::nullopt;
        }
        const std::vector<std::string> entries = llvm_import_split_top_level(
            llvm_import_trim_copy(trimmed.substr(1, trimmed.size() - 2)), ',');
        if (entries.size() != type.element_types.size()) {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::Aggregate;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            std::size_t position = 0;
            const auto element_type_text =
                llvm_import_consume_type_token(entries[index], position);
            if (element_type_text.has_value()) {
                const auto parsed_type =
                    parse_llvm_import_type_text(*element_type_text);
                if (!parsed_type.has_value()) {
                    return std::nullopt;
                }
            }
            const auto element = parse_constant_impl(
                type.element_types[index],
                llvm_import_trim_copy(entries[index].substr(position)));
            if (!element.has_value()) {
                return std::nullopt;
            }
            constant.elements.push_back(*element);
        }
        return constant;
    }
    case AArch64LlvmImportTypeKind::Unknown:
    case AArch64LlvmImportTypeKind::Void:
    case AArch64LlvmImportTypeKind::Named:
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<AArch64LlvmImportConstant> parse_llvm_import_constant_text(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_constant_impl(type, text);
}

} // namespace sysycc
