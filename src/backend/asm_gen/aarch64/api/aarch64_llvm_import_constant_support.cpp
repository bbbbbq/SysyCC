#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.hpp"

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_literal_support.hpp"

#include <cctype>
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

    if (type.kind == AArch64LlvmImportTypeKind::Float128) {
        return canonicalize_fp128_literal_text(trimmed);
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
            std::string hex_bits_text = trimmed;
            if (llvm_import_starts_with(hex_bits_text, "0xH") ||
                llvm_import_starts_with(hex_bits_text, "0XH")) {
                hex_bits_text = "0x" + hex_bits_text.substr(3);
            } else if (llvm_import_starts_with(hex_bits_text, "-0xH") ||
                       llvm_import_starts_with(hex_bits_text, "-0XH")) {
                hex_bits_text = "-0x" + hex_bits_text.substr(4);
            } else if (llvm_import_starts_with(hex_bits_text, "+0xH") ||
                       llvm_import_starts_with(hex_bits_text, "+0XH")) {
                hex_bits_text = "+0x" + hex_bits_text.substr(4);
            }
            std::size_t consumed = 0;
            const std::uint64_t bits = static_cast<std::uint64_t>(
                std::stoull(hex_bits_text, &consumed, 16));
            if (consumed != hex_bits_text.size()) {
                return std::nullopt;
            }
            auto decode_float64_bits = [](std::uint64_t raw_bits) {
                double value = 0.0;
                std::memcpy(&value, &raw_bits, sizeof(value));
                return static_cast<long double>(value);
            };
            if (type.kind == AArch64LlvmImportTypeKind::Float16) {
                const std::size_t hex_digit_count =
                    hex_bits_text.size() >= 2 ? (hex_bits_text.size() - 2) : 0;
                if (hex_digit_count > 4U) {
                    return format_float_literal(decode_float64_bits(bits));
                }
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
                const std::size_t hex_digit_count =
                    hex_bits_text.size() >= 2 ? (hex_bits_text.size() - 2) : 0;
                if (hex_digit_count > 8U) {
                    return format_float_literal(
                        static_cast<long double>(
                            static_cast<float>(decode_float64_bits(bits))));
                }
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
    const AArch64LlvmImportType &type, const std::string &text);

std::optional<AArch64LlvmImportTypedConstant>
parse_typed_constant(const std::string &text);

bool import_types_equal(const AArch64LlvmImportType &lhs,
                        const AArch64LlvmImportType &rhs);

std::optional<AArch64LlvmImportConstant> parse_vector_splat_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (!type.array_uses_vector_syntax || type.element_types.size() != 1 ||
        !llvm_import_starts_with(text, "splat (") || text.back() != ')') {
        return std::nullopt;
    }

    const std::string inner = llvm_import_trim_copy(
        text.substr(std::strlen("splat ("), text.size() - std::strlen("splat (") - 1));
    const auto typed_element = parse_typed_constant(inner);
    if (!typed_element.has_value()) {
        return std::nullopt;
    }

    const AArch64LlvmImportType &element_type = type.element_types.front();
    if (!import_types_equal(typed_element->type, element_type)) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::Aggregate;
    for (std::size_t index = 0; index < type.array_element_count; ++index) {
        constant.elements.push_back(typed_element->constant);
    }
    return constant;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>>
parse_integer_literal_words_128(const std::string &text) {
    const std::string trimmed = llvm_import_trim_copy(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    bool negative = false;
    std::size_t position = 0;
    if (trimmed[position] == '+' || trimmed[position] == '-') {
        negative = trimmed[position] == '-';
        ++position;
    }
    if (position >= trimmed.size()) {
        return std::nullopt;
    }

    unsigned base = 10;
    if (position + 1 < trimmed.size() && trimmed[position] == '0' &&
        (trimmed[position + 1] == 'x' || trimmed[position + 1] == 'X')) {
        base = 16;
        position += 2;
    }
    if (position >= trimmed.size()) {
        return std::nullopt;
    }

    unsigned __int128 magnitude = 0;
    for (; position < trimmed.size(); ++position) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[position]);
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = 10U + static_cast<unsigned>(ch - 'a');
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = 10U + static_cast<unsigned>(ch - 'A');
        } else {
            return std::nullopt;
        }
        if (digit >= base) {
            return std::nullopt;
        }
        magnitude = magnitude * base + digit;
    }

    unsigned __int128 value = magnitude;
    if (negative) {
        value = (~magnitude) + 1;
    }
    return std::pair<std::uint64_t, std::uint64_t>{
        static_cast<std::uint64_t>(value),
        static_cast<std::uint64_t>(value >> 64U)};
}

std::optional<std::size_t> find_top_level_separator(const std::string &text,
                                                    const std::string &needle) {
    if (needle.empty() || text.size() < needle.size()) {
        return std::nullopt;
    }

    int square_depth = 0;
    int brace_depth = 0;
    int paren_depth = 0;
    int angle_depth = 0;
    for (std::size_t index = 0; index + needle.size() <= text.size(); ++index) {
        switch (text[index]) {
        case '[':
            ++square_depth;
            break;
        case ']':
            --square_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            --brace_depth;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            --paren_depth;
            break;
        case '<':
            ++angle_depth;
            break;
        case '>':
            --angle_depth;
            break;
        default:
            break;
        }

        if (square_depth == 0 && brace_depth == 0 && paren_depth == 0 &&
            angle_depth == 0 &&
            text.compare(index, needle.size(), needle) == 0) {
            return index;
        }
    }
    return std::nullopt;
}

bool is_constant_trailing_suffix(std::string_view text) {
    return llvm_import_starts_with(text, "align ") ||
           llvm_import_starts_with(text, "section ") ||
           llvm_import_starts_with(text, "comdat") ||
           llvm_import_starts_with(text, "partition ") ||
           llvm_import_starts_with(text, "!dbg ");
}

std::string strip_trailing_constant_suffixes(const std::string &text) {
    const std::vector<std::string> parts =
        llvm_import_split_top_level(text, ',');
    if (parts.empty()) {
        return llvm_import_trim_copy(text);
    }

    std::string result = parts.front();
    for (std::size_t index = 1; index < parts.size(); ++index) {
        if (is_constant_trailing_suffix(parts[index])) {
            break;
        }
        result += ", " + parts[index];
    }
    return llvm_import_strip_trailing_alignment_suffix(result);
}

bool import_types_equal(const AArch64LlvmImportType &lhs,
                        const AArch64LlvmImportType &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if (lhs.integer_bit_width != rhs.integer_bit_width ||
        lhs.pointer_address_space != rhs.pointer_address_space ||
        lhs.array_element_count != rhs.array_element_count ||
        lhs.array_uses_vector_syntax != rhs.array_uses_vector_syntax ||
        lhs.named_type_name != rhs.named_type_name ||
        lhs.element_types.size() != rhs.element_types.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.element_types.size(); ++index) {
        if (!import_types_equal(lhs.element_types[index], rhs.element_types[index])) {
            return false;
        }
    }
    return true;
}

std::optional<AArch64LlvmImportTypedConstant>
parse_typed_constant(const std::string &text) {
    const std::string normalized = llvm_import_trim_copy(text);
    std::size_t position = 0;
    const auto type_text =
        llvm_import_consume_type_token(normalized, position);
    if (!type_text.has_value()) {
        return std::nullopt;
    }
    const auto type = parse_llvm_import_type_text(*type_text);
    if (!type.has_value()) {
        return std::nullopt;
    }
    const auto constant = parse_constant_impl(
        *type, llvm_import_trim_copy(normalized.substr(position)));
    if (!constant.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportTypedConstant{*type_text, *type, std::move(*constant)};
}

bool is_integer_import_type(const AArch64LlvmImportType &type) {
    return type.kind == AArch64LlvmImportTypeKind::Integer;
}

bool is_float_import_type(const AArch64LlvmImportType &type) {
    return type.kind == AArch64LlvmImportTypeKind::Float16 ||
           type.kind == AArch64LlvmImportTypeKind::Float32 ||
           type.kind == AArch64LlvmImportTypeKind::Float64 ||
           type.kind == AArch64LlvmImportTypeKind::Float128;
}

std::optional<std::size_t>
get_import_type_storage_bit_width(const AArch64LlvmImportType &type) {
    switch (type.kind) {
    case AArch64LlvmImportTypeKind::Integer:
        return type.integer_bit_width;
    case AArch64LlvmImportTypeKind::Float16:
        return 16;
    case AArch64LlvmImportTypeKind::Float32:
        return 32;
    case AArch64LlvmImportTypeKind::Float64:
        return 64;
    case AArch64LlvmImportTypeKind::Float128:
        return 128;
    case AArch64LlvmImportTypeKind::Pointer:
        return 64;
    default:
        return std::nullopt;
    }
}

bool supports_integer_cast(const AArch64LlvmImportType &source_type,
                           const AArch64LlvmImportType &target_type) {
    return is_integer_import_type(source_type) && is_integer_import_type(target_type);
}

bool supports_int_to_float_cast(const AArch64LlvmImportType &source_type,
                                const AArch64LlvmImportType &target_type) {
    return is_integer_import_type(source_type) && is_float_import_type(target_type);
}

bool supports_float_to_int_cast(const AArch64LlvmImportType &source_type,
                                const AArch64LlvmImportType &target_type) {
    return is_float_import_type(source_type) && is_integer_import_type(target_type);
}

bool supports_float_cast(const AArch64LlvmImportType &source_type,
                         const AArch64LlvmImportType &target_type) {
    return is_float_import_type(source_type) && is_float_import_type(target_type);
}

bool supports_pointer_bitcast(const AArch64LlvmImportType &source_type,
                              const AArch64LlvmImportType &target_type) {
    return source_type.kind == AArch64LlvmImportTypeKind::Pointer &&
           target_type.kind == AArch64LlvmImportTypeKind::Pointer;
}

bool supports_scalar_bitcast(const AArch64LlvmImportType &source_type,
                             const AArch64LlvmImportType &target_type) {
    const auto source_bits = get_import_type_storage_bit_width(source_type);
    const auto target_bits = get_import_type_storage_bit_width(target_type);
    if (!source_bits.has_value() || !target_bits.has_value() ||
        *source_bits != *target_bits) {
        return false;
    }
    return (is_integer_import_type(source_type) && is_float_import_type(target_type)) ||
           (is_float_import_type(source_type) && is_integer_import_type(target_type));
}

bool supports_bitcast(const AArch64LlvmImportType &source_type,
                      const AArch64LlvmImportType &target_type) {
    return supports_pointer_bitcast(source_type, target_type) ||
           supports_scalar_bitcast(source_type, target_type);
}

bool supports_addrspace_cast(const AArch64LlvmImportType &source_type,
                             const AArch64LlvmImportType &target_type) {
    return source_type.kind == AArch64LlvmImportTypeKind::Pointer &&
           target_type.kind == AArch64LlvmImportTypeKind::Pointer;
}

std::optional<AArch64LlvmImportConstant> parse_gep_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (type.kind != AArch64LlvmImportTypeKind::Pointer) {
        return std::nullopt;
    }

    std::string payload = llvm_import_trim_copy(text);
    if (!llvm_import_starts_with(payload, "getelementptr ")) {
        return std::nullopt;
    }
    payload = llvm_import_trim_copy(payload.substr(14));

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::GetElementPtr;
    if (llvm_import_starts_with(payload, "inbounds ")) {
        constant.gep_is_inbounds = true;
        payload = llvm_import_trim_copy(payload.substr(9));
    }
    if (payload.size() < 2 || payload.front() != '(' || payload.back() != ')') {
        return std::nullopt;
    }

    const std::vector<std::string> operands = llvm_import_split_top_level(
        llvm_import_trim_copy(payload.substr(1, payload.size() - 2)), ',');
    if (operands.size() < 2) {
        return std::nullopt;
    }

    constant.gep_source_type_text = llvm_import_trim_copy(operands[0]);
    const auto source_type =
        parse_llvm_import_type_text(constant.gep_source_type_text);
    if (!source_type.has_value()) {
        return std::nullopt;
    }
    constant.gep_source_type = *source_type;

    const auto base = parse_typed_constant(operands[1]);
    if (!base.has_value() || base->type.kind != AArch64LlvmImportTypeKind::Pointer) {
        return std::nullopt;
    }
    constant.gep_base =
        std::make_shared<AArch64LlvmImportConstant>(std::move(base->constant));

    for (std::size_t index = 2; index < operands.size(); ++index) {
        const auto typed_index = parse_typed_constant(operands[index]);
        if (!typed_index.has_value() ||
            typed_index->type.kind != AArch64LlvmImportTypeKind::Integer ||
            typed_index->constant.kind != AArch64LlvmImportConstantKind::Integer) {
            return std::nullopt;
        }
        constant.gep_index_type_texts.push_back(typed_index->type_text);
        constant.gep_index_types.push_back(typed_index->type);
        constant.gep_indices.push_back(std::move(typed_index->constant));
    }

    return constant;
}

std::optional<std::vector<std::string>> parse_parenthesized_operands(
    const std::string &text, const char *opcode_prefix) {
    std::string payload = llvm_import_trim_copy(text);
    if (!llvm_import_starts_with(payload, opcode_prefix)) {
        return std::nullopt;
    }
    payload = llvm_import_trim_copy(payload.substr(std::strlen(opcode_prefix)));
    if (payload.size() < 2 || payload.front() != '(' || payload.back() != ')') {
        return std::nullopt;
    }
    return llvm_import_split_top_level(
        llvm_import_trim_copy(payload.substr(1, payload.size() - 2)), ',');
}

bool is_vector_import_type(const AArch64LlvmImportType &type) {
    return type.kind == AArch64LlvmImportTypeKind::Array &&
           type.array_uses_vector_syntax && type.element_types.size() == 1;
}

std::optional<std::vector<std::uint8_t>>
parse_llvm_c_string_literal_bytes(const std::string &text) {
    const std::string trimmed = llvm_import_trim_copy(text);
    if (trimmed.size() < 3 || trimmed.front() != 'c' || trimmed[1] != '"' ||
        trimmed.back() != '"') {
        return std::nullopt;
    }

    auto hex_value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        return -1;
    };

    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 2; index + 1 < trimmed.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[index]);
        if (ch != '\\') {
            bytes.push_back(static_cast<std::uint8_t>(ch));
            continue;
        }
        if (index + 2 >= trimmed.size() - 1) {
            return std::nullopt;
        }
        const int hi = hex_value(trimmed[index + 1]);
        const int lo = hex_value(trimmed[index + 2]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        index += 2;
    }
    return bytes;
}

std::optional<AArch64LlvmImportConstant> parse_blockaddress_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (type.kind != AArch64LlvmImportTypeKind::Pointer) {
        return std::nullopt;
    }
    const auto operands = parse_parenthesized_operands(text, "blockaddress");
    if (!operands.has_value() || operands->size() != 2) {
        return std::nullopt;
    }

    std::string function_name = llvm_import_trim_copy((*operands)[0]);
    std::string label_name = llvm_import_trim_copy((*operands)[1]);
    if (function_name.empty() || label_name.empty() || function_name.front() != '@' ||
        label_name.front() != '%') {
        return std::nullopt;
    }
    function_name.erase(function_name.begin());
    label_name.erase(label_name.begin());
    if (function_name.empty() || label_name.empty()) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::BlockAddress;
    constant.blockaddress_function_name = std::move(function_name);
    constant.blockaddress_label_name = std::move(label_name);
    return constant;
}

bool is_i1_import_type(const AArch64LlvmImportType &type) {
    return type.kind == AArch64LlvmImportTypeKind::Integer &&
           type.integer_bit_width == 1;
}

bool is_vector_bool_import_type(const AArch64LlvmImportType &type) {
    return is_vector_import_type(type) &&
           is_i1_import_type(type.element_types.front());
}

bool is_integer_or_pointer_import_type(const AArch64LlvmImportType &type) {
    return is_integer_import_type(type) ||
           type.kind == AArch64LlvmImportTypeKind::Pointer;
}

bool is_compare_operand_type(const AArch64LlvmImportType &type, bool is_float_compare) {
    if (is_float_compare) {
        if (is_float_import_type(type)) {
            return true;
        }
        return is_vector_import_type(type) &&
               is_float_import_type(type.element_types.front());
    }
    if (is_integer_or_pointer_import_type(type)) {
        return true;
    }
    return is_vector_import_type(type) &&
           is_integer_or_pointer_import_type(type.element_types.front());
}

std::optional<AArch64LlvmImportConstant> parse_compare_constant(
    const AArch64LlvmImportType &result_type, const std::string &text,
    bool is_float_compare) {
    const char *opcode_prefix = is_float_compare ? "fcmp " : "icmp ";
    std::string payload = llvm_import_trim_copy(text);
    if (!llvm_import_starts_with(payload, opcode_prefix)) {
        return std::nullopt;
    }
    payload = llvm_import_trim_copy(payload.substr(std::strlen(opcode_prefix)));
    const std::size_t predicate_end = payload.find(' ');
    if (predicate_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string predicate_text = payload.substr(0, predicate_end);
    const auto operands = parse_parenthesized_operands(
        llvm_import_trim_copy(payload.substr(predicate_end + 1)), "");
    if (!operands.has_value() || operands->size() != 2) {
        return std::nullopt;
    }

    const auto lhs_operand = parse_typed_constant((*operands)[0]);
    const auto rhs_operand = parse_typed_constant((*operands)[1]);
    if (!lhs_operand.has_value() || !rhs_operand.has_value() ||
        !import_types_equal(lhs_operand->type, rhs_operand->type) ||
        !is_compare_operand_type(lhs_operand->type, is_float_compare)) {
        return std::nullopt;
    }

    const bool scalar_result = is_i1_import_type(result_type);
    const bool vector_result =
        is_vector_bool_import_type(result_type) &&
        is_vector_import_type(lhs_operand->type) &&
        lhs_operand->type.array_element_count == result_type.array_element_count;
    if ((!scalar_result || is_vector_import_type(lhs_operand->type)) &&
        !vector_result) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::Compare;
    constant.compare_is_float = is_float_compare;
    constant.compare_predicate_text = predicate_text;
    constant.compare_lhs_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*lhs_operand);
    constant.compare_rhs_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*rhs_operand);
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_select_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    const auto operands = parse_parenthesized_operands(text, "select ");
    if (!operands.has_value() || operands->size() != 3) {
        return std::nullopt;
    }
    const auto condition_operand = parse_typed_constant((*operands)[0]);
    const auto true_operand = parse_typed_constant((*operands)[1]);
    const auto false_operand = parse_typed_constant((*operands)[2]);
    if (!condition_operand.has_value() || !true_operand.has_value() ||
        !false_operand.has_value()) {
        return std::nullopt;
    }
    if (!import_types_equal(true_operand->type, type) ||
        !import_types_equal(false_operand->type, type)) {
        return std::nullopt;
    }

    const bool scalar_condition = is_i1_import_type(condition_operand->type);
    const bool vector_condition =
        is_vector_import_type(condition_operand->type) &&
        condition_operand->type.array_element_count == type.array_element_count &&
        condition_operand->type.element_types.size() == 1 &&
        is_i1_import_type(condition_operand->type.element_types.front()) &&
        is_vector_import_type(type);
    if (!scalar_condition && !vector_condition) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::Select;
    constant.select_condition_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*condition_operand);
    constant.select_true_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*true_operand);
    constant.select_false_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*false_operand);
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_extractelement_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    const auto operands =
        parse_parenthesized_operands(text, "extractelement ");
    if (!operands.has_value() || operands->size() != 2) {
        return std::nullopt;
    }
    const auto vector_operand = parse_typed_constant((*operands)[0]);
    const auto index_operand = parse_typed_constant((*operands)[1]);
    if (!vector_operand.has_value() || !index_operand.has_value() ||
        !is_vector_import_type(vector_operand->type) ||
        index_operand->type.kind != AArch64LlvmImportTypeKind::Integer ||
        vector_operand->type.element_types.front().kind != type.kind) {
        return std::nullopt;
    }
    if (!import_types_equal(vector_operand->type.element_types.front(), type)) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::ExtractElement;
    constant.extract_vector_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*vector_operand);
    constant.extract_index_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*index_operand);
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_insertelement_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (!is_vector_import_type(type)) {
        return std::nullopt;
    }
    const auto operands =
        parse_parenthesized_operands(text, "insertelement ");
    if (!operands.has_value() || operands->size() != 3) {
        return std::nullopt;
    }
    const auto vector_operand = parse_typed_constant((*operands)[0]);
    const auto element_operand = parse_typed_constant((*operands)[1]);
    const auto index_operand = parse_typed_constant((*operands)[2]);
    if (!vector_operand.has_value() || !element_operand.has_value() ||
        !index_operand.has_value() || !is_vector_import_type(vector_operand->type) ||
        index_operand->type.kind != AArch64LlvmImportTypeKind::Integer) {
        return std::nullopt;
    }
    if (!import_types_equal(vector_operand->type, type) ||
        !import_types_equal(element_operand->type, type.element_types.front())) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::InsertElement;
    constant.insert_vector_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*vector_operand);
    constant.insert_element_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*element_operand);
    constant.insert_index_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*index_operand);
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_shufflevector_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (!is_vector_import_type(type)) {
        return std::nullopt;
    }
    const auto operands =
        parse_parenthesized_operands(text, "shufflevector ");
    if (!operands.has_value() || operands->size() != 3) {
        return std::nullopt;
    }
    const auto lhs_operand = parse_typed_constant((*operands)[0]);
    const auto rhs_operand = parse_typed_constant((*operands)[1]);
    const auto mask_operand = parse_typed_constant((*operands)[2]);
    if (!lhs_operand.has_value() || !rhs_operand.has_value() ||
        !mask_operand.has_value() || !is_vector_import_type(lhs_operand->type) ||
        !is_vector_import_type(rhs_operand->type) ||
        !is_vector_import_type(mask_operand->type)) {
        return std::nullopt;
    }
    if (!import_types_equal(lhs_operand->type, rhs_operand->type) ||
        !import_types_equal(lhs_operand->type.element_types.front(),
                            type.element_types.front()) ||
        mask_operand->type.element_types.front().kind !=
            AArch64LlvmImportTypeKind::Integer ||
        mask_operand->type.array_element_count != type.array_element_count) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = AArch64LlvmImportConstantKind::ShuffleVector;
    constant.shuffle_lhs_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*lhs_operand);
    constant.shuffle_rhs_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*rhs_operand);
    constant.shuffle_mask_operand =
        std::make_shared<AArch64LlvmImportTypedConstant>(*mask_operand);
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_cast_constant(
    const AArch64LlvmImportType &target_type, const std::string &text,
    const char *opcode_prefix, AArch64LlvmImportConstantKind kind,
    bool (*supports_cast)(const AArch64LlvmImportType &,
                          const AArch64LlvmImportType &)) {
    std::string payload = llvm_import_trim_copy(text);
    if (!llvm_import_starts_with(payload, opcode_prefix)) {
        return std::nullopt;
    }
    payload = llvm_import_trim_copy(
        payload.substr(std::strlen(opcode_prefix)));
    if (payload.size() < 2 || payload.front() != '(' || payload.back() != ')') {
        return std::nullopt;
    }

    payload = llvm_import_trim_copy(payload.substr(1, payload.size() - 2));
    const auto to_pos = find_top_level_separator(payload, " to ");
    if (!to_pos.has_value()) {
        return std::nullopt;
    }

    const std::string source_text = llvm_import_trim_copy(payload.substr(0, *to_pos));
    const std::string target_type_text =
        llvm_import_trim_copy(payload.substr(*to_pos + 4));
    const auto parsed_target_type = parse_llvm_import_type_text(target_type_text);
    if (!parsed_target_type.has_value() ||
        !import_types_equal(*parsed_target_type, target_type)) {
        return std::nullopt;
    }

    const auto source = parse_typed_constant(source_text);
    if (!source.has_value() || supports_cast == nullptr ||
        !supports_cast(source->type, target_type)) {
        return std::nullopt;
    }

    AArch64LlvmImportConstant constant;
    constant.kind = kind;
    constant.cast_source_type_text = source->type_text;
    constant.cast_source_type = source->type;
    constant.cast_target_type_text = target_type_text;
    constant.cast_operand =
        std::make_shared<AArch64LlvmImportConstant>(std::move(source->constant));
    return constant;
}

std::optional<AArch64LlvmImportConstant> parse_bitcast_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "bitcast ",
                               AArch64LlvmImportConstantKind::Bitcast,
                               supports_bitcast);
}

std::optional<AArch64LlvmImportConstant> parse_addrspacecast_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (type.kind != AArch64LlvmImportTypeKind::Pointer) {
        return std::nullopt;
    }
    return parse_cast_constant(type, text, "addrspacecast ",
                               AArch64LlvmImportConstantKind::AddrSpaceCast,
                               supports_addrspace_cast);
}

std::optional<AArch64LlvmImportConstant> parse_inttoptr_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (type.kind != AArch64LlvmImportTypeKind::Pointer) {
        return std::nullopt;
    }
    return parse_cast_constant(type, text, "inttoptr ",
                               AArch64LlvmImportConstantKind::IntToPtr,
                               [](const AArch64LlvmImportType &source_type,
                                  const AArch64LlvmImportType &target_type) {
                                   return source_type.kind ==
                                              AArch64LlvmImportTypeKind::Integer &&
                                          target_type.kind ==
                                              AArch64LlvmImportTypeKind::Pointer;
                               });
}

std::optional<AArch64LlvmImportConstant> parse_ptrtoint_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    if (type.kind != AArch64LlvmImportTypeKind::Integer) {
        return std::nullopt;
    }
    return parse_cast_constant(type, text, "ptrtoint ",
                               AArch64LlvmImportConstantKind::PtrToInt,
                               [](const AArch64LlvmImportType &source_type,
                                  const AArch64LlvmImportType &target_type) {
                                   return source_type.kind ==
                                              AArch64LlvmImportTypeKind::Pointer &&
                                          target_type.kind ==
                                              AArch64LlvmImportTypeKind::Integer;
                               });
}

std::optional<AArch64LlvmImportConstant> parse_sext_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "sext ",
                               AArch64LlvmImportConstantKind::SignExtend,
                               supports_integer_cast);
}

std::optional<AArch64LlvmImportConstant> parse_zext_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "zext ",
                               AArch64LlvmImportConstantKind::ZeroExtend,
                               supports_integer_cast);
}

std::optional<AArch64LlvmImportConstant> parse_trunc_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "trunc ",
                               AArch64LlvmImportConstantKind::Truncate,
                               supports_integer_cast);
}

std::optional<AArch64LlvmImportConstant> parse_sitofp_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "sitofp ",
                               AArch64LlvmImportConstantKind::SignedIntToFloat,
                               supports_int_to_float_cast);
}

std::optional<AArch64LlvmImportConstant> parse_uitofp_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "uitofp ",
                               AArch64LlvmImportConstantKind::UnsignedIntToFloat,
                               supports_int_to_float_cast);
}

std::optional<AArch64LlvmImportConstant> parse_fptosi_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "fptosi ",
                               AArch64LlvmImportConstantKind::FloatToSignedInt,
                               supports_float_to_int_cast);
}

std::optional<AArch64LlvmImportConstant> parse_fptoui_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "fptoui ",
                               AArch64LlvmImportConstantKind::FloatToUnsignedInt,
                               supports_float_to_int_cast);
}

std::optional<AArch64LlvmImportConstant> parse_fpext_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "fpext ",
                               AArch64LlvmImportConstantKind::FloatExtend,
                               supports_float_cast);
}

std::optional<AArch64LlvmImportConstant> parse_fptrunc_constant(
    const AArch64LlvmImportType &type, const std::string &text) {
    return parse_cast_constant(type, text, "fptrunc ",
                               AArch64LlvmImportConstantKind::FloatTruncate,
                               supports_float_cast);
}

std::optional<AArch64LlvmImportConstant> parse_constant_impl(
    const AArch64LlvmImportType &type, const std::string &text) {
    const std::string trimmed =
        strip_trailing_constant_suffixes(llvm_import_trim_copy(text));
    if (trimmed == "zeroinitializer") {
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::ZeroInitializer;
        return constant;
    }
    if (trimmed == "undef") {
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::UndefValue;
        return constant;
    }
    if (trimmed == "poison") {
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::PoisonValue;
        return constant;
    }

    switch (type.kind) {
    case AArch64LlvmImportTypeKind::Integer: {
        if (llvm_import_starts_with(trimmed, "icmp ") ||
            llvm_import_starts_with(trimmed, "fcmp ")) {
            return parse_compare_constant(
                type, trimmed, llvm_import_starts_with(trimmed, "fcmp "));
        }
        if (llvm_import_starts_with(trimmed, "select ")) {
            return parse_select_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "extractelement ")) {
            return parse_extractelement_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "bitcast ")) {
            return parse_bitcast_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "trunc ")) {
            return parse_trunc_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "zext ")) {
            return parse_zext_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "sext ")) {
            return parse_sext_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "fptosi ")) {
            return parse_fptosi_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "fptoui ")) {
            return parse_fptoui_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "ptrtoint ")) {
            return parse_ptrtoint_constant(type, trimmed);
        }
        if (type.integer_bit_width > 64 && type.integer_bit_width <= 128) {
            const auto words = parse_integer_literal_words_128(trimmed);
            if (!words.has_value()) {
                return std::nullopt;
            }
            unsigned __int128 combined =
                (static_cast<unsigned __int128>(words->second) << 64U) |
                static_cast<unsigned __int128>(words->first);
            if (type.integer_bit_width < 128) {
                const unsigned __int128 mask =
                    (static_cast<unsigned __int128>(1) << type.integer_bit_width) -
                    1;
                combined &= mask;
            }
            AArch64LlvmImportConstant constant;
            constant.kind = AArch64LlvmImportConstantKind::Aggregate;
            AArch64LlvmImportConstant low;
            low.kind = AArch64LlvmImportConstantKind::Integer;
            low.integer_value = static_cast<std::uint64_t>(combined);
            constant.elements.push_back(low);
            AArch64LlvmImportConstant high;
            high.kind = AArch64LlvmImportConstantKind::Integer;
            high.integer_value = static_cast<std::uint64_t>(combined >> 64U);
            constant.elements.push_back(high);
            return constant;
        }
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
        if (llvm_import_starts_with(trimmed, "select ")) {
            return parse_select_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "extractelement ")) {
            return parse_extractelement_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "trunc ")) {
            return parse_trunc_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "sitofp ")) {
            return parse_sitofp_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "uitofp ")) {
            return parse_uitofp_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "fpext ")) {
            return parse_fpext_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "fptrunc ")) {
            return parse_fptrunc_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "bitcast ")) {
            return parse_bitcast_constant(type, trimmed);
        }
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
        if (llvm_import_starts_with(trimmed, "select ")) {
            return parse_select_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "extractelement ")) {
            return parse_extractelement_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "blockaddress")) {
            return parse_blockaddress_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "inttoptr ")) {
            return parse_inttoptr_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "addrspacecast ")) {
            return parse_addrspacecast_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "bitcast ")) {
            return parse_bitcast_constant(type, trimmed);
        }
        if (llvm_import_starts_with(trimmed, "getelementptr ")) {
            return parse_gep_constant(type, trimmed);
        }
        if (trimmed == "null") {
            AArch64LlvmImportConstant constant;
            constant.kind = AArch64LlvmImportConstantKind::NullPointer;
            return constant;
        }
        if (!trimmed.empty() && trimmed.front() == '@') {
            AArch64LlvmImportConstant constant;
            constant.kind = AArch64LlvmImportConstantKind::SymbolReference;
            constant.symbol_name = trimmed.substr(1);
            return constant;
        }
        return std::nullopt;
    }
    case AArch64LlvmImportTypeKind::Array: {
        if (llvm_import_starts_with(trimmed, "icmp ") ||
            llvm_import_starts_with(trimmed, "fcmp ")) {
            return parse_compare_constant(
                type, trimmed, llvm_import_starts_with(trimmed, "fcmp "));
        }
        if (llvm_import_starts_with(trimmed, "select ")) {
            return parse_select_constant(type, trimmed);
        }
        if (type.array_uses_vector_syntax) {
            if (const auto splat = parse_vector_splat_constant(type, trimmed);
                splat.has_value()) {
                return splat;
            }
            if (llvm_import_starts_with(trimmed, "insertelement ")) {
                return parse_insertelement_constant(type, trimmed);
            }
            if (llvm_import_starts_with(trimmed, "shufflevector ")) {
                return parse_shufflevector_constant(type, trimmed);
            }
        }
        if (type.element_types.size() == 1 &&
            type.element_types.front().kind == AArch64LlvmImportTypeKind::Integer &&
            type.element_types.front().integer_bit_width == 8) {
            const auto string_bytes = parse_llvm_c_string_literal_bytes(trimmed);
            if (string_bytes.has_value()) {
                if (string_bytes->size() != type.array_element_count) {
                    return std::nullopt;
                }
                AArch64LlvmImportConstant constant;
                constant.kind = AArch64LlvmImportConstantKind::Aggregate;
                for (std::uint8_t byte : *string_bytes) {
                    AArch64LlvmImportConstant element;
                    element.kind = AArch64LlvmImportConstantKind::Integer;
                    element.integer_value = byte;
                    constant.elements.push_back(std::move(element));
                }
                return constant;
            }
        }
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
    case AArch64LlvmImportTypeKind::Named: {
        const bool square_bracketed =
            trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']';
        const bool brace_bracketed =
            trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
        if (!square_bracketed && !brace_bracketed) {
            return std::nullopt;
        }
        AArch64LlvmImportConstant constant;
        constant.kind = AArch64LlvmImportConstantKind::Aggregate;
        const std::string inner =
            llvm_import_trim_copy(trimmed.substr(1, trimmed.size() - 2));
        for (const std::string &element_entry :
             llvm_import_split_top_level(inner, ',')) {
            if (element_entry.empty()) {
                continue;
            }
            const auto typed_element = parse_typed_constant(element_entry);
            if (!typed_element.has_value()) {
                return std::nullopt;
            }
            constant.elements.push_back(typed_element->constant);
        }
        return constant;
    }
    case AArch64LlvmImportTypeKind::Struct: {
        const bool brace_bracketed =
            trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
        const bool packed_bracketed =
            trimmed.size() >= 4 && trimmed.front() == '<' && trimmed[1] == '{' &&
            trimmed[trimmed.size() - 2] == '}' && trimmed.back() == '>';
        if (!brace_bracketed && !packed_bracketed) {
            return std::nullopt;
        }
        const std::string inner =
            brace_bracketed
                ? llvm_import_trim_copy(trimmed.substr(1, trimmed.size() - 2))
                : llvm_import_trim_copy(trimmed.substr(2, trimmed.size() - 4));
        const std::vector<std::string> entries = llvm_import_split_top_level(
            inner, ',');
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
