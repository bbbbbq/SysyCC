#include "backend/asm_gen/aarch64/support/aarch64_import_model_core_ir_bridge.hpp"

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_instruction_parse_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"

#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"

namespace sysycc {

namespace {

using TypeCache = std::unordered_map<std::string, const CoreIrType *>;

std::string trim_copy(const std::string &text) {
    return llvm_import_trim_copy(text);
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return llvm_import_starts_with(text, prefix);
}

bool is_identifier_char(char ch) {
    return llvm_import_is_identifier_char(ch);
}

std::string strip_comment(const std::string &line) {
    return llvm_import_strip_comment(line);
}

std::optional<std::string> unquote_llvm_string_literal(const std::string &text) {
    return llvm_import_unquote_string_literal(text);
}

std::vector<std::string> split_top_level(const std::string &text,
                                         char delimiter) {
    return llvm_import_split_top_level(text, delimiter);
}

std::string strip_trailing_alignment_suffix(const std::string &text) {
    return llvm_import_strip_trailing_alignment_suffix(text);
}

std::optional<std::string> consume_type_token(const std::string &text,
                                              std::size_t &position) {
    return llvm_import_consume_type_token(text, position);
}

std::optional<std::string> parse_symbol_name(const std::string &text,
                                             std::size_t &position,
                                             char prefix) {
    return llvm_import_parse_symbol_name(text, position, prefix);
}

std::optional<std::uint64_t> parse_integer_literal(const std::string &text) {
    return llvm_import_parse_integer_literal(text);
}

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

std::optional<std::string> canonicalize_float128_hex_literal_text(
    const std::string &trimmed) {
    if (!starts_with(trimmed, "0xL") && !starts_with(trimmed, "0XL")) {
        return std::nullopt;
    }
    const std::string payload = trimmed.substr(3);
    if (payload.size() != 32) {
        return std::nullopt;
    }

    try {
        const std::uint64_t low =
            static_cast<std::uint64_t>(std::stoull(payload.substr(0, 16), nullptr, 16));
        const std::uint64_t high =
            static_cast<std::uint64_t>(std::stoull(payload.substr(16, 16), nullptr, 16));
        const bool negative = (high >> 63U) != 0;
        const std::uint16_t exponent =
            static_cast<std::uint16_t>((high >> 48U) & 0x7fffU);
        const std::uint64_t fraction_high = high & 0x0000FFFFFFFFFFFFULL;
        if (low == 0 && fraction_high == 0) {
            if (exponent == 0) {
                return negative ? std::optional<std::string>("-0.0")
                                : std::optional<std::string>("0.0");
            }
            if (exponent == 0x7fffU) {
                return negative ? std::optional<std::string>("-inf")
                                : std::optional<std::string>("inf");
            }
            const int unbiased_exponent = static_cast<int>(exponent) - 16383;
            return format_float_literal(
                negative ? -std::ldexp(1.0L, unbiased_exponent)
                         : std::ldexp(1.0L, unbiased_exponent));
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> canonicalize_float_literal_text(
    CoreIrFloatKind kind, const std::string &literal_text) {
    const std::string trimmed = trim_copy(literal_text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (starts_with(trimmed, "0x") || starts_with(trimmed, "-0x") ||
        starts_with(trimmed, "+0x") || starts_with(trimmed, "0X") ||
        starts_with(trimmed, "-0X") || starts_with(trimmed, "+0X")) {
        if (kind == CoreIrFloatKind::Float128) {
            if (const auto normalized = canonicalize_float128_hex_literal_text(trimmed);
                normalized.has_value()) {
                return normalized;
            }
        }
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
            if (kind == CoreIrFloatKind::Float16) {
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
            if (kind == CoreIrFloatKind::Float32) {
                const std::uint32_t float_bits =
                    static_cast<std::uint32_t>(bits & 0xffffffffU);
                float value = 0.0F;
                std::memcpy(&value, &float_bits, sizeof(value));
                return format_float_literal(static_cast<long double>(value));
            }
            if (kind == CoreIrFloatKind::Float64) {
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
import_type_storage_bit_width(const AArch64LlvmImportType &type) {
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

std::optional<CoreIrFloatKind>
import_type_to_core_float_kind(const AArch64LlvmImportType &type) {
    switch (type.kind) {
    case AArch64LlvmImportTypeKind::Float16:
        return CoreIrFloatKind::Float16;
    case AArch64LlvmImportTypeKind::Float32:
        return CoreIrFloatKind::Float32;
    case AArch64LlvmImportTypeKind::Float64:
        return CoreIrFloatKind::Float64;
    case AArch64LlvmImportTypeKind::Float128:
        return CoreIrFloatKind::Float128;
    default:
        return std::nullopt;
    }
}

std::optional<std::size_t> integer_type_bit_width(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    if (integer_type == nullptr) {
        return std::nullopt;
    }
    return integer_type->get_bit_width();
}

std::uint64_t truncate_integer_to_width(std::uint64_t value, std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    if (bit_width >= 64) {
        return value;
    }
    return value & ((1ULL << bit_width) - 1ULL);
}

std::uint64_t sign_extend_integer_to_u64(std::uint64_t value,
                                         std::size_t source_bit_width) {
    value = truncate_integer_to_width(value, source_bit_width);
    if (source_bit_width == 0 || source_bit_width >= 64) {
        return value;
    }
    const std::uint64_t sign_bit = 1ULL << (source_bit_width - 1U);
    const std::uint64_t mask = (1ULL << source_bit_width) - 1ULL;
    return (value & sign_bit) != 0 ? (value | ~mask) : value;
}

std::optional<long double> parse_core_float_literal(const CoreIrConstantFloat &constant) {
    char *end = nullptr;
    const long double parsed =
        std::strtold(constant.get_literal_text().c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> format_float_literal_for_kind(CoreIrFloatKind kind,
                                                         long double value) {
    switch (kind) {
    case CoreIrFloatKind::Float16:
        return format_float_literal(value);
    case CoreIrFloatKind::Float32:
        return format_float_literal(static_cast<long double>(static_cast<float>(value)));
    case CoreIrFloatKind::Float64:
        return format_float_literal(static_cast<long double>(static_cast<double>(value)));
    case CoreIrFloatKind::Float128:
        return format_float_literal(value);
    default:
        return std::nullopt;
    }
}

std::string format_hex_bits(std::uint64_t bits) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << bits;
    return stream.str();
}

std::optional<std::uint64_t> encode_float_literal_bits(const CoreIrConstantFloat &constant,
                                                       CoreIrFloatKind kind) {
    const auto parsed = parse_core_float_literal(constant);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    switch (kind) {
    case CoreIrFloatKind::Float32: {
        const float narrowed = static_cast<float>(*parsed);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &narrowed, sizeof(bits));
        return bits;
    }
    case CoreIrFloatKind::Float64:
    {
        const double narrowed = static_cast<double>(*parsed);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &narrowed, sizeof(bits));
        return bits;
    }
    case CoreIrFloatKind::Float128:
        return std::nullopt;
    case CoreIrFloatKind::Float16:
    default:
        return std::nullopt;
    }
}

class RestrictedLlvmIrImporter {
  private:
    struct ParameterSpec {
        const CoreIrType *type = nullptr;
        std::string name;
    };

    struct PendingFunctionDefinition {
        CoreIrFunction *function = nullptr;
        std::vector<ParameterSpec> parameters;
        std::vector<AArch64LlvmImportBasicBlock> basic_blocks;
    };

    struct ValueBinding {
        CoreIrValue *value = nullptr;
        CoreIrStackSlot *stack_slot = nullptr;
    };

    struct ResolvedAddress {
        CoreIrValue *address_value = nullptr;
        CoreIrStackSlot *stack_slot = nullptr;
    };

    std::string file_path_;
    std::unique_ptr<CoreIrContext> context_ = std::make_unique<CoreIrContext>();
    CoreIrModule *module_ = nullptr;
    TypeCache type_cache_;
    std::unordered_map<const CoreIrType *, const CoreIrType *> pointer_type_cache_;
    std::unordered_map<std::string, const CoreIrType *> named_type_cache_;
    std::unordered_map<std::string, CoreIrGlobal *> globals_;
    std::unordered_map<std::string, CoreIrFunction *> functions_;
    std::vector<std::string> module_asm_lines_;
    std::vector<PendingFunctionDefinition> pending_definitions_;
    std::vector<AArch64CodegenDiagnostic> diagnostics_;
    std::string source_target_triple_;

    const CoreIrType *void_type() {
        auto it = type_cache_.find("void");
        if (it != type_cache_.end()) {
            return it->second;
        }
        const CoreIrType *type = context_->create_type<CoreIrVoidType>();
        type_cache_.emplace("void", type);
        return type;
    }

    void add_error(std::string message, int line = 0, int column = 0) {
        AArch64CodegenDiagnostic diagnostic;
        diagnostic.severity = AArch64CodegenDiagnosticSeverity::Error;
        diagnostic.stage_name = "llvm-import";
        diagnostic.message = std::move(message);
        diagnostic.file_path = file_path_;
        diagnostic.line = line;
        diagnostic.column = column;
        diagnostics_.push_back(std::move(diagnostic));
    }

    bool is_modifier_token(const std::string &token) const {
        return llvm_import_is_modifier_token(token);
    }

    std::string strip_leading_modifiers(const std::string &text) const {
        return llvm_import_strip_leading_modifiers(text);
    }

    std::string strip_metadata_suffix(const std::string &text) const {
        return llvm_import_strip_metadata_suffix(text);
    }

    const CoreIrType *parse_type_text(const std::string &type_text) {
        const std::string normalized = trim_copy(type_text);
        if (normalized.empty()) {
            return nullptr;
        }
        if (!normalized.empty() && normalized.front() == '%') {
            const auto named_type_it = named_type_cache_.find(normalized.substr(1));
            return named_type_it == named_type_cache_.end() ? nullptr
                                                            : named_type_it->second;
        }
        if (auto it = type_cache_.find(normalized); it != type_cache_.end()) {
            return it->second;
        }

        const CoreIrType *type = nullptr;
        if (normalized == "void") {
            type = void_type();
        } else if (normalized == "ptr") {
            type = context_->create_type<CoreIrPointerType>(void_type());
        } else if (normalized == "half") {
            type = context_->create_type<CoreIrFloatType>(CoreIrFloatKind::Float16);
        } else if (normalized == "float") {
            type = context_->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
        } else if (normalized == "double") {
            type = context_->create_type<CoreIrFloatType>(CoreIrFloatKind::Float64);
        } else if (normalized == "fp128") {
            type = context_->create_type<CoreIrFloatType>(CoreIrFloatKind::Float128);
        } else if (normalized.size() > 1 && normalized[0] == 'i') {
            try {
                const std::size_t bit_width =
                    static_cast<std::size_t>(std::stoull(normalized.substr(1)));
                type = context_->create_type<CoreIrIntegerType>(bit_width);
            } catch (...) {
                return nullptr;
            }
        } else if (normalized.front() == '<' && normalized.back() == '>') {
            const std::string inner =
                trim_copy(normalized.substr(1, normalized.size() - 2));
            const std::size_t x_pos = inner.find('x');
            if (x_pos == std::string::npos) {
                return nullptr;
            }
            std::size_t element_count = 0;
            try {
                element_count = static_cast<std::size_t>(
                    std::stoull(trim_copy(inner.substr(0, x_pos))));
            } catch (...) {
                return nullptr;
            }
            const CoreIrType *element_type =
                parse_type_text(trim_copy(inner.substr(x_pos + 1)));
            if (element_type == nullptr) {
                return nullptr;
            }
            type = context_->create_type<CoreIrArrayType>(element_type,
                                                          element_count);
        } else if (normalized.front() == '[' && normalized.back() == ']') {
            const std::string inner =
                trim_copy(normalized.substr(1, normalized.size() - 2));
            const std::size_t x_pos = inner.find('x');
            if (x_pos == std::string::npos) {
                return nullptr;
            }
            std::size_t element_count = 0;
            try {
                element_count = static_cast<std::size_t>(
                    std::stoull(trim_copy(inner.substr(0, x_pos))));
            } catch (...) {
                return nullptr;
            }
            const CoreIrType *element_type =
                parse_type_text(trim_copy(inner.substr(x_pos + 1)));
            if (element_type == nullptr) {
                return nullptr;
            }
            type = context_->create_type<CoreIrArrayType>(element_type,
                                                          element_count);
        } else if (normalized.front() == '{' && normalized.back() == '}') {
            const std::string inner =
                trim_copy(normalized.substr(1, normalized.size() - 2));
            std::vector<const CoreIrType *> element_types;
            for (const std::string &element_text : split_top_level(inner, ',')) {
                if (element_text.empty()) {
                    continue;
                }
                const CoreIrType *element_type = parse_type_text(element_text);
                if (element_type == nullptr) {
                    return nullptr;
                }
                element_types.push_back(element_type);
            }
            type = context_->create_type<CoreIrStructType>(element_types);
        } else {
            return nullptr;
        }

        type_cache_.emplace(normalized, type);
        return type;
    }

    std::string import_type_cache_key(const AArch64LlvmImportType &type) const {
        switch (type.kind) {
        case AArch64LlvmImportTypeKind::Void:
            return "void";
        case AArch64LlvmImportTypeKind::Pointer:
            return "ptr";
        case AArch64LlvmImportTypeKind::Float16:
            return "half";
        case AArch64LlvmImportTypeKind::Float32:
            return "float";
        case AArch64LlvmImportTypeKind::Float64:
            return "double";
        case AArch64LlvmImportTypeKind::Float128:
            return "fp128";
        case AArch64LlvmImportTypeKind::Integer:
            return "i" + std::to_string(type.integer_bit_width);
        case AArch64LlvmImportTypeKind::Named:
            return "%" + type.named_type_name;
        case AArch64LlvmImportTypeKind::Array: {
            if (type.element_types.empty()) {
                return {};
            }
            const std::string inner = import_type_cache_key(type.element_types.front());
            if (inner.empty()) {
                return {};
            }
            return std::string(type.array_uses_vector_syntax ? "<" : "[") +
                   std::to_string(type.array_element_count) + " x " + inner +
                   (type.array_uses_vector_syntax ? ">" : "]");
        }
        case AArch64LlvmImportTypeKind::Struct: {
            std::string key = "{";
            for (std::size_t index = 0; index < type.element_types.size(); ++index) {
                if (index != 0) {
                    key += ", ";
                }
                const std::string inner =
                    import_type_cache_key(type.element_types[index]);
                if (inner.empty()) {
                    return {};
                }
                key += inner;
            }
            key += "}";
            return key;
        }
        case AArch64LlvmImportTypeKind::Unknown:
        default:
            return {};
        }
    }

    const CoreIrType *lower_import_type(const AArch64LlvmImportType &type) {
        if (!type.is_valid()) {
            return nullptr;
        }
        if (type.kind == AArch64LlvmImportTypeKind::Named) {
            const auto it = named_type_cache_.find(type.named_type_name);
            return it == named_type_cache_.end() ? nullptr : it->second;
        }

        const std::string key = import_type_cache_key(type);
        if (!key.empty()) {
            if (auto it = type_cache_.find(key); it != type_cache_.end()) {
                return it->second;
            }
        }

        const CoreIrType *lowered = nullptr;
        switch (type.kind) {
        case AArch64LlvmImportTypeKind::Void:
            lowered = void_type();
            break;
        case AArch64LlvmImportTypeKind::Pointer:
            lowered = context_->create_type<CoreIrPointerType>(void_type());
            break;
        case AArch64LlvmImportTypeKind::Float16:
            lowered = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float16);
            break;
        case AArch64LlvmImportTypeKind::Float32:
            lowered = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float32);
            break;
        case AArch64LlvmImportTypeKind::Float64:
            lowered = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float64);
            break;
        case AArch64LlvmImportTypeKind::Float128:
            lowered = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float128);
            break;
        case AArch64LlvmImportTypeKind::Integer:
            lowered = context_->create_type<CoreIrIntegerType>(
                type.integer_bit_width);
            break;
        case AArch64LlvmImportTypeKind::Array: {
            if (type.element_types.size() != 1) {
                return nullptr;
            }
            const CoreIrType *element_type = lower_import_type(type.element_types.front());
            if (element_type == nullptr) {
                return nullptr;
            }
            lowered = context_->create_type<CoreIrArrayType>(
                element_type, type.array_element_count);
            break;
        }
        case AArch64LlvmImportTypeKind::Struct: {
            std::vector<const CoreIrType *> element_types;
            for (const AArch64LlvmImportType &element : type.element_types) {
                const CoreIrType *element_type = lower_import_type(element);
                if (element_type == nullptr) {
                    return nullptr;
                }
                element_types.push_back(element_type);
            }
            lowered = context_->create_type<CoreIrStructType>(element_types);
            break;
        }
        case AArch64LlvmImportTypeKind::Named:
        case AArch64LlvmImportTypeKind::Unknown:
        default:
            return nullptr;
        }

        if (!key.empty() && lowered != nullptr) {
            type_cache_.emplace(key, lowered);
        }
        return lowered;
    }

    const CoreIrType *pointer_to(const CoreIrType *pointee_type) {
        if (pointee_type == nullptr) {
            return nullptr;
        }
        if (auto it = pointer_type_cache_.find(pointee_type);
            it != pointer_type_cache_.end()) {
            return it->second;
        }
        const CoreIrType *pointer_type =
            context_->create_type<CoreIrPointerType>(pointee_type);
        pointer_type_cache_.emplace(pointee_type, pointer_type);
        return pointer_type;
    }

    const CoreIrConstantGlobalAddress *unwrap_alias_target_global_address(
        const CoreIrConstant *constant) const {
        if (constant == nullptr) {
            return nullptr;
        }
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
            global_address != nullptr) {
            return global_address;
        }
        const auto *cast_constant =
            dynamic_cast<const CoreIrConstantCast *>(constant);
        if (cast_constant == nullptr) {
            return nullptr;
        }
        if (cast_constant->get_cast_kind() == CoreIrCastKind::IntToPtr) {
            if (const auto *nested_cast =
                    dynamic_cast<const CoreIrConstantCast *>(
                        cast_constant->get_operand());
                nested_cast != nullptr &&
                nested_cast->get_cast_kind() == CoreIrCastKind::PtrToInt) {
                return unwrap_alias_target_global_address(
                    nested_cast->get_operand());
            }
        }
        return nullptr;
    }

    const CoreIrConstant *fold_scalar_constant_cast(
        const CoreIrType *type, const AArch64LlvmImportConstant &constant,
        const CoreIrConstant *operand) {
        if (type == nullptr || operand == nullptr) {
            return nullptr;
        }

        switch (constant.kind) {
        case AArch64LlvmImportConstantKind::SignExtend:
        case AArch64LlvmImportConstantKind::ZeroExtend:
        case AArch64LlvmImportConstantKind::Truncate: {
            const auto *int_operand = dynamic_cast<const CoreIrConstantInt *>(operand);
            const auto target_width = integer_type_bit_width(type);
            if (int_operand == nullptr || !target_width.has_value()) {
                return nullptr;
            }
            const std::uint64_t source_value = truncate_integer_to_width(
                int_operand->get_value(), constant.cast_source_type.integer_bit_width);
            std::uint64_t folded = source_value;
            if (constant.kind == AArch64LlvmImportConstantKind::SignExtend) {
                folded = sign_extend_integer_to_u64(
                    source_value, constant.cast_source_type.integer_bit_width);
            } else if (constant.kind ==
                       AArch64LlvmImportConstantKind::Truncate) {
                folded = truncate_integer_to_width(source_value, *target_width);
            }
            folded = truncate_integer_to_width(folded, *target_width);
            return context_->create_constant<CoreIrConstantInt>(type, folded);
        }
        case AArch64LlvmImportConstantKind::SignedIntToFloat:
        case AArch64LlvmImportConstantKind::UnsignedIntToFloat: {
            const auto *int_operand = dynamic_cast<const CoreIrConstantInt *>(operand);
            const auto *float_type = dynamic_cast<const CoreIrFloatType *>(type);
            if (int_operand == nullptr || float_type == nullptr) {
                return nullptr;
            }
            const std::uint64_t source_value = truncate_integer_to_width(
                int_operand->get_value(), constant.cast_source_type.integer_bit_width);
            const long double numeric_value =
                constant.kind == AArch64LlvmImportConstantKind::SignedIntToFloat
                    ? static_cast<long double>(static_cast<std::int64_t>(
                          sign_extend_integer_to_u64(
                              source_value,
                              constant.cast_source_type.integer_bit_width)))
                    : static_cast<long double>(source_value);
            const auto literal = format_float_literal_for_kind(
                float_type->get_float_kind(), numeric_value);
            return literal.has_value()
                       ? context_->create_constant<CoreIrConstantFloat>(type, *literal)
                       : nullptr;
        }
        case AArch64LlvmImportConstantKind::FloatToSignedInt:
        case AArch64LlvmImportConstantKind::FloatToUnsignedInt: {
            const auto *float_operand = dynamic_cast<const CoreIrConstantFloat *>(operand);
            const auto target_width = integer_type_bit_width(type);
            const auto parsed = float_operand == nullptr
                                    ? std::optional<long double>{}
                                    : parse_core_float_literal(*float_operand);
            if (!target_width.has_value() || !parsed.has_value()) {
                return nullptr;
            }
            std::uint64_t folded = 0;
            if (constant.kind == AArch64LlvmImportConstantKind::FloatToSignedInt) {
                folded = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(*parsed));
            } else {
                if (*parsed < 0.0L) {
                    return nullptr;
                }
                folded = static_cast<std::uint64_t>(*parsed);
            }
            folded = truncate_integer_to_width(folded, *target_width);
            return context_->create_constant<CoreIrConstantInt>(type, folded);
        }
        case AArch64LlvmImportConstantKind::FloatExtend:
        case AArch64LlvmImportConstantKind::FloatTruncate: {
            const auto *float_operand = dynamic_cast<const CoreIrConstantFloat *>(operand);
            const auto *float_type = dynamic_cast<const CoreIrFloatType *>(type);
            const auto parsed = float_operand == nullptr
                                    ? std::optional<long double>{}
                                    : parse_core_float_literal(*float_operand);
            if (float_type == nullptr || !parsed.has_value()) {
                return nullptr;
            }
            const auto literal = format_float_literal_for_kind(
                float_type->get_float_kind(), *parsed);
            return literal.has_value()
                       ? context_->create_constant<CoreIrConstantFloat>(type, *literal)
                       : nullptr;
        }
        case AArch64LlvmImportConstantKind::Bitcast: {
            if (type->get_kind() == CoreIrTypeKind::Pointer) {
                return operand;
            }
            if (const auto *int_operand =
                    dynamic_cast<const CoreIrConstantInt *>(operand);
                int_operand != nullptr) {
                const auto *float_type = dynamic_cast<const CoreIrFloatType *>(type);
                const auto source_bits =
                    import_type_storage_bit_width(constant.cast_source_type);
                if (float_type == nullptr || !source_bits.has_value() ||
                    *source_bits > 64) {
                    return nullptr;
                }
                const std::string bit_pattern = format_hex_bits(
                    truncate_integer_to_width(int_operand->get_value(), *source_bits));
                const auto literal = canonicalize_float_literal_text(
                    float_type->get_float_kind(), bit_pattern);
                return literal.has_value()
                           ? context_->create_constant<CoreIrConstantFloat>(type, *literal)
                           : nullptr;
            }
            if (const auto *float_operand =
                    dynamic_cast<const CoreIrConstantFloat *>(operand);
                float_operand != nullptr) {
                const auto target_width = integer_type_bit_width(type);
                const auto source_kind =
                    import_type_to_core_float_kind(constant.cast_source_type);
                if (!target_width.has_value() || !source_kind.has_value()) {
                    return nullptr;
                }
                const auto encoded_bits =
                    encode_float_literal_bits(*float_operand, *source_kind);
                if (!encoded_bits.has_value()) {
                    return nullptr;
                }
                return context_->create_constant<CoreIrConstantInt>(
                    type, truncate_integer_to_width(*encoded_bits, *target_width));
            }
            return nullptr;
        }
        case AArch64LlvmImportConstantKind::AddrSpaceCast:
            return type->get_kind() == CoreIrTypeKind::Pointer ? operand : nullptr;
        default:
            return nullptr;
        }
    }

    const CoreIrConstant *lower_typed_import_constant(
        const std::shared_ptr<AArch64LlvmImportTypedConstant> &typed_constant) {
        if (typed_constant == nullptr || !typed_constant->is_valid()) {
            return nullptr;
        }
        const CoreIrType *type = lower_import_type(typed_constant->type);
        if (type == nullptr) {
            return nullptr;
        }
        return lower_import_constant(type, typed_constant->constant);
    }

    bool expand_vector_constant_lanes(const CoreIrType *vector_type,
                                      const CoreIrConstant *constant,
                                      std::vector<const CoreIrConstant *> &lanes) {
        const auto *array_type = dynamic_cast<const CoreIrArrayType *>(vector_type);
        if (array_type == nullptr || constant == nullptr) {
            return false;
        }
        lanes.clear();
        lanes.reserve(array_type->get_element_count());
        if (const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            aggregate != nullptr) {
            for (const CoreIrConstant *element : aggregate->get_elements()) {
                lanes.push_back(element);
            }
            while (lanes.size() < array_type->get_element_count()) {
                lanes.push_back(make_zero_constant(array_type->get_element_type()));
            }
            return lanes.size() == array_type->get_element_count();
        }
        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
            for (std::size_t index = 0; index < array_type->get_element_count(); ++index) {
                lanes.push_back(make_zero_constant(array_type->get_element_type()));
            }
            return true;
        }
        return false;
    }

    std::optional<bool> lower_scalar_bool_constant(
        const std::shared_ptr<AArch64LlvmImportTypedConstant> &typed_condition) {
        const auto *condition_constant =
            dynamic_cast<const CoreIrConstantInt *>(
                lower_typed_import_constant(typed_condition));
        if (condition_constant == nullptr) {
            return std::nullopt;
        }
        return condition_constant->get_value() != 0;
    }

    bool constants_equivalent(const CoreIrConstant *lhs,
                              const CoreIrConstant *rhs) const {
        if (lhs == rhs) {
            return true;
        }
        if (lhs == nullptr || rhs == nullptr) {
            return false;
        }
        if (const auto *lhs_int = dynamic_cast<const CoreIrConstantInt *>(lhs);
            lhs_int != nullptr) {
            const auto *rhs_int = dynamic_cast<const CoreIrConstantInt *>(rhs);
            return rhs_int != nullptr &&
                   lhs_int->get_value() == rhs_int->get_value();
        }
        if (const auto *lhs_float = dynamic_cast<const CoreIrConstantFloat *>(lhs);
            lhs_float != nullptr) {
            const auto *rhs_float = dynamic_cast<const CoreIrConstantFloat *>(rhs);
            return rhs_float != nullptr &&
                   lhs_float->get_literal_text() == rhs_float->get_literal_text();
        }
        if (dynamic_cast<const CoreIrConstantNull *>(lhs) != nullptr ||
            dynamic_cast<const CoreIrConstantZeroInitializer *>(lhs) != nullptr) {
            return dynamic_cast<const CoreIrConstantNull *>(rhs) != nullptr ||
                   dynamic_cast<const CoreIrConstantZeroInitializer *>(rhs) != nullptr;
        }
        if (const auto *lhs_global =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(lhs);
            lhs_global != nullptr) {
            const auto *rhs_global =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(rhs);
            return rhs_global != nullptr &&
                   lhs_global->get_global() == rhs_global->get_global() &&
                   lhs_global->get_function() == rhs_global->get_function();
        }
        if (const auto *lhs_gep =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(lhs);
            lhs_gep != nullptr) {
            const auto *rhs_gep =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(rhs);
            if (rhs_gep == nullptr ||
                !constants_equivalent(lhs_gep->get_base(), rhs_gep->get_base()) ||
                lhs_gep->get_indices().size() != rhs_gep->get_indices().size()) {
                return false;
            }
            for (std::size_t index = 0; index < lhs_gep->get_indices().size(); ++index) {
                if (!constants_equivalent(lhs_gep->get_indices()[index],
                                          rhs_gep->get_indices()[index])) {
                    return false;
                }
            }
            return true;
        }
        if (const auto *lhs_cast = dynamic_cast<const CoreIrConstantCast *>(lhs);
            lhs_cast != nullptr) {
            const auto *rhs_cast = dynamic_cast<const CoreIrConstantCast *>(rhs);
            return rhs_cast != nullptr &&
                   lhs_cast->get_cast_kind() == rhs_cast->get_cast_kind() &&
                   constants_equivalent(lhs_cast->get_operand(),
                                        rhs_cast->get_operand());
        }
        if (const auto *lhs_agg =
                dynamic_cast<const CoreIrConstantAggregate *>(lhs);
            lhs_agg != nullptr) {
            const auto *rhs_agg =
                dynamic_cast<const CoreIrConstantAggregate *>(rhs);
            if (rhs_agg == nullptr ||
                lhs_agg->get_elements().size() != rhs_agg->get_elements().size()) {
                return false;
            }
            for (std::size_t index = 0; index < lhs_agg->get_elements().size(); ++index) {
                if (!constants_equivalent(lhs_agg->get_elements()[index],
                                          rhs_agg->get_elements()[index])) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    bool evaluate_integer_compare(CoreIrComparePredicate predicate,
                                  std::uint64_t lhs, std::uint64_t rhs,
                                  std::size_t bit_width) const {
        lhs = truncate_integer_to_width(lhs, bit_width);
        rhs = truncate_integer_to_width(rhs, bit_width);
        const std::int64_t signed_lhs =
            static_cast<std::int64_t>(sign_extend_integer_to_u64(lhs, bit_width));
        const std::int64_t signed_rhs =
            static_cast<std::int64_t>(sign_extend_integer_to_u64(rhs, bit_width));
        switch (predicate) {
        case CoreIrComparePredicate::Equal:
            return lhs == rhs;
        case CoreIrComparePredicate::NotEqual:
            return lhs != rhs;
        case CoreIrComparePredicate::SignedLess:
            return signed_lhs < signed_rhs;
        case CoreIrComparePredicate::SignedLessEqual:
            return signed_lhs <= signed_rhs;
        case CoreIrComparePredicate::SignedGreater:
            return signed_lhs > signed_rhs;
        case CoreIrComparePredicate::SignedGreaterEqual:
            return signed_lhs >= signed_rhs;
        case CoreIrComparePredicate::UnsignedLess:
            return lhs < rhs;
        case CoreIrComparePredicate::UnsignedLessEqual:
            return lhs <= rhs;
        case CoreIrComparePredicate::UnsignedGreater:
            return lhs > rhs;
        case CoreIrComparePredicate::UnsignedGreaterEqual:
            return lhs >= rhs;
        }
        return false;
    }

    std::optional<bool> evaluate_float_compare(
        const std::string &predicate_text, long double lhs,
        long double rhs) const {
        const bool unordered = std::isnan(lhs) || std::isnan(rhs);
        if (predicate_text == "false") {
            return false;
        }
        if (predicate_text == "true") {
            return true;
        }
        if (predicate_text == "ord") {
            return !unordered;
        }
        if (predicate_text == "uno") {
            return unordered;
        }
        if (predicate_text == "oeq") {
            return !unordered && lhs == rhs;
        }
        if (predicate_text == "one") {
            return !unordered && lhs != rhs;
        }
        if (predicate_text == "olt") {
            return !unordered && lhs < rhs;
        }
        if (predicate_text == "ole") {
            return !unordered && lhs <= rhs;
        }
        if (predicate_text == "ogt") {
            return !unordered && lhs > rhs;
        }
        if (predicate_text == "oge") {
            return !unordered && lhs >= rhs;
        }
        if (predicate_text == "ueq") {
            return unordered || lhs == rhs;
        }
        if (predicate_text == "une") {
            return unordered || lhs != rhs;
        }
        if (predicate_text == "ult") {
            return unordered || lhs < rhs;
        }
        if (predicate_text == "ule") {
            return unordered || lhs <= rhs;
        }
        if (predicate_text == "ugt") {
            return unordered || lhs > rhs;
        }
        if (predicate_text == "uge") {
            return unordered || lhs >= rhs;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> lower_vector_index_operand(
        const std::shared_ptr<AArch64LlvmImportTypedConstant> &typed_index) {
        const auto *index_constant =
            dynamic_cast<const CoreIrConstantInt *>(lower_typed_import_constant(
                typed_index));
        if (index_constant == nullptr) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(index_constant->get_value());
    }

    const CoreIrConstant *fold_vector_constant_expr(
        const CoreIrType *type, const AArch64LlvmImportConstant &constant) {
        switch (constant.kind) {
        case AArch64LlvmImportConstantKind::Compare: {
            if (constant.compare_lhs_operand == nullptr ||
                constant.compare_rhs_operand == nullptr) {
                return nullptr;
            }
            const auto fold_scalar_lane =
                [&](const AArch64LlvmImportType &operand_type,
                    const CoreIrConstant *lhs_constant,
                    const CoreIrConstant *rhs_constant) -> std::optional<bool> {
                if (lhs_constant == nullptr || rhs_constant == nullptr) {
                    return std::nullopt;
                }
                if (constant.compare_is_float) {
                    const auto *lhs_float =
                        dynamic_cast<const CoreIrConstantFloat *>(lhs_constant);
                    const auto *rhs_float =
                        dynamic_cast<const CoreIrConstantFloat *>(rhs_constant);
                    const auto lhs_value =
                        lhs_float == nullptr ? std::optional<long double>{}
                                             : parse_core_float_literal(*lhs_float);
                    const auto rhs_value =
                        rhs_float == nullptr ? std::optional<long double>{}
                                             : parse_core_float_literal(*rhs_float);
                    if (!lhs_value.has_value() || !rhs_value.has_value()) {
                        return std::nullopt;
                    }
                    return evaluate_float_compare(constant.compare_predicate_text,
                                                  *lhs_value, *rhs_value);
                }

                const auto predicate = parse_compare_predicate(
                    constant.compare_predicate_text, false);
                if (!predicate.has_value()) {
                    return std::nullopt;
                }
                if (const auto *lhs_int =
                        dynamic_cast<const CoreIrConstantInt *>(lhs_constant);
                    lhs_int != nullptr) {
                    const auto *rhs_int =
                        dynamic_cast<const CoreIrConstantInt *>(rhs_constant);
                    if (rhs_int == nullptr) {
                        return std::nullopt;
                    }
                    const std::size_t bit_width =
                        operand_type.kind == AArch64LlvmImportTypeKind::Integer
                            ? operand_type.integer_bit_width
                            : 64;
                    return evaluate_integer_compare(*predicate, lhs_int->get_value(),
                                                    rhs_int->get_value(),
                                                    bit_width);
                }
                if (operand_type.kind == AArch64LlvmImportTypeKind::Pointer &&
                    (*predicate == CoreIrComparePredicate::Equal ||
                     *predicate == CoreIrComparePredicate::NotEqual)) {
                    const bool equivalent =
                        constants_equivalent(lhs_constant, rhs_constant);
                    return *predicate == CoreIrComparePredicate::Equal
                               ? equivalent
                               : !equivalent;
                }
                return std::nullopt;
            };

            const CoreIrConstant *lhs_constant =
                lower_typed_import_constant(constant.compare_lhs_operand);
            const CoreIrConstant *rhs_constant =
                lower_typed_import_constant(constant.compare_rhs_operand);
            if (lhs_constant == nullptr || rhs_constant == nullptr) {
                return nullptr;
            }
            if (type->get_kind() == CoreIrTypeKind::Integer) {
                const auto folded = fold_scalar_lane(
                    constant.compare_lhs_operand->type, lhs_constant, rhs_constant);
                return folded.has_value()
                           ? context_->create_constant<CoreIrConstantInt>(
                                 type, *folded ? 1 : 0)
                           : nullptr;
            }
            const CoreIrType *operand_type =
                lower_import_type(constant.compare_lhs_operand->type);
            std::vector<const CoreIrConstant *> lhs_lanes;
            std::vector<const CoreIrConstant *> rhs_lanes;
            if (operand_type == nullptr ||
                !expand_vector_constant_lanes(operand_type, lhs_constant, lhs_lanes) ||
                !expand_vector_constant_lanes(operand_type, rhs_constant, rhs_lanes) ||
                lhs_lanes.size() != rhs_lanes.size()) {
                return nullptr;
            }
            std::vector<const CoreIrConstant *> result_lanes;
            result_lanes.reserve(lhs_lanes.size());
            const CoreIrType *i1_type = parse_type_text("i1");
            for (std::size_t index = 0; index < lhs_lanes.size(); ++index) {
                const auto lane = fold_scalar_lane(
                    constant.compare_lhs_operand->type.element_types.front(),
                    lhs_lanes[index], rhs_lanes[index]);
                if (!lane.has_value() || i1_type == nullptr) {
                    return nullptr;
                }
                result_lanes.push_back(
                    context_->create_constant<CoreIrConstantInt>(i1_type,
                                                                 *lane ? 1 : 0));
            }
            return context_->create_constant<CoreIrConstantAggregate>(type,
                                                                      result_lanes);
        }
        case AArch64LlvmImportConstantKind::Select: {
            if (constant.select_condition_operand == nullptr ||
                constant.select_true_operand == nullptr ||
                constant.select_false_operand == nullptr) {
                return nullptr;
            }
            const auto scalar_condition =
                lower_scalar_bool_constant(constant.select_condition_operand);
            if (scalar_condition.has_value()) {
                return *scalar_condition
                           ? lower_typed_import_constant(constant.select_true_operand)
                           : lower_typed_import_constant(constant.select_false_operand);
            }

            const CoreIrType *condition_type =
                lower_import_type(constant.select_condition_operand->type);
            const CoreIrConstant *condition_constant =
                lower_typed_import_constant(constant.select_condition_operand);
            const CoreIrConstant *true_constant =
                lower_typed_import_constant(constant.select_true_operand);
            const CoreIrConstant *false_constant =
                lower_typed_import_constant(constant.select_false_operand);
            std::vector<const CoreIrConstant *> condition_lanes;
            std::vector<const CoreIrConstant *> true_lanes;
            std::vector<const CoreIrConstant *> false_lanes;
            if (condition_type == nullptr || condition_constant == nullptr ||
                true_constant == nullptr || false_constant == nullptr ||
                !expand_vector_constant_lanes(condition_type, condition_constant,
                                              condition_lanes) ||
                !expand_vector_constant_lanes(type, true_constant, true_lanes) ||
                !expand_vector_constant_lanes(type, false_constant, false_lanes) ||
                condition_lanes.size() != true_lanes.size() ||
                condition_lanes.size() != false_lanes.size()) {
                return nullptr;
            }

            std::vector<const CoreIrConstant *> lanes;
            lanes.reserve(true_lanes.size());
            for (std::size_t index = 0; index < condition_lanes.size(); ++index) {
                const auto *lane_condition =
                    dynamic_cast<const CoreIrConstantInt *>(condition_lanes[index]);
                if (lane_condition == nullptr) {
                    return nullptr;
                }
                lanes.push_back(lane_condition->get_value() != 0 ? true_lanes[index]
                                                                 : false_lanes[index]);
            }
            return context_->create_constant<CoreIrConstantAggregate>(type, lanes);
        }
        case AArch64LlvmImportConstantKind::ExtractElement: {
            if (constant.extract_vector_operand == nullptr ||
                constant.extract_index_operand == nullptr) {
                return nullptr;
            }
            const CoreIrType *vector_type =
                lower_import_type(constant.extract_vector_operand->type);
            const CoreIrConstant *vector_constant =
                lower_typed_import_constant(constant.extract_vector_operand);
            const auto index = lower_vector_index_operand(
                constant.extract_index_operand);
            std::vector<const CoreIrConstant *> lanes;
            if (vector_type == nullptr || !index.has_value() ||
                !expand_vector_constant_lanes(vector_type, vector_constant, lanes) ||
                *index >= lanes.size()) {
                return nullptr;
            }
            return lanes[*index];
        }
        case AArch64LlvmImportConstantKind::InsertElement: {
            if (constant.insert_vector_operand == nullptr ||
                constant.insert_element_operand == nullptr ||
                constant.insert_index_operand == nullptr) {
                return nullptr;
            }
            const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
            const CoreIrType *vector_type =
                lower_import_type(constant.insert_vector_operand->type);
            const CoreIrConstant *vector_constant =
                lower_typed_import_constant(constant.insert_vector_operand);
            const CoreIrConstant *element_constant =
                lower_typed_import_constant(constant.insert_element_operand);
            const auto index = lower_vector_index_operand(
                constant.insert_index_operand);
            std::vector<const CoreIrConstant *> lanes;
            if (array_type == nullptr || vector_type == nullptr ||
                element_constant == nullptr || !index.has_value() ||
                !expand_vector_constant_lanes(vector_type, vector_constant, lanes) ||
                *index >= lanes.size()) {
                return nullptr;
            }
            lanes[*index] = element_constant;
            return context_->create_constant<CoreIrConstantAggregate>(type, lanes);
        }
        case AArch64LlvmImportConstantKind::ShuffleVector: {
            if (constant.shuffle_lhs_operand == nullptr ||
                constant.shuffle_rhs_operand == nullptr ||
                constant.shuffle_mask_operand == nullptr) {
                return nullptr;
            }
            const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
            const CoreIrType *lhs_type =
                lower_import_type(constant.shuffle_lhs_operand->type);
            const CoreIrType *rhs_type =
                lower_import_type(constant.shuffle_rhs_operand->type);
            const CoreIrType *mask_type =
                lower_import_type(constant.shuffle_mask_operand->type);
            const CoreIrConstant *lhs_constant =
                lower_typed_import_constant(constant.shuffle_lhs_operand);
            const CoreIrConstant *rhs_constant =
                lower_typed_import_constant(constant.shuffle_rhs_operand);
            const CoreIrConstant *mask_constant =
                lower_typed_import_constant(constant.shuffle_mask_operand);
            std::vector<const CoreIrConstant *> lhs_lanes;
            std::vector<const CoreIrConstant *> rhs_lanes;
            std::vector<const CoreIrConstant *> mask_lanes;
            if (array_type == nullptr || lhs_type == nullptr || rhs_type == nullptr ||
                mask_type == nullptr ||
                !expand_vector_constant_lanes(lhs_type, lhs_constant, lhs_lanes) ||
                !expand_vector_constant_lanes(rhs_type, rhs_constant, rhs_lanes) ||
                !expand_vector_constant_lanes(mask_type, mask_constant, mask_lanes) ||
                mask_lanes.size() != array_type->get_element_count()) {
                return nullptr;
            }
            std::vector<const CoreIrConstant *> combined;
            combined.reserve(lhs_lanes.size() + rhs_lanes.size());
            combined.insert(combined.end(), lhs_lanes.begin(), lhs_lanes.end());
            combined.insert(combined.end(), rhs_lanes.begin(), rhs_lanes.end());

            std::vector<const CoreIrConstant *> lanes;
            lanes.reserve(mask_lanes.size());
            for (const CoreIrConstant *mask_lane : mask_lanes) {
                const auto *mask_index =
                    dynamic_cast<const CoreIrConstantInt *>(mask_lane);
                if (mask_index == nullptr ||
                    mask_index->get_value() >= combined.size()) {
                    return nullptr;
                }
                lanes.push_back(combined[static_cast<std::size_t>(
                    mask_index->get_value())]);
            }
            return context_->create_constant<CoreIrConstantAggregate>(type, lanes);
        }
        default:
            return nullptr;
        }
    }

    const CoreIrConstant *lower_import_constant(
        const CoreIrType *type, const AArch64LlvmImportConstant &constant) {
        if (type == nullptr || !constant.is_valid()) {
            return nullptr;
        }

        switch (constant.kind) {
        case AArch64LlvmImportConstantKind::ZeroInitializer:
            return context_->create_constant<CoreIrConstantZeroInitializer>(type);
        case AArch64LlvmImportConstantKind::UndefValue:
        case AArch64LlvmImportConstantKind::PoisonValue:
            return make_zero_constant(type);
        case AArch64LlvmImportConstantKind::BlockAddress:
            return context_->create_constant<CoreIrConstantBlockAddress>(
                type, constant.blockaddress_function_name,
                constant.blockaddress_label_name);
        case AArch64LlvmImportConstantKind::Integer:
            return context_->create_constant<CoreIrConstantInt>(
                type, constant.integer_value);
        case AArch64LlvmImportConstantKind::Float:
            return context_->create_constant<CoreIrConstantFloat>(type,
                                                                  constant.float_text);
        case AArch64LlvmImportConstantKind::NullPointer:
            return context_->create_constant<CoreIrConstantNull>(type);
        case AArch64LlvmImportConstantKind::SymbolReference:
            if (auto global_it = globals_.find(constant.symbol_name);
                global_it != globals_.end()) {
                return context_->create_constant<CoreIrConstantGlobalAddress>(
                    type->get_kind() == CoreIrTypeKind::Pointer
                        ? type
                        : pointer_to(global_it->second->get_type()),
                    global_it->second);
            }
            if (auto function_it = functions_.find(constant.symbol_name);
                function_it != functions_.end()) {
                return context_->create_constant<CoreIrConstantGlobalAddress>(
                    type->get_kind() == CoreIrTypeKind::Pointer
                        ? type
                        : pointer_to(function_it->second->get_function_type()),
                    function_it->second);
            }
            return nullptr;
        case AArch64LlvmImportConstantKind::SignExtend:
        case AArch64LlvmImportConstantKind::ZeroExtend:
        case AArch64LlvmImportConstantKind::Truncate:
        case AArch64LlvmImportConstantKind::SignedIntToFloat:
        case AArch64LlvmImportConstantKind::UnsignedIntToFloat:
        case AArch64LlvmImportConstantKind::FloatToSignedInt:
        case AArch64LlvmImportConstantKind::FloatToUnsignedInt:
        case AArch64LlvmImportConstantKind::FloatExtend:
        case AArch64LlvmImportConstantKind::FloatTruncate:
        case AArch64LlvmImportConstantKind::Bitcast:
        case AArch64LlvmImportConstantKind::AddrSpaceCast:
        case AArch64LlvmImportConstantKind::IntToPtr:
        case AArch64LlvmImportConstantKind::PtrToInt: {
            const CoreIrType *source_type =
                lower_import_type(constant.cast_source_type);
            if (source_type == nullptr || constant.cast_operand == nullptr) {
                return nullptr;
            }
            const CoreIrConstant *operand =
                lower_import_constant(source_type, *constant.cast_operand);
            if (operand == nullptr) {
                return nullptr;
            }
            if (constant.kind != AArch64LlvmImportConstantKind::IntToPtr &&
                constant.kind != AArch64LlvmImportConstantKind::PtrToInt) {
                return fold_scalar_constant_cast(type, constant, operand);
            }
            return context_->create_constant<CoreIrConstantCast>(
                type,
                constant.kind == AArch64LlvmImportConstantKind::IntToPtr
                    ? CoreIrCastKind::IntToPtr
                    : CoreIrCastKind::PtrToInt,
                operand);
        }
        case AArch64LlvmImportConstantKind::GetElementPtr: {
            const CoreIrType *source_type =
                lower_import_type(constant.gep_source_type);
            if (source_type == nullptr || constant.gep_base == nullptr) {
                return nullptr;
            }

            const CoreIrConstant *base = lower_import_constant(
                pointer_to(source_type), *constant.gep_base);
            if (base == nullptr) {
                return nullptr;
            }

            if (constant.gep_index_types.size() != constant.gep_indices.size()) {
                return nullptr;
            }
            std::vector<const CoreIrConstant *> indices;
            indices.reserve(constant.gep_indices.size());
            for (std::size_t index = 0; index < constant.gep_indices.size();
                 ++index) {
                const CoreIrType *index_type =
                    lower_import_type(constant.gep_index_types[index]);
                if (index_type == nullptr) {
                    return nullptr;
                }
                const CoreIrConstant *lowered = lower_import_constant(
                    index_type, constant.gep_indices[index]);
                if (lowered == nullptr) {
                    return nullptr;
                }
                indices.push_back(lowered);
            }
            return context_->create_constant<CoreIrConstantGetElementPtr>(
                type, base, indices);
        }
        case AArch64LlvmImportConstantKind::Compare:
        case AArch64LlvmImportConstantKind::Select:
        case AArch64LlvmImportConstantKind::ExtractElement:
        case AArch64LlvmImportConstantKind::InsertElement:
        case AArch64LlvmImportConstantKind::ShuffleVector:
            return fold_vector_constant_expr(type, constant);
        case AArch64LlvmImportConstantKind::Aggregate: {
            std::vector<const CoreIrConstant *> elements;
            if (type->get_kind() == CoreIrTypeKind::Array) {
                const auto *array_type = static_cast<const CoreIrArrayType *>(type);
                if (constant.elements.size() > array_type->get_element_count()) {
                    return nullptr;
                }
                for (const AArch64LlvmImportConstant &element : constant.elements) {
                    const CoreIrConstant *lowered = lower_import_constant(
                        array_type->get_element_type(), element);
                    if (lowered == nullptr) {
                        return nullptr;
                    }
                    elements.push_back(lowered);
                }
                return context_->create_constant<CoreIrConstantAggregate>(
                    type, elements);
            }
            if (type->get_kind() == CoreIrTypeKind::Struct) {
                const auto *struct_type =
                    static_cast<const CoreIrStructType *>(type);
                if (constant.elements.size() !=
                    struct_type->get_element_types().size()) {
                    return nullptr;
                }
                for (std::size_t index = 0; index < constant.elements.size();
                     ++index) {
                    const CoreIrConstant *lowered = lower_import_constant(
                        struct_type->get_element_types()[index],
                        constant.elements[index]);
                    if (lowered == nullptr) {
                        return nullptr;
                    }
                    elements.push_back(lowered);
                }
                return context_->create_constant<CoreIrConstantAggregate>(
                    type, elements);
            }
            return nullptr;
        }
        case AArch64LlvmImportConstantKind::Invalid:
        default:
            return nullptr;
        }
    }

    bool lower_named_type_definition(const AArch64LlvmImportNamedType &named_type) {
        if (named_type.is_opaque || named_type.body_text == "opaque") {
            named_type_cache_[named_type.name] =
                context_->create_type<CoreIrStructType>();
            return true;
        }

        const CoreIrType *type = lower_import_type(named_type.body_type);
        if (type == nullptr) {
            add_error("unsupported LLVM named type body: " + named_type.body_text,
                      named_type.line, 1);
            return false;
        }
        named_type_cache_[named_type.name] = type;
        return true;
    }

    bool lower_global_definition(const AArch64LlvmImportGlobal &global) {
        const CoreIrType *type = lower_import_type(global.type);
        if (type == nullptr) {
            add_error("unsupported LLVM global type: " + global.type_text,
                      global.line, 1);
            return false;
        }

        if (!global.initializer.is_valid()) {
            add_error("unsupported LLVM global initializer: " +
                          global.initializer_text,
                      global.line, 1);
            return false;
        }
        const CoreIrConstant *initializer =
            lower_import_constant(type, global.initializer);
        if (initializer == nullptr) {
            add_error("unsupported LLVM global initializer: " +
                          global.initializer_text,
                      global.line, 1);
            return false;
        }

        CoreIrGlobal *core_global = module_->find_global(global.name);
        if (core_global == nullptr) {
            core_global = module_->create_global<CoreIrGlobal>(
                global.name, type, initializer, global.is_internal_linkage,
                global.is_constant);
        } else {
            core_global->set_initializer(initializer);
            core_global->set_is_constant(global.is_constant);
        }
        globals_[global.name] = core_global;
        return true;
    }

    bool lower_alias_definition(const AArch64LlvmImportAlias &alias) {
        const CoreIrType *target_type = lower_import_type(alias.target_type);
        if (target_type == nullptr || !alias.target.is_valid()) {
            return false;
        }
        const CoreIrConstant *target =
            lower_import_constant(target_type, alias.target);
        const auto *global_address =
            unwrap_alias_target_global_address(target);
        if (global_address == nullptr) {
            return false;
        }
        if (global_address->get_global() != nullptr) {
            globals_[alias.name] = global_address->get_global();
            return true;
        }
        if (global_address->get_function() != nullptr) {
            functions_[alias.name] = global_address->get_function();
            return true;
        }
        return false;
    }

    bool lower_function_signature(const AArch64LlvmImportFunction &function,
                                  PendingFunctionDefinition *pending) {
        const CoreIrType *return_type = lower_import_type(function.return_type);
        if (return_type == nullptr) {
            add_error("unsupported LLVM function return type: " +
                          function.return_type_text,
                      function.line, 1);
            return false;
        }

        std::vector<ParameterSpec> parameters;
        std::vector<const CoreIrType *> parameter_types;
        for (const AArch64LlvmImportParameter &parameter : function.parameters) {
            const CoreIrType *parameter_type = lower_import_type(parameter.type);
            if (parameter_type == nullptr) {
                add_error("unsupported LLVM function parameter type: " +
                              parameter.type_text,
                          function.line, 1);
                return false;
            }
            ParameterSpec lowered_parameter;
            lowered_parameter.type = parameter_type;
            lowered_parameter.name = parameter.name;
            parameters.push_back(std::move(lowered_parameter));
            parameter_types.push_back(parameter_type);
        }

        const CoreIrFunctionType *function_type =
            context_->create_type<CoreIrFunctionType>(return_type, parameter_types,
                                                      function.is_variadic);
        CoreIrFunction *core_function = module_->find_function(function.name);
        if (core_function == nullptr) {
            core_function = module_->create_function<CoreIrFunction>(
                function.name, function_type, function.is_internal_linkage,
                false);
        }
        functions_[function.name] = core_function;

        if (pending != nullptr) {
            pending->function = core_function;
            pending->parameters = std::move(parameters);
        }
        return true;
    }

    std::string describe_import_constant(
        const AArch64LlvmImportConstant &constant) const {
        switch (constant.kind) {
        case AArch64LlvmImportConstantKind::Integer:
            return std::to_string(constant.integer_value);
        case AArch64LlvmImportConstantKind::Float:
            return constant.float_text;
        case AArch64LlvmImportConstantKind::NullPointer:
            return "null";
        case AArch64LlvmImportConstantKind::ZeroInitializer:
            return "zeroinitializer";
        case AArch64LlvmImportConstantKind::UndefValue:
            return "undef";
        case AArch64LlvmImportConstantKind::PoisonValue:
            return "poison";
        case AArch64LlvmImportConstantKind::BlockAddress:
            return "blockaddress(@" + constant.blockaddress_function_name + ", %" +
                   constant.blockaddress_label_name + ")";
        case AArch64LlvmImportConstantKind::SymbolReference:
            return "@" + constant.symbol_name;
        case AArch64LlvmImportConstantKind::SignExtend:
            if (constant.cast_operand == nullptr) {
                return "sext(<null-operand>)";
            }
            return "sext (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::ZeroExtend:
            if (constant.cast_operand == nullptr) {
                return "zext(<null-operand>)";
            }
            return "zext (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::Truncate:
            if (constant.cast_operand == nullptr) {
                return "trunc(<null-operand>)";
            }
            return "trunc (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::SignedIntToFloat:
            if (constant.cast_operand == nullptr) {
                return "sitofp(<null-operand>)";
            }
            return "sitofp (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::UnsignedIntToFloat:
            if (constant.cast_operand == nullptr) {
                return "uitofp(<null-operand>)";
            }
            return "uitofp (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::FloatToSignedInt:
            if (constant.cast_operand == nullptr) {
                return "fptosi(<null-operand>)";
            }
            return "fptosi (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::FloatToUnsignedInt:
            if (constant.cast_operand == nullptr) {
                return "fptoui(<null-operand>)";
            }
            return "fptoui (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::FloatExtend:
            if (constant.cast_operand == nullptr) {
                return "fpext(<null-operand>)";
            }
            return "fpext (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::FloatTruncate:
            if (constant.cast_operand == nullptr) {
                return "fptrunc(<null-operand>)";
            }
            return "fptrunc (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::Bitcast:
            if (constant.cast_operand == nullptr) {
                return "bitcast(<null-operand>)";
            }
            return "bitcast (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::AddrSpaceCast:
            if (constant.cast_operand == nullptr) {
                return "addrspacecast(<null-operand>)";
            }
            return "addrspacecast (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::IntToPtr:
            if (constant.cast_operand == nullptr) {
                return "inttoptr(<null-operand>)";
            }
            return "inttoptr (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::PtrToInt:
            if (constant.cast_operand == nullptr) {
                return "ptrtoint(<null-operand>)";
            }
            return "ptrtoint (" + constant.cast_source_type_text + " " +
                   describe_import_constant(*constant.cast_operand) + " to " +
                   constant.cast_target_type_text + ")";
        case AArch64LlvmImportConstantKind::GetElementPtr: {
            if (constant.gep_base == nullptr) {
                return "getelementptr(<null-base>)";
            }
            std::string text = "getelementptr ";
            if (constant.gep_is_inbounds) {
                text += "inbounds ";
            }
            text += "(" + constant.gep_source_type_text + ", ptr " +
                    describe_import_constant(*constant.gep_base);
            for (std::size_t index = 0; index < constant.gep_indices.size(); ++index) {
                text += ", " + constant.gep_index_type_texts[index] + " " +
                        describe_import_constant(constant.gep_indices[index]);
            }
            text += ")";
            return text;
        }
        case AArch64LlvmImportConstantKind::Compare:
            if (constant.compare_lhs_operand == nullptr ||
                constant.compare_rhs_operand == nullptr) {
                return (constant.compare_is_float ? "fcmp" : "icmp") +
                       std::string("(<missing-operands>)");
            }
            return std::string(constant.compare_is_float ? "fcmp " : "icmp ") +
                   constant.compare_predicate_text + " (" +
                   constant.compare_lhs_operand->type_text + " " +
                   describe_import_constant(constant.compare_lhs_operand->constant) +
                   ", " + constant.compare_rhs_operand->type_text + " " +
                   describe_import_constant(constant.compare_rhs_operand->constant) +
                   ")";
        case AArch64LlvmImportConstantKind::Select:
            if (constant.select_condition_operand == nullptr ||
                constant.select_true_operand == nullptr ||
                constant.select_false_operand == nullptr) {
                return "select(<missing-operands>)";
            }
            return "select (" + constant.select_condition_operand->type_text + " " +
                   describe_import_constant(constant.select_condition_operand->constant) +
                   ", " + constant.select_true_operand->type_text + " " +
                   describe_import_constant(constant.select_true_operand->constant) +
                   ", " + constant.select_false_operand->type_text + " " +
                   describe_import_constant(constant.select_false_operand->constant) +
                   ")";
        case AArch64LlvmImportConstantKind::ExtractElement:
            if (constant.extract_vector_operand == nullptr ||
                constant.extract_index_operand == nullptr) {
                return "extractelement(<missing-operands>)";
            }
            return "extractelement (" + constant.extract_vector_operand->type_text +
                   " " +
                   describe_import_constant(constant.extract_vector_operand->constant) +
                   ", " + constant.extract_index_operand->type_text + " " +
                   describe_import_constant(constant.extract_index_operand->constant) +
                   ")";
        case AArch64LlvmImportConstantKind::InsertElement:
            if (constant.insert_vector_operand == nullptr ||
                constant.insert_element_operand == nullptr ||
                constant.insert_index_operand == nullptr) {
                return "insertelement(<missing-operands>)";
            }
            return "insertelement (" + constant.insert_vector_operand->type_text +
                   " " +
                   describe_import_constant(constant.insert_vector_operand->constant) +
                   ", " + constant.insert_element_operand->type_text + " " +
                   describe_import_constant(constant.insert_element_operand->constant) +
                   ", " + constant.insert_index_operand->type_text + " " +
                   describe_import_constant(constant.insert_index_operand->constant) +
                   ")";
        case AArch64LlvmImportConstantKind::ShuffleVector:
            if (constant.shuffle_lhs_operand == nullptr ||
                constant.shuffle_rhs_operand == nullptr ||
                constant.shuffle_mask_operand == nullptr) {
                return "shufflevector(<missing-operands>)";
            }
            return "shufflevector (" + constant.shuffle_lhs_operand->type_text +
                   " " +
                   describe_import_constant(constant.shuffle_lhs_operand->constant) +
                   ", " + constant.shuffle_rhs_operand->type_text + " " +
                   describe_import_constant(constant.shuffle_rhs_operand->constant) +
                   ", " + constant.shuffle_mask_operand->type_text + " " +
                   describe_import_constant(constant.shuffle_mask_operand->constant) +
                   ")";
        case AArch64LlvmImportConstantKind::Aggregate: {
            std::string text = "{ ";
            for (std::size_t index = 0; index < constant.elements.size(); ++index) {
                if (index != 0) {
                    text += ", ";
                }
                text += describe_import_constant(constant.elements[index]);
            }
            text += " }";
            return text;
        }
        case AArch64LlvmImportConstantKind::Invalid:
        default:
            return "<invalid-constant>";
        }
    }

    std::string describe_typed_operand(const AArch64LlvmImportTypedValue &operand) const {
        if (operand.kind == AArch64LlvmImportValueKind::Local) {
            return "%" + operand.local_name;
        }
        if (operand.kind == AArch64LlvmImportValueKind::Global) {
            return "@" + operand.global_name;
        }
        if (operand.kind == AArch64LlvmImportValueKind::Constant) {
            return describe_import_constant(operand.constant);
        }
        return "<unknown>";
    }

    ResolvedAddress resolve_typed_address_operand(
        const AArch64LlvmImportTypedValue &operand, CoreIrBasicBlock &block,
        std::unordered_map<std::string, ValueBinding> &bindings,
        std::size_t &synthetic_index, int line_number) {
        if (operand.kind == AArch64LlvmImportValueKind::Local) {
            const auto it = bindings.find(operand.local_name);
            if (it == bindings.end()) {
                add_error("unknown LLVM local value: %" + operand.local_name,
                          line_number, 1);
                return {};
            }
            if (it->second.stack_slot != nullptr) {
                ResolvedAddress resolved;
                resolved.stack_slot = it->second.stack_slot;
                return resolved;
            }
            ResolvedAddress resolved;
            resolved.address_value = it->second.value;
            return resolved;
        }
        if (operand.kind == AArch64LlvmImportValueKind::Global) {
            if (auto global_it = globals_.find(operand.global_name);
                global_it != globals_.end()) {
                CoreIrGlobal *global = global_it->second;
                const CoreIrType *pointer_type = pointer_to(global->get_type());
                ResolvedAddress resolved;
                resolved.address_value =
                    block.create_instruction<CoreIrAddressOfGlobalInst>(
                        pointer_type,
                        "ll.global.addr." + std::to_string(synthetic_index++),
                        global);
                return resolved;
            }
            if (auto function_it = functions_.find(operand.global_name);
                function_it != functions_.end()) {
                CoreIrFunction *function = function_it->second;
                const CoreIrType *pointer_type =
                    pointer_to(function->get_function_type());
                ResolvedAddress resolved;
                resolved.address_value =
                    block.create_instruction<CoreIrAddressOfFunctionInst>(
                        pointer_type,
                        "ll.function.addr." + std::to_string(synthetic_index++),
                        function);
                return resolved;
            }
            add_error("unsupported LLVM address operand: @" + operand.global_name,
                      line_number, 1);
            return {};
        }
        if (operand.kind == AArch64LlvmImportValueKind::Constant) {
            const CoreIrType *address_type = lower_import_type(operand.type);
            const CoreIrConstant *constant_address =
                lower_import_constant(address_type, operand.constant);
            if (constant_address == nullptr) {
                add_error("unsupported LLVM address operand: " +
                              describe_typed_operand(operand),
                          line_number, 1);
                return {};
            }
            ResolvedAddress resolved;
            resolved.address_value = const_cast<CoreIrConstant *>(constant_address);
            return resolved;
        }
        add_error("missing typed LLVM address operand", line_number, 1);
        return {};
    }

    CoreIrValue *resolve_typed_value_operand(
        const CoreIrType *type, const AArch64LlvmImportTypedValue &operand,
        CoreIrBasicBlock &block, std::unordered_map<std::string, ValueBinding> &bindings,
        std::size_t &synthetic_index, int line_number) {
        if (operand.kind == AArch64LlvmImportValueKind::Constant) {
            const CoreIrConstant *constant =
                lower_import_constant(type, operand.constant);
            return constant == nullptr ? nullptr
                                       : const_cast<CoreIrConstant *>(constant);
        }
        if (operand.kind == AArch64LlvmImportValueKind::Local) {
            const auto it = bindings.find(operand.local_name);
            if (it == bindings.end()) {
                add_error("unknown LLVM local value: %" + operand.local_name,
                          line_number, 1);
                return nullptr;
            }
            if (it->second.value != nullptr) {
                return it->second.value;
            }
            if (it->second.stack_slot != nullptr) {
                const CoreIrType *pointer_type =
                    pointer_to(it->second.stack_slot->get_allocated_type());
                return block.create_instruction<CoreIrAddressOfStackSlotInst>(
                    pointer_type,
                    "ll.stack.addr." + std::to_string(synthetic_index++),
                    it->second.stack_slot);
            }
        }
        if (operand.kind == AArch64LlvmImportValueKind::Global) {
            const ResolvedAddress resolved = resolve_typed_address_operand(
                operand, block, bindings, synthetic_index, line_number);
            return resolved.address_value;
        }
        add_error("unsupported LLVM value operand: " +
                      describe_typed_operand(operand),
                  line_number, 1);
        return nullptr;
    }

    const CoreIrType *compute_gep_result_pointee(
        const CoreIrType *source_type, const std::vector<CoreIrValue *> &indices) {
        if (source_type == nullptr) {
            return nullptr;
        }

        const CoreIrType *current = source_type;
        for (std::size_t index = 1; index < indices.size(); ++index) {
            if (current == nullptr) {
                return nullptr;
            }
            if (current->get_kind() == CoreIrTypeKind::Array) {
                current = static_cast<const CoreIrArrayType *>(current)->get_element_type();
                continue;
            }
            if (current->get_kind() == CoreIrTypeKind::Struct) {
                const auto *integer_constant =
                    dynamic_cast<const CoreIrConstantInt *>(indices[index]);
                if (integer_constant == nullptr) {
                    return nullptr;
                }
                const auto *struct_type =
                    static_cast<const CoreIrStructType *>(current);
                const std::size_t element_index =
                    static_cast<std::size_t>(integer_constant->get_value());
                if (element_index >= struct_type->get_element_types().size()) {
                    return nullptr;
                }
                current = struct_type->get_element_types()[element_index];
                continue;
            }
            return nullptr;
        }
        return current;
    }

    std::optional<long long> compute_constant_gep_byte_offset(
        const CoreIrType *source_type, const std::vector<CoreIrValue *> &indices) {
        if (source_type == nullptr) {
            return std::nullopt;
        }

        const CoreIrType *current = source_type;
        long long offset = 0;
        for (std::size_t index_position = 0; index_position < indices.size();
             ++index_position) {
            const auto *integer_constant =
                dynamic_cast<const CoreIrConstantInt *>(indices[index_position]);
            if (integer_constant == nullptr) {
                return std::nullopt;
            }
            const std::uint64_t index_value = integer_constant->get_value();

            if (index_position == 0) {
                offset += static_cast<long long>(index_value) *
                          static_cast<long long>(get_type_size(current));
                continue;
            }
            if (const auto *array_type = as_array_type(current); array_type != nullptr) {
                offset += static_cast<long long>(index_value) *
                          static_cast<long long>(
                              get_type_size(array_type->get_element_type()));
                current = array_type->get_element_type();
                continue;
            }
            if (const auto *struct_type =
                    dynamic_cast<const CoreIrStructType *>(current);
                struct_type != nullptr) {
                if (index_value >= struct_type->get_element_types().size()) {
                    return std::nullopt;
                }
                offset += static_cast<long long>(
                    get_struct_member_offset(struct_type,
                                             static_cast<std::size_t>(index_value)));
                current = struct_type->get_element_types()[index_value];
                continue;
            }
            if (const auto *pointer_type =
                    dynamic_cast<const CoreIrPointerType *>(current);
                pointer_type != nullptr) {
                offset += static_cast<long long>(index_value) *
                          static_cast<long long>(
                              get_type_size(pointer_type->get_pointee_type()));
                current = pointer_type->get_pointee_type();
                continue;
            }
            return std::nullopt;
        }
        return offset;
    }

    const CoreIrArrayType *as_array_type(const CoreIrType *type) const {
        return dynamic_cast<const CoreIrArrayType *>(type);
    }

    CoreIrConstantInt *get_i32_constant(std::uint64_t value) {
        const CoreIrType *i32_type = parse_type_text("i32");
        return context_->create_constant<CoreIrConstantInt>(i32_type, value);
    }

    CoreIrValue *materialize_stack_slot_address(CoreIrBasicBlock &block,
                                                CoreIrStackSlot *stack_slot,
                                                std::size_t &synthetic_index) {
        if (stack_slot == nullptr) {
            return nullptr;
        }
        return block.create_instruction<CoreIrAddressOfStackSlotInst>(
            pointer_to(stack_slot->get_allocated_type()),
            "ll.stack.addr." + std::to_string(synthetic_index++), stack_slot);
    }

    CoreIrValue *create_element_address(CoreIrBasicBlock &block,
                                        CoreIrValue *base_address,
                                        const CoreIrType *aggregate_type,
                                        std::uint64_t index_value,
                                        std::size_t &synthetic_index) {
        const auto *array_type = as_array_type(aggregate_type);
        if (array_type == nullptr || base_address == nullptr) {
            return nullptr;
        }
        std::vector<CoreIrValue *> indices;
        indices.push_back(get_i32_constant(0));
        indices.push_back(get_i32_constant(index_value));
        return block.create_instruction<CoreIrGetElementPtrInst>(
            pointer_to(array_type->get_element_type()),
            "ll.vec.gep." + std::to_string(synthetic_index++), base_address,
            indices);
    }

    bool materialize_constant_aggregate_to_slot(
        const CoreIrConstantAggregate *aggregate, CoreIrStackSlot *slot,
        CoreIrBasicBlock &block, std::size_t &synthetic_index) {
        if (aggregate == nullptr || slot == nullptr) {
            return false;
        }
        const auto *array_type = as_array_type(slot->get_allocated_type());
        if (array_type == nullptr) {
            return false;
        }
        CoreIrValue *slot_address =
            materialize_stack_slot_address(block, slot, synthetic_index);
        if (slot_address == nullptr) {
            return false;
        }
        const auto &elements = aggregate->get_elements();
        for (std::size_t index = 0; index < elements.size(); ++index) {
            CoreIrValue *element_address = create_element_address(
                block, slot_address, slot->get_allocated_type(),
                static_cast<std::uint64_t>(index), synthetic_index);
            if (element_address == nullptr) {
                return false;
            }
            block.create_instruction<CoreIrStoreInst>(
                void_type(), const_cast<CoreIrConstant *>(elements[index]),
                element_address);
        }
        return true;
    }

    const CoreIrConstant *make_zero_constant(const CoreIrType *type) {
        if (type == nullptr) {
            return nullptr;
        }
        if (type->get_kind() == CoreIrTypeKind::Integer) {
            return context_->create_constant<CoreIrConstantInt>(type, 0);
        }
        if (type->get_kind() == CoreIrTypeKind::Float) {
            return context_->create_constant<CoreIrConstantFloat>(type, "0.0");
        }
        if (type->get_kind() == CoreIrTypeKind::Pointer) {
            return context_->create_constant<CoreIrConstantNull>(type);
        }
        return context_->create_constant<CoreIrConstantZeroInitializer>(type);
    }

    bool materialize_zero_initializer_to_slot(const CoreIrType *type,
                                              CoreIrStackSlot *slot,
                                              CoreIrBasicBlock &block,
                                              std::size_t &synthetic_index) {
        const auto *array_type = as_array_type(type);
        if (array_type == nullptr || slot == nullptr) {
            return false;
        }
        CoreIrValue *slot_address =
            materialize_stack_slot_address(block, slot, synthetic_index);
        if (slot_address == nullptr) {
            return false;
        }
        for (std::size_t index = 0; index < array_type->get_element_count(); ++index) {
            CoreIrValue *element_address = create_element_address(
                block, slot_address, type, static_cast<std::uint64_t>(index),
                synthetic_index);
            const CoreIrConstant *zero =
                make_zero_constant(array_type->get_element_type());
            if (element_address == nullptr || zero == nullptr) {
                return false;
            }
            block.create_instruction<CoreIrStoreInst>(
                void_type(), const_cast<CoreIrConstant *>(zero), element_address);
        }
        return true;
    }

    CoreIrValue *ensure_addressable_aggregate_typed_operand(
        const CoreIrType *aggregate_type, const AArch64LlvmImportTypedValue &operand,
        CoreIrFunction &function, CoreIrBasicBlock &block,
        std::unordered_map<std::string, ValueBinding> &bindings,
        std::size_t &synthetic_index, int line_number) {
        if (operand.kind == AArch64LlvmImportValueKind::Local) {
            const auto it = bindings.find(operand.local_name);
            if (it == bindings.end()) {
                add_error("unknown LLVM aggregate value: %" + operand.local_name,
                          line_number, 1);
                return nullptr;
            }
            if (it->second.stack_slot != nullptr) {
                return materialize_stack_slot_address(block, it->second.stack_slot,
                                                      synthetic_index);
            }
            if (it->second.value != nullptr) {
                CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                    "ll.agg." + operand.local_name, aggregate_type,
                    get_storage_alignment(aggregate_type));
                CoreIrValue *slot_address =
                    materialize_stack_slot_address(block, slot, synthetic_index);
                block.create_instruction<CoreIrStoreInst>(void_type(),
                                                          it->second.value, slot);
                ValueBinding updated = it->second;
                updated.stack_slot = slot;
                bindings[operand.local_name] = updated;
                return slot_address;
            }
        }
        if (operand.kind == AArch64LlvmImportValueKind::Global) {
            const ResolvedAddress resolved = resolve_typed_address_operand(
                operand, block, bindings, synthetic_index, line_number);
            return resolved.address_value != nullptr
                       ? resolved.address_value
                       : materialize_stack_slot_address(block, resolved.stack_slot,
                                                        synthetic_index);
        }
        if (operand.kind == AArch64LlvmImportValueKind::Constant) {
            const CoreIrConstant *constant =
                lower_import_constant(aggregate_type, operand.constant);
            const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            const auto *zero_initializer =
                dynamic_cast<const CoreIrConstantZeroInitializer *>(constant);
            CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                "ll.const.vec." + std::to_string(synthetic_index), aggregate_type,
                get_storage_alignment(aggregate_type));
            const bool ok =
                aggregate != nullptr
                    ? materialize_constant_aggregate_to_slot(aggregate, slot, block,
                                                             synthetic_index)
                    : (zero_initializer != nullptr &&
                       materialize_zero_initializer_to_slot(
                           aggregate_type, slot, block, synthetic_index));
            if (!ok) {
                add_error("failed to materialize LLVM aggregate constant operand: " +
                              describe_typed_operand(operand),
                          line_number, 1);
                return nullptr;
            }
            return materialize_stack_slot_address(block, slot, synthetic_index);
        }
        add_error("unsupported LLVM aggregate operand: " +
                      describe_typed_operand(operand),
                  line_number, 1);
        return nullptr;
    }

    bool copy_aggregate_between_addresses(CoreIrBasicBlock &block,
                                          const CoreIrType *aggregate_type,
                                          CoreIrValue *destination_address,
                                          CoreIrValue *source_address,
                                          std::size_t &synthetic_index) {
        const auto *array_type = as_array_type(aggregate_type);
        if (array_type == nullptr || destination_address == nullptr ||
            source_address == nullptr) {
            return false;
        }
        for (std::size_t index = 0; index < array_type->get_element_count(); ++index) {
            CoreIrValue *source_element = create_element_address(
                block, source_address, aggregate_type,
                static_cast<std::uint64_t>(index), synthetic_index);
            CoreIrValue *destination_element = create_element_address(
                block, destination_address, aggregate_type,
                static_cast<std::uint64_t>(index), synthetic_index);
            if (source_element == nullptr || destination_element == nullptr) {
                return false;
            }
            CoreIrValue *loaded = block.create_instruction<CoreIrLoadInst>(
                array_type->get_element_type(),
                "ll.vec.load." + std::to_string(synthetic_index++), source_element);
            block.create_instruction<CoreIrStoreInst>(void_type(), loaded,
                                                      destination_element);
        }
        return true;
    }

    void retype_opaque_pointer_value(CoreIrValue *value,
                                     const CoreIrType *pointee_type) {
        if (value == nullptr || pointee_type == nullptr || value->get_type() == nullptr) {
            return;
        }
        const auto *pointer_type =
            dynamic_cast<const CoreIrPointerType *>(value->get_type());
        if (pointer_type == nullptr) {
            return;
        }
        if (pointer_type->get_pointee_type() != void_type()) {
            return;
        }
        value->set_type(pointer_to(pointee_type));
    }

    bool bind_instruction_result(
        const std::string &name, CoreIrValue *value,
        std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.value = value;
        bindings[name] = binding;
        return true;
    }

    bool bind_stack_slot_result(const std::string &name, CoreIrStackSlot *stack_slot,
                                std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.stack_slot = stack_slot;
        bindings[name] = binding;
        return true;
    }

    std::optional<CoreIrComparePredicate>
    parse_compare_predicate(const std::string &predicate_text,
                            bool is_float_compare) const {
        if (is_float_compare) {
            if (predicate_text == "oeq" || predicate_text == "ueq") {
                return CoreIrComparePredicate::Equal;
            }
            if (predicate_text == "one" || predicate_text == "une") {
                return CoreIrComparePredicate::NotEqual;
            }
            if (predicate_text == "olt" || predicate_text == "ult") {
                return CoreIrComparePredicate::SignedLess;
            }
            if (predicate_text == "ole" || predicate_text == "ule") {
                return CoreIrComparePredicate::SignedLessEqual;
            }
            if (predicate_text == "ogt" || predicate_text == "ugt") {
                return CoreIrComparePredicate::SignedGreater;
            }
            if (predicate_text == "oge" || predicate_text == "uge") {
                return CoreIrComparePredicate::SignedGreaterEqual;
            }
            return std::nullopt;
        }

        static const std::unordered_map<std::string, CoreIrComparePredicate>
            integer_predicates = {
                {"eq", CoreIrComparePredicate::Equal},
                {"ne", CoreIrComparePredicate::NotEqual},
                {"slt", CoreIrComparePredicate::SignedLess},
                {"sle", CoreIrComparePredicate::SignedLessEqual},
                {"sgt", CoreIrComparePredicate::SignedGreater},
                {"sge", CoreIrComparePredicate::SignedGreaterEqual},
                {"ult", CoreIrComparePredicate::UnsignedLess},
                {"ule", CoreIrComparePredicate::UnsignedLessEqual},
                {"ugt", CoreIrComparePredicate::UnsignedGreater},
                {"uge", CoreIrComparePredicate::UnsignedGreaterEqual},
            };
        const auto it = integer_predicates.find(predicate_text);
        return it == integer_predicates.end() ? std::nullopt
                                              : std::optional(it->second);
    }

    bool parse_instruction(const AArch64LlvmImportInstruction &instruction,
                           CoreIrFunction &function,
                           CoreIrBasicBlock *&current_block,
                           std::unordered_map<std::string, CoreIrBasicBlock *>
                               &block_map,
                           std::unordered_map<std::string, ValueBinding> &bindings,
                           std::size_t &synthetic_index) {
        CoreIrBasicBlock &block = *current_block;
        const std::string &line = instruction.canonical_text;
        const int line_number = instruction.line;
        const std::string &result_name = instruction.result_name;
        const std::string instruction_text = strip_metadata_suffix(line);
        const AArch64LlvmImportInstructionKind instruction_kind =
            instruction.kind;
        const std::string &opcode_text = instruction.opcode_text;
        const auto payload_after_opcode = [&]() -> std::string {
            const std::size_t opcode_pos = instruction_text.find(opcode_text);
            if (opcode_pos == std::string::npos) {
                return {};
            }
            return trim_copy(
                instruction_text.substr(opcode_pos + opcode_text.size()));
        };

        auto lower_binary = [&](CoreIrBinaryOpcode opcode,
                                const AArch64LlvmImportBinarySpec &spec) -> bool {
            const CoreIrType *type = lower_import_type(spec.type);
            if (type == nullptr) {
                add_error("unsupported LLVM binary instruction: " + line,
                          line_number, 1);
                return false;
            }
            if (const auto *array_type = as_array_type(type); array_type != nullptr) {
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.bin." + std::to_string(synthetic_index++)
                            : result_name,
                        type, get_storage_alignment(type));
                CoreIrValue *result_address = materialize_stack_slot_address(
                    block, result_slot, synthetic_index);
                CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                    type, spec.lhs, function, block, bindings,
                    synthetic_index,
                    line_number);
                CoreIrValue *rhs_address = ensure_addressable_aggregate_typed_operand(
                    type, spec.rhs, function, block, bindings,
                    synthetic_index,
                    line_number);
                if (result_address == nullptr || lhs_address == nullptr ||
                    rhs_address == nullptr) {
                    return false;
                }
                for (std::size_t lane = 0; lane < array_type->get_element_count();
                     ++lane) {
                    CoreIrValue *lhs_element = create_element_address(
                        block, lhs_address, type, static_cast<std::uint64_t>(lane),
                        synthetic_index);
                    CoreIrValue *rhs_element = create_element_address(
                        block, rhs_address, type, static_cast<std::uint64_t>(lane),
                        synthetic_index);
                    CoreIrValue *dst_element = create_element_address(
                        block, result_address, type, static_cast<std::uint64_t>(lane),
                        synthetic_index);
                    if (lhs_element == nullptr || rhs_element == nullptr ||
                        dst_element == nullptr) {
                        return false;
                    }
                    CoreIrValue *lhs_value = block.create_instruction<CoreIrLoadInst>(
                        array_type->get_element_type(),
                        "ll.vec.lhs." + std::to_string(synthetic_index++),
                        lhs_element);
                    CoreIrValue *rhs_value = block.create_instruction<CoreIrLoadInst>(
                        array_type->get_element_type(),
                        "ll.vec.rhs." + std::to_string(synthetic_index++),
                        rhs_element);
                    CoreIrValue *lane_value =
                        block.create_instruction<CoreIrBinaryInst>(
                            opcode, array_type->get_element_type(),
                            "ll.vec.bin.lane." + std::to_string(synthetic_index++),
                            lhs_value, rhs_value);
                    block.create_instruction<CoreIrStoreInst>(void_type(), lane_value,
                                                              dst_element);
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            CoreIrValue *lhs = resolve_typed_value_operand(
                type, spec.lhs, block, bindings, synthetic_index,
                line_number);
            CoreIrValue *rhs = resolve_typed_value_operand(
                type, spec.rhs, block, bindings, synthetic_index,
                line_number);
            if (lhs == nullptr || rhs == nullptr) {
                return false;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrBinaryInst>(opcode, type,
                                                           result_name, lhs, rhs),
                bindings);
        };

        auto lower_cast = [&](CoreIrCastKind kind,
                              const AArch64LlvmImportCastSpec &spec) -> bool {
            const CoreIrType *source_type = lower_import_type(spec.source_type);
            CoreIrValue *operand = resolve_typed_value_operand(
                source_type, spec.source_value, block, bindings,
                synthetic_index, line_number);
            const CoreIrType *target_type = lower_import_type(spec.target_type);
            if (operand == nullptr || target_type == nullptr) {
                add_error("unsupported LLVM cast target type", line_number, 1);
                return false;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrCastInst>(kind, target_type,
                                                         result_name, operand),
                bindings);
        };

        auto lower_identity_cast =
            [&](const AArch64LlvmImportCastSpec &spec) -> bool {
            const CoreIrType *source_type = lower_import_type(spec.source_type);
            const CoreIrType *target_type = lower_import_type(spec.target_type);
            CoreIrValue *operand = resolve_typed_value_operand(
                source_type, spec.source_value, block, bindings,
                synthetic_index, line_number);
            if (operand == nullptr || source_type == nullptr || target_type == nullptr) {
                add_error("unsupported LLVM cast target type", line_number, 1);
                return false;
            }
            const bool same_type = source_type == target_type;
            const bool pointer_identity =
                source_type->get_kind() == CoreIrTypeKind::Pointer &&
                target_type->get_kind() == CoreIrTypeKind::Pointer;
            if (!same_type && !pointer_identity) {
                add_error("unsupported LLVM identity-style cast: " + line,
                          line_number, 1);
                return false;
            }
            return bind_instruction_result(result_name, operand, bindings);
        };

        auto get_or_create_imported_declaration =
            [&](const std::string &callee_name, const CoreIrFunctionType *callee_type)
            -> CoreIrFunction * {
            if (auto it = functions_.find(callee_name); it != functions_.end()) {
                return it->second;
            }
            CoreIrFunction *callee =
                module_->create_function<CoreIrFunction>(callee_name, callee_type,
                                                         false, false);
            functions_[callee_name] = callee;
            return callee;
        };

        if (instruction_kind == AArch64LlvmImportInstructionKind::Binary) {
            const std::optional<AArch64LlvmImportBinarySpec> binary_spec =
                parse_llvm_import_binary_spec(instruction);
            if (!binary_spec.has_value()) {
                add_error("unsupported LLVM binary instruction: " + line,
                          line_number, 1);
                return false;
            }
            if (opcode_text == "add") {
                return lower_binary(CoreIrBinaryOpcode::Add, *binary_spec);
            }
            if (opcode_text == "fadd") {
                return lower_binary(CoreIrBinaryOpcode::Add, *binary_spec);
            }
            if (opcode_text == "sub") {
                return lower_binary(CoreIrBinaryOpcode::Sub, *binary_spec);
            }
            if (opcode_text == "fsub") {
                return lower_binary(CoreIrBinaryOpcode::Sub, *binary_spec);
            }
            if (opcode_text == "mul") {
                return lower_binary(CoreIrBinaryOpcode::Mul, *binary_spec);
            }
            if (opcode_text == "fmul") {
                return lower_binary(CoreIrBinaryOpcode::Mul, *binary_spec);
            }
            if (opcode_text == "sdiv") {
                return lower_binary(CoreIrBinaryOpcode::SDiv, *binary_spec);
            }
            if (opcode_text == "udiv") {
                return lower_binary(CoreIrBinaryOpcode::UDiv, *binary_spec);
            }
            if (opcode_text == "fdiv") {
                return lower_binary(CoreIrBinaryOpcode::SDiv, *binary_spec);
            }
            if (opcode_text == "srem") {
                return lower_binary(CoreIrBinaryOpcode::SRem, *binary_spec);
            }
            if (opcode_text == "urem") {
                return lower_binary(CoreIrBinaryOpcode::URem, *binary_spec);
            }
            if (opcode_text == "and") {
                return lower_binary(CoreIrBinaryOpcode::And, *binary_spec);
            }
            if (opcode_text == "or") {
                return lower_binary(CoreIrBinaryOpcode::Or, *binary_spec);
            }
            if (opcode_text == "xor") {
                return lower_binary(CoreIrBinaryOpcode::Xor, *binary_spec);
            }
            if (opcode_text == "shl") {
                return lower_binary(CoreIrBinaryOpcode::Shl, *binary_spec);
            }
            if (opcode_text == "lshr") {
                return lower_binary(CoreIrBinaryOpcode::LShr, *binary_spec);
            }
            if (opcode_text == "ashr") {
                return lower_binary(CoreIrBinaryOpcode::AShr, *binary_spec);
            }
            add_error("unsupported LLVM binary opcode: " + opcode_text,
                      line_number, 1);
            return false;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Cast) {
            const std::optional<AArch64LlvmImportCastSpec> cast_spec =
                parse_llvm_import_cast_spec(instruction);
            if (!cast_spec.has_value()) {
                add_error("unsupported LLVM cast instruction: " + line,
                          line_number, 1);
                return false;
            }
            if (opcode_text == "sext") {
                return lower_cast(CoreIrCastKind::SignExtend, *cast_spec);
            }
            if (opcode_text == "zext") {
                return lower_cast(CoreIrCastKind::ZeroExtend, *cast_spec);
            }
            if (opcode_text == "trunc") {
                return lower_cast(CoreIrCastKind::Truncate, *cast_spec);
            }
            if (opcode_text == "sitofp") {
                return lower_cast(CoreIrCastKind::SignedIntToFloat, *cast_spec);
            }
            if (opcode_text == "uitofp") {
                return lower_cast(CoreIrCastKind::UnsignedIntToFloat, *cast_spec);
            }
            if (opcode_text == "fptosi") {
                return lower_cast(CoreIrCastKind::FloatToSignedInt, *cast_spec);
            }
            if (opcode_text == "fptoui") {
                return lower_cast(CoreIrCastKind::FloatToUnsignedInt, *cast_spec);
            }
            if (opcode_text == "fpext") {
                return lower_cast(CoreIrCastKind::FloatExtend, *cast_spec);
            }
            if (opcode_text == "fptrunc") {
                return lower_cast(CoreIrCastKind::FloatTruncate, *cast_spec);
            }
            if (opcode_text == "bitcast") {
                return lower_identity_cast(*cast_spec);
            }
            if (opcode_text == "addrspacecast") {
                return lower_identity_cast(*cast_spec);
            }
            if (opcode_text == "inttoptr") {
                return lower_cast(CoreIrCastKind::IntToPtr, *cast_spec);
            }
            if (opcode_text == "ptrtoint") {
                return lower_cast(CoreIrCastKind::PtrToInt, *cast_spec);
            }
            add_error("unsupported LLVM cast opcode: " + opcode_text,
                      line_number, 1);
            return false;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Unary &&
            opcode_text == "fneg") {
            const std::optional<AArch64LlvmImportUnarySpec> unary_spec =
                parse_llvm_import_unary_spec(instruction);
            if (!unary_spec.has_value()) {
                add_error("unsupported LLVM unary instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *type = lower_import_type(unary_spec->type);
            CoreIrValue *operand = resolve_typed_value_operand(
                type, unary_spec->operand, block, bindings,
                synthetic_index, line_number);
            if (type == nullptr || operand == nullptr) {
                return false;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrUnaryInst>(
                    CoreIrUnaryOpcode::Negate, type, result_name, operand),
                bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Compare) {
            const std::optional<AArch64LlvmImportCompareSpec> compare_spec =
                parse_llvm_import_compare_spec(instruction);
            if (!compare_spec.has_value()) {
                add_error("unsupported LLVM compare instruction: " + line,
                          line_number, 1);
                return false;
            }
            const std::optional<CoreIrComparePredicate> predicate =
                parse_compare_predicate(compare_spec->predicate_text,
                                        compare_spec->is_float_compare);
            if (!predicate.has_value()) {
                add_error("unsupported LLVM compare predicate: " +
                              compare_spec->predicate_text,
                          line_number, 1);
                return false;
            }
            const CoreIrType *operand_type =
                lower_import_type(compare_spec->lhs.type);
            const CoreIrType *i1_type = parse_type_text("i1");
            CoreIrValue *lhs = resolve_typed_value_operand(
                operand_type, compare_spec->lhs, block, bindings,
                synthetic_index, line_number);
            CoreIrValue *rhs = resolve_typed_value_operand(
                operand_type, compare_spec->rhs, block, bindings,
                synthetic_index, line_number);
            if (lhs == nullptr || rhs == nullptr) {
                return false;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrCompareInst>(
                    *predicate, i1_type, result_name, lhs, rhs),
                bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Select) {
            const std::optional<AArch64LlvmImportSelectSpec> select_spec =
                parse_llvm_import_select_spec(instruction);
            if (!select_spec.has_value()) {
                add_error("unsupported LLVM select instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *condition_type =
                lower_import_type(select_spec->condition.type);
            const CoreIrType *result_type =
                lower_import_type(select_spec->true_value.type);
            const CoreIrType *false_type =
                lower_import_type(select_spec->false_value.type);
            if (condition_type == nullptr || result_type == nullptr ||
                false_type == nullptr || result_type != false_type) {
                add_error("unsupported LLVM select operand types: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *condition = resolve_typed_value_operand(
                condition_type, select_spec->condition, block,
                bindings, synthetic_index, line_number);
            CoreIrValue *true_value = resolve_typed_value_operand(
                result_type, select_spec->true_value, block, bindings,
                synthetic_index, line_number);
            CoreIrValue *false_value = resolve_typed_value_operand(
                result_type, select_spec->false_value, block,
                bindings, synthetic_index, line_number);
            if (condition == nullptr || true_value == nullptr ||
                false_value == nullptr) {
                return false;
            }
            if (current_block->get_has_terminator()) {
                add_error("cannot lower LLVM select after an existing terminator",
                          line_number, 1);
                return false;
            }

            const std::string suffix = ".llsel." + std::to_string(synthetic_index++);
            CoreIrBasicBlock *true_block =
                function.create_basic_block<CoreIrBasicBlock>(
                    current_block->get_name() + suffix + ".true");
            CoreIrBasicBlock *false_block =
                function.create_basic_block<CoreIrBasicBlock>(
                    current_block->get_name() + suffix + ".false");
            CoreIrBasicBlock *merge_block =
                function.create_basic_block<CoreIrBasicBlock>(
                    current_block->get_name() + suffix + ".merge");
            block_map[true_block->get_name()] = true_block;
            block_map[false_block->get_name()] = false_block;
            block_map[merge_block->get_name()] = merge_block;

            current_block->create_instruction<CoreIrCondJumpInst>(
                void_type(), condition, true_block, false_block);
            true_block->create_instruction<CoreIrJumpInst>(void_type(), merge_block);
            false_block->create_instruction<CoreIrJumpInst>(void_type(),
                                                            merge_block);

            current_block = merge_block;
            CoreIrPhiInst *phi =
                current_block->create_instruction<CoreIrPhiInst>(result_type,
                                                                 result_name);
            phi->add_incoming(true_block, true_value);
            phi->add_incoming(false_block, false_value);
            return bind_instruction_result(result_name, phi, bindings);
        }
        if (instruction_kind ==
            AArch64LlvmImportInstructionKind::ExtractElement) {
            const std::optional<AArch64LlvmImportExtractElementSpec>
                extract_spec =
                    parse_llvm_import_extractelement_spec(instruction);
            if (!extract_spec.has_value()) {
                add_error("unsupported LLVM extractelement instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *vector_type =
                lower_import_type(extract_spec->vector_value.type);
            const auto *array_type = as_array_type(vector_type);
            const CoreIrType *index_type =
                lower_import_type(extract_spec->index_value.type);
            if (array_type == nullptr || index_type == nullptr) {
                add_error("unsupported LLVM extractelement operand types: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *vector_address = ensure_addressable_aggregate_typed_operand(
                vector_type, extract_spec->vector_value, function,
                block, bindings, synthetic_index, line_number);
            CoreIrValue *index_value = resolve_typed_value_operand(
                index_type, extract_spec->index_value, block, bindings,
                synthetic_index, line_number);
            if (vector_address == nullptr || index_value == nullptr) {
                return false;
            }
            std::vector<CoreIrValue *> gep_indices;
            gep_indices.push_back(get_i32_constant(0));
            gep_indices.push_back(index_value);
            CoreIrValue *element_address =
                block.create_instruction<CoreIrGetElementPtrInst>(
                    pointer_to(array_type->get_element_type()),
                    "ll.vec.extract.addr." + std::to_string(synthetic_index++),
                    vector_address, gep_indices);
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrLoadInst>(
                    array_type->get_element_type(), result_name, element_address),
                bindings);
        }
        if (instruction_kind ==
            AArch64LlvmImportInstructionKind::InsertElement) {
            const std::optional<AArch64LlvmImportInsertElementSpec> insert_spec =
                parse_llvm_import_insertelement_spec(instruction);
            if (!insert_spec.has_value()) {
                add_error("unsupported LLVM insertelement instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *vector_type =
                lower_import_type(insert_spec->vector_value.type);
            const auto *array_type = as_array_type(vector_type);
            const CoreIrType *element_type =
                lower_import_type(insert_spec->element_value.type);
            const CoreIrType *index_type =
                lower_import_type(insert_spec->index_value.type);
            if (array_type == nullptr || element_type == nullptr ||
                index_type == nullptr) {
                add_error("unsupported LLVM insertelement operand types: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                vector_type, insert_spec->vector_value, function,
                block, bindings, synthetic_index, line_number);
            CoreIrValue *element_value = resolve_typed_value_operand(
                element_type, insert_spec->element_value, block,
                bindings, synthetic_index, line_number);
            CoreIrValue *index_value = resolve_typed_value_operand(
                index_type, insert_spec->index_value, block, bindings,
                synthetic_index, line_number);
            if (source_address == nullptr || element_value == nullptr ||
                index_value == nullptr) {
                return false;
            }
            CoreIrStackSlot *result_slot =
                function.create_stack_slot<CoreIrStackSlot>(
                    result_name.empty()
                        ? "ll.vec.insert." + std::to_string(synthetic_index++)
                        : result_name,
                    vector_type, get_storage_alignment(vector_type));
            CoreIrValue *result_address = materialize_stack_slot_address(
                block, result_slot, synthetic_index);
            if (result_address == nullptr ||
                !copy_aggregate_between_addresses(block, vector_type, result_address,
                                                 source_address, synthetic_index)) {
                return false;
            }
            std::vector<CoreIrValue *> gep_indices;
            gep_indices.push_back(get_i32_constant(0));
            gep_indices.push_back(index_value);
            CoreIrValue *element_address =
                block.create_instruction<CoreIrGetElementPtrInst>(
                    pointer_to(array_type->get_element_type()),
                    "ll.vec.insert.addr." + std::to_string(synthetic_index++),
                    result_address, gep_indices);
            block.create_instruction<CoreIrStoreInst>(void_type(), element_value,
                                                      element_address);
            return bind_stack_slot_result(result_name, result_slot, bindings);
        }
        if (instruction_kind ==
            AArch64LlvmImportInstructionKind::ShuffleVector) {
            const std::optional<AArch64LlvmImportShuffleVectorSpec> shuffle_spec =
                parse_llvm_import_shufflevector_spec(instruction);
            if (!shuffle_spec.has_value()) {
                add_error("unsupported LLVM shufflevector instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *lhs_type =
                lower_import_type(shuffle_spec->lhs_value.type);
            const CoreIrType *rhs_type =
                lower_import_type(shuffle_spec->rhs_value.type);
            const CoreIrType *mask_type =
                lower_import_type(shuffle_spec->mask_value.type);
            const auto *lhs_array = as_array_type(lhs_type);
            const auto *rhs_array = as_array_type(rhs_type);
            const auto *mask_array = as_array_type(mask_type);
            if (lhs_array == nullptr || rhs_array == nullptr ||
                mask_array == nullptr) {
                add_error("unsupported LLVM shufflevector operand types: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                lhs_type, shuffle_spec->lhs_value, function, block,
                bindings, synthetic_index, line_number);
            CoreIrValue *rhs_address = ensure_addressable_aggregate_typed_operand(
                rhs_type, shuffle_spec->rhs_value, function, block,
                bindings, synthetic_index, line_number);
            const CoreIrConstant *mask_constant =
                shuffle_spec->mask_value.kind == AArch64LlvmImportValueKind::Constant
                    ? lower_import_constant(mask_type,
                                            shuffle_spec->mask_value.constant)
                    : nullptr;
            const auto *mask_aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(mask_constant);
            if (lhs_address == nullptr || rhs_address == nullptr ||
                mask_aggregate == nullptr) {
                add_error("unsupported LLVM shufflevector mask: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrStackSlot *result_slot =
                function.create_stack_slot<CoreIrStackSlot>(
                    result_name.empty()
                        ? "ll.vec.shuffle." + std::to_string(synthetic_index++)
                        : result_name,
                    lhs_type, get_storage_alignment(lhs_type));
            CoreIrValue *result_address = materialize_stack_slot_address(
                block, result_slot, synthetic_index);
            if (result_address == nullptr) {
                return false;
            }
            const std::size_t lhs_count = lhs_array->get_element_count();
            for (std::size_t lane = 0; lane < mask_aggregate->get_elements().size();
                 ++lane) {
                const auto *mask_element =
                    dynamic_cast<const CoreIrConstantInt *>(
                        mask_aggregate->get_elements()[lane]);
                if (mask_element == nullptr) {
                    return false;
                }
                const std::uint64_t selected_index = mask_element->get_value();
                CoreIrValue *source_base = selected_index < lhs_count
                                               ? lhs_address
                                               : rhs_address;
                const std::uint64_t source_lane =
                    selected_index < lhs_count ? selected_index
                                               : selected_index - lhs_count;
                CoreIrValue *source_element = create_element_address(
                    block, source_base,
                    selected_index < lhs_count ? lhs_type : rhs_type, source_lane,
                    synthetic_index);
                CoreIrValue *destination_element = create_element_address(
                    block, result_address, lhs_type,
                    static_cast<std::uint64_t>(lane), synthetic_index);
                if (source_element == nullptr || destination_element == nullptr) {
                    return false;
                }
                CoreIrValue *loaded = block.create_instruction<CoreIrLoadInst>(
                    lhs_array->get_element_type(),
                    "ll.vec.shuffle.load." + std::to_string(synthetic_index++),
                    source_element);
                block.create_instruction<CoreIrStoreInst>(void_type(), loaded,
                                                          destination_element);
            }
            return bind_stack_slot_result(result_name, result_slot, bindings);
        }
        if (instruction_kind ==
            AArch64LlvmImportInstructionKind::VectorReduceAdd) {
            const std::optional<AArch64LlvmImportVectorReduceAddSpec>
                reduce_spec =
                    parse_llvm_import_vector_reduce_add_spec(instruction);
            if (!reduce_spec.has_value()) {
                add_error("unsupported LLVM vector reduce call: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *return_type =
                lower_import_type(reduce_spec->return_type);
            const CoreIrType *vector_type =
                lower_import_type(reduce_spec->vector_value.type);
            const auto *array_type = as_array_type(vector_type);
            CoreIrValue *vector_address = ensure_addressable_aggregate_typed_operand(
                vector_type, reduce_spec->vector_value, function,
                block, bindings, synthetic_index, line_number);
            if (array_type == nullptr || vector_address == nullptr) {
                add_error("unsupported LLVM vector reduce operand: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *accumulator = nullptr;
            for (std::size_t lane = 0; lane < array_type->get_element_count(); ++lane) {
                CoreIrValue *element_address = create_element_address(
                    block, vector_address, vector_type,
                    static_cast<std::uint64_t>(lane), synthetic_index);
                if (element_address == nullptr) {
                    return false;
                }
                CoreIrValue *element_value = block.create_instruction<CoreIrLoadInst>(
                    array_type->get_element_type(),
                    "ll.vec.reduce.load." + std::to_string(synthetic_index++),
                    element_address);
                if (accumulator == nullptr) {
                    accumulator = element_value;
                    continue;
                }
                accumulator = block.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Add, return_type,
                    "ll.vec.reduce.add." + std::to_string(synthetic_index++),
                    accumulator, element_value);
            }
            return bind_instruction_result(result_name, accumulator, bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Alloca) {
            const std::optional<AArch64LlvmImportAllocaSpec> alloca_spec =
                parse_llvm_import_alloca_spec(instruction);
            if (!alloca_spec.has_value()) {
                add_error("failed to parse LLVM alloca instruction", line_number,
                          1);
                return false;
            }
            const CoreIrType *allocated_type =
                lower_import_type(alloca_spec->allocated_type);
            if (allocated_type == nullptr) {
                add_error("unsupported LLVM alloca type: " +
                              alloca_spec->allocated_type_text,
                          line_number, 1);
                return false;
            }
            ValueBinding binding;
            binding.stack_slot = function.create_stack_slot<CoreIrStackSlot>(
                result_name, allocated_type, alloca_spec->alignment);
            bindings[result_name] = binding;
            return true;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Load) {
            const std::optional<AArch64LlvmImportLoadSpec> load_spec =
                parse_llvm_import_load_spec(instruction);
            if (!load_spec.has_value()) {
                add_error("unsupported LLVM load instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *load_type = lower_import_type(load_spec->load_type);
            if (load_type == nullptr) {
                add_error("unsupported LLVM load type: " +
                              load_spec->load_type_text,
                          line_number, 1);
                return false;
            }
            if (load_spec->address.type.kind !=
                AArch64LlvmImportTypeKind::Pointer) {
                add_error("unsupported LLVM load address operand: " +
                              load_spec->address.type_text,
                          line_number, 1);
                return false;
            }
            ResolvedAddress address = resolve_typed_address_operand(
                load_spec->address, block, bindings, synthetic_index,
                line_number);
            if (const auto *array_type = as_array_type(load_type); array_type != nullptr) {
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.load." + std::to_string(synthetic_index++)
                            : result_name,
                        load_type,
                        load_spec->alignment == 0
                            ? get_storage_alignment(load_type)
                            : load_spec->alignment);
                CoreIrValue *result_address = materialize_stack_slot_address(
                    block, result_slot, synthetic_index);
                CoreIrValue *source_address =
                    address.stack_slot != nullptr
                        ? materialize_stack_slot_address(block, address.stack_slot,
                                                         synthetic_index)
                        : address.address_value;
                if (result_address == nullptr || source_address == nullptr ||
                    !copy_aggregate_between_addresses(block, load_type,
                                                     result_address, source_address,
                                                     synthetic_index)) {
                    return false;
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            CoreIrValue *load_result = nullptr;
            if (address.stack_slot != nullptr) {
                load_result = block.create_instruction<CoreIrLoadInst>(
                    load_type, result_name, address.stack_slot,
                    load_spec->alignment);
            } else {
                load_result = block.create_instruction<CoreIrLoadInst>(
                    load_type, result_name, address.address_value,
                    load_spec->alignment);
            }
            return bind_instruction_result(result_name, load_result, bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Store) {
            const std::optional<AArch64LlvmImportStoreSpec> store_spec =
                parse_llvm_import_store_spec(instruction);
            if (!store_spec.has_value()) {
                add_error("unsupported LLVM store instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *value_type =
                lower_import_type(store_spec->value.type);
            if (value_type == nullptr) {
                add_error("unsupported LLVM store value type: " +
                              store_spec->value.type_text,
                          line_number, 1);
                return false;
            }
            CoreIrValue *value = resolve_typed_value_operand(
                value_type, store_spec->value, block, bindings,
                synthetic_index, line_number);
            if (value == nullptr ||
                store_spec->address.type.kind !=
                    AArch64LlvmImportTypeKind::Pointer) {
                add_error("unsupported LLVM store address operand: " +
                              store_spec->address.type_text,
                          line_number, 1);
                return false;
            }
            ResolvedAddress address = resolve_typed_address_operand(
                store_spec->address, block, bindings, synthetic_index,
                line_number);
            if (as_array_type(value_type) != nullptr) {
                CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                    value_type, store_spec->value,
                    function, block, bindings, synthetic_index, line_number);
                CoreIrValue *destination_address =
                    address.stack_slot != nullptr
                        ? materialize_stack_slot_address(block, address.stack_slot,
                                                         synthetic_index)
                        : address.address_value;
                if (source_address == nullptr || destination_address == nullptr ||
                    !copy_aggregate_between_addresses(block, value_type,
                                                     destination_address,
                                                     source_address,
                                                     synthetic_index)) {
                    return false;
                }
                return true;
            }
            if (address.stack_slot != nullptr) {
                block.create_instruction<CoreIrStoreInst>(void_type(), value,
                                                          address.stack_slot,
                                                          store_spec->alignment);
            } else {
                block.create_instruction<CoreIrStoreInst>(void_type(), value,
                                                          address.address_value,
                                                          store_spec->alignment);
            }
            return true;
        }
        if (instruction_kind ==
            AArch64LlvmImportInstructionKind::GetElementPtr) {
            const std::optional<AArch64LlvmImportGetElementPtrSpec> gep_spec =
                parse_llvm_import_gep_spec(instruction);
            if (!gep_spec.has_value()) {
                add_error("unsupported LLVM getelementptr instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *source_type =
                lower_import_type(gep_spec->source_type);
            if (source_type == nullptr ||
                gep_spec->base.type.kind != AArch64LlvmImportTypeKind::Pointer) {
                add_error("unsupported LLVM getelementptr base operand: " +
                              gep_spec->base.type_text,
                          line_number, 1);
                return false;
            }
            CoreIrValue *base = resolve_typed_value_operand(
                pointer_to(source_type), gep_spec->base, block,
                bindings, synthetic_index, line_number);
            if (base == nullptr) {
                return false;
            }
            std::vector<CoreIrValue *> indices;
            for (const AArch64LlvmImportTypedValue &index_operand :
                 gep_spec->indices) {
                const CoreIrType *index_type =
                    lower_import_type(index_operand.type);
                CoreIrValue *index_value = resolve_typed_value_operand(
                    index_type, index_operand,
                    block, bindings, synthetic_index, line_number);
                if (index_type == nullptr || index_value == nullptr) {
                    return false;
                }
                indices.push_back(index_value);
            }
            const CoreIrType *result_pointee =
                compute_gep_result_pointee(source_type, indices);
            if (result_pointee == nullptr) {
                add_error("unsupported LLVM getelementptr shape: " + line,
                          line_number, 1);
                return false;
            }
            const auto *base_pointer_type =
                dynamic_cast<const CoreIrPointerType *>(base->get_type());
            const bool needs_explicit_source_type =
                base_pointer_type != nullptr &&
                base_pointer_type->get_pointee_type() != void_type() &&
                base_pointer_type->get_pointee_type() != source_type;
            if (needs_explicit_source_type) {
                const auto byte_offset =
                    compute_constant_gep_byte_offset(source_type, indices);
                if (!byte_offset.has_value()) {
                    add_error("unsupported LLVM getelementptr source/base type mismatch: " +
                                  line,
                              line_number, 1);
                    return false;
                }
                const CoreIrType *i64_type = parse_type_text("i64");
                CoreIrValue *base_int = block.create_instruction<CoreIrCastInst>(
                    CoreIrCastKind::PtrToInt, i64_type,
                    "ll.gep.ptrtoint." + std::to_string(synthetic_index++), base);
                CoreIrValue *address_int = base_int;
                if (*byte_offset != 0) {
                    const CoreIrConstant *offset_constant =
                        context_->create_constant<CoreIrConstantInt>(
                            i64_type, static_cast<std::uint64_t>(*byte_offset));
                    address_int = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Add, i64_type,
                        "ll.gep.add." + std::to_string(synthetic_index++), base_int,
                        const_cast<CoreIrConstant *>(offset_constant));
                }
                CoreIrValue *typed_address =
                    block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::IntToPtr, pointer_to(result_pointee),
                        result_name, address_int);
                return bind_instruction_result(result_name, typed_address, bindings);
            }
            retype_opaque_pointer_value(base, source_type);
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrGetElementPtrInst>(
                    pointer_to(result_pointee), result_name, base, indices),
                bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Call) {
            const std::optional<AArch64LlvmImportCallSpec> call_spec =
                parse_llvm_import_call_spec(instruction);
            if (!call_spec.has_value()) {
                add_error("unsupported LLVM call instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *return_type =
                lower_import_type(call_spec->return_type);
            if (return_type == nullptr) {
                add_error("failed to parse LLVM call return type", line_number, 1);
                return false;
            }
            std::vector<CoreIrValue *> arguments;
            std::vector<const CoreIrType *> argument_types;
            for (const AArch64LlvmImportTypedValue &argument :
                 call_spec->arguments) {
                const CoreIrType *argument_type =
                    lower_import_type(argument.type);
                CoreIrValue *argument_value = resolve_typed_value_operand(
                    argument_type, argument,
                    block, bindings, synthetic_index, line_number);
                if (argument_type == nullptr || argument_value == nullptr) {
                    add_error("unsupported LLVM call argument: " +
                                  argument.type_text + " " +
                                  describe_typed_operand(argument),
                              line_number, 1);
                    return false;
                }
                arguments.push_back(argument_value);
                argument_types.push_back(argument_type);
            }
            const CoreIrFunctionType *callee_type =
                context_->create_type<CoreIrFunctionType>(return_type,
                                                          argument_types, false);
            if (call_spec->callee.kind == AArch64LlvmImportValueKind::Global) {
                const std::string &callee_name = call_spec->callee.global_name;
                if (callee_name == "llvm.stacksave.p0") {
                    if (return_type->get_kind() != CoreIrTypeKind::Pointer) {
                        add_error("unsupported llvm.stacksave return type: " + line,
                                  line_number, 1);
                        return false;
                    }
                    return bind_instruction_result(
                        result_name,
                        context_->create_constant<CoreIrConstantNull>(return_type),
                        bindings);
                }
                if (callee_name == "llvm.stackrestore.p0") {
                    return true;
                }
                auto lower_memory_intrinsic_call =
                    [&](const std::string &runtime_name,
                        std::size_t expected_minimum_arguments,
                        std::size_t used_argument_count) -> bool {
                    if (arguments.size() < expected_minimum_arguments ||
                        argument_types.size() < expected_minimum_arguments) {
                        add_error("unsupported LLVM intrinsic call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    std::vector<CoreIrValue *> runtime_arguments(
                        arguments.begin(), arguments.begin() + used_argument_count);
                    std::vector<const CoreIrType *> runtime_argument_types(
                        argument_types.begin(),
                        argument_types.begin() + used_argument_count);
                    const CoreIrFunctionType *runtime_callee_type =
                        context_->create_type<CoreIrFunctionType>(
                            return_type, runtime_argument_types, false);
                    CoreIrFunction *callee = get_or_create_imported_declaration(
                        runtime_name, runtime_callee_type);
                    CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                        return_type, result_name, callee->get_name(),
                        runtime_callee_type, runtime_arguments);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, call_value,
                                                         bindings);
                };
                if (callee_name.rfind("llvm.memcpy.", 0) == 0) {
                    return lower_memory_intrinsic_call("memcpy", 4, 3);
                }
                if (callee_name.rfind("llvm.memmove.", 0) == 0) {
                    return lower_memory_intrinsic_call("memmove", 4, 3);
                }
                if (callee_name.rfind("llvm.memset.", 0) == 0) {
                    return lower_memory_intrinsic_call("memset", 4, 3);
                }
            }
            if (call_spec->callee.kind == AArch64LlvmImportValueKind::Global) {
                const std::string &callee_name = call_spec->callee.global_name;
                CoreIrFunction *callee = nullptr;
                if (auto callee_it = functions_.find(callee_name);
                    callee_it != functions_.end()) {
                    callee = callee_it->second;
                }
                if (callee == nullptr) {
                    callee = module_->create_function<CoreIrFunction>(
                        callee_name, callee_type, false, false);
                    functions_[callee_name] = callee;
                }
                CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                    return_type, result_name, callee->get_name(), callee_type,
                    arguments);
                return result_name.empty()
                           ? true
                           : bind_instruction_result(result_name, call_value,
                                                     bindings);
            }
            if (call_spec->callee.kind != AArch64LlvmImportValueKind::Local) {
                add_error("unsupported LLVM call callee operand: " +
                              describe_typed_operand(call_spec->callee),
                          line_number, 1);
                return false;
            }
            CoreIrValue *callee_value = resolve_typed_value_operand(
                pointer_to(callee_type), call_spec->callee, block, bindings,
                synthetic_index, line_number);
            if (callee_value == nullptr) {
                return false;
            }
            CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                return_type, result_name, callee_value, callee_type, arguments);
            return result_name.empty()
                       ? true
                       : bind_instruction_result(result_name, call_value,
                                                 bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Phi) {
            const std::optional<AArch64LlvmImportPhiSpec> phi_spec =
                parse_llvm_import_phi_spec(instruction);
            if (!phi_spec.has_value()) {
                add_error("unsupported LLVM phi instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *type = lower_import_type(phi_spec->type);
            if (type == nullptr) {
                add_error("unsupported LLVM phi type: " + phi_spec->type_text,
                          line_number, 1);
                return false;
            }
            CoreIrPhiInst *phi =
                block.create_instruction<CoreIrPhiInst>(type, result_name);
            for (const AArch64LlvmImportPhiIncoming &incoming :
                 phi_spec->incoming_values) {
                CoreIrValue *incoming_value = resolve_typed_value_operand(
                    type, incoming.value, block, bindings, synthetic_index,
                    line_number);
                auto block_it = block_map.find(incoming.block_label);
                if (block_it == block_map.end() && incoming.block_label == "1") {
                    block_it = block_map.find("0");
                }
                if (incoming_value == nullptr || block_it == block_map.end()) {
                    add_error("unsupported LLVM phi incoming block: " +
                                  incoming.block_label,
                              line_number, 1);
                    return false;
                }
                phi->add_incoming(block_it->second, incoming_value);
            }
            return bind_instruction_result(result_name, phi, bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Branch ||
            instruction_kind == AArch64LlvmImportInstructionKind::CondBranch) {
            const std::optional<AArch64LlvmImportBranchSpec> branch_spec =
                parse_llvm_import_branch_spec(instruction);
            if (!branch_spec.has_value()) {
                add_error("unsupported LLVM branch instruction: " + line,
                          line_number, 1);
                return false;
            }
            if (!branch_spec->is_conditional) {
                const auto target_it =
                    block_map.find(branch_spec->true_target_label);
                if (target_it == block_map.end()) {
                    add_error("unknown LLVM branch target: " +
                                  branch_spec->true_target_label,
                              line_number, 1);
                    return false;
                }
                block.create_instruction<CoreIrJumpInst>(void_type(),
                                                         target_it->second);
                return true;
            }

            const CoreIrType *condition_type =
                lower_import_type(branch_spec->condition.type);
            if (condition_type == nullptr) {
                add_error("failed to parse LLVM branch condition type",
                          line_number, 1);
                return false;
            }
            CoreIrValue *condition = resolve_typed_value_operand(
                condition_type, branch_spec->condition, block,
                bindings, synthetic_index, line_number);
            const auto true_it =
                block_map.find(branch_spec->true_target_label);
            const auto false_it =
                block_map.find(branch_spec->false_target_label);
            if (condition == nullptr || true_it == block_map.end() ||
                false_it == block_map.end()) {
                add_error("unsupported LLVM branch targets in: " + line,
                          line_number, 1);
                return false;
            }
            block.create_instruction<CoreIrCondJumpInst>(
                void_type(), condition, true_it->second, false_it->second);
            return true;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Switch) {
            const std::optional<AArch64LlvmImportSwitchSpec> switch_spec =
                parse_llvm_import_switch_spec(instruction);
            if (!switch_spec.has_value()) {
                add_error("unsupported LLVM switch instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *selector_type =
                lower_import_type(switch_spec->selector.type);
            const auto *selector_integer =
                dynamic_cast<const CoreIrIntegerType *>(selector_type);
            const CoreIrType *i1_type = parse_type_text("i1");
            CoreIrValue *selector = resolve_typed_value_operand(
                selector_type, switch_spec->selector, block, bindings,
                synthetic_index, line_number);
            const auto default_it =
                block_map.find(switch_spec->default_target_label);
            if (selector_integer == nullptr || i1_type == nullptr ||
                selector == nullptr || default_it == block_map.end()) {
                add_error("unsupported LLVM switch selector or targets: " + line,
                          line_number, 1);
                return false;
            }
            if (switch_spec->cases.empty()) {
                block.create_instruction<CoreIrJumpInst>(void_type(),
                                                         default_it->second);
                return true;
            }

            CoreIrBasicBlock *test_block = current_block;
            for (std::size_t case_index = 0; case_index < switch_spec->cases.size();
                 ++case_index) {
                const AArch64LlvmImportSwitchCase &case_entry =
                    switch_spec->cases[case_index];
                const auto target_it = block_map.find(case_entry.target_label);
                if (target_it == block_map.end()) {
                    add_error("unknown LLVM switch target: " +
                                  case_entry.target_label,
                              line_number, 1);
                    return false;
                }
                CoreIrBasicBlock *false_target = nullptr;
                if (case_index + 1 == switch_spec->cases.size()) {
                    false_target = default_it->second;
                } else {
                    false_target = function.create_basic_block<CoreIrBasicBlock>(
                        current_block->get_name() + ".llswitch." +
                        std::to_string(synthetic_index++) + ".case" +
                        std::to_string(case_index));
                    block_map[false_target->get_name()] = false_target;
                }
                CoreIrValue *case_value =
                    context_->create_constant<CoreIrConstantInt>(
                        selector_type, case_entry.value);
                CoreIrValue *compare = test_block->create_instruction<CoreIrCompareInst>(
                    CoreIrComparePredicate::Equal, i1_type,
                    "ll.switch.eq." + std::to_string(synthetic_index++), selector,
                    case_value);
                test_block->create_instruction<CoreIrCondJumpInst>(
                    void_type(), compare, target_it->second, false_target);
                test_block = false_target;
            }
            return true;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::IndirectBranch) {
            const auto indirect_spec =
                parse_llvm_import_indirect_branch_spec(instruction);
            if (!indirect_spec.has_value()) {
                add_error("unsupported LLVM indirectbr instruction: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *address_type =
                lower_import_type(indirect_spec->address.type);
            if (address_type == nullptr ||
                address_type->get_kind() != CoreIrTypeKind::Pointer) {
                add_error("unsupported LLVM indirectbr address type: " +
                              indirect_spec->address.type_text,
                          line_number, 1);
                return false;
            }
            CoreIrValue *address = resolve_typed_value_operand(
                address_type, indirect_spec->address, block, bindings,
                synthetic_index, line_number);
            if (address == nullptr) {
                return false;
            }
            std::vector<CoreIrBasicBlock *> targets;
            targets.reserve(indirect_spec->target_labels.size());
            for (const std::string &target_label : indirect_spec->target_labels) {
                const auto block_it = block_map.find(target_label);
                if (block_it == block_map.end()) {
                    add_error("unknown LLVM indirectbr target: " + target_label,
                              line_number, 1);
                    return false;
                }
                targets.push_back(block_it->second);
            }
            block.create_instruction<CoreIrIndirectJumpInst>(void_type(), address,
                                                             std::move(targets));
            return true;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Unreachable) {
            const CoreIrType *function_return_type =
                function.get_function_type()->get_return_type();
            if (function_return_type == void_type()) {
                block.create_instruction<CoreIrReturnInst>(void_type());
                return true;
            }
            const CoreIrConstant *fallback_return =
                make_zero_constant(function_return_type);
            if (fallback_return == nullptr) {
                add_error("unsupported LLVM unreachable terminator fallback: " + line,
                          line_number, 1);
                return false;
            }
            block.create_instruction<CoreIrReturnInst>(
                void_type(), const_cast<CoreIrConstant *>(fallback_return));
            return true;
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Return) {
            const std::optional<AArch64LlvmImportReturnSpec> return_spec =
                parse_llvm_import_return_spec(instruction);
            if (!return_spec.has_value()) {
                add_error("unsupported LLVM return instruction: " + line,
                          line_number, 1);
                return false;
            }
            if (return_spec->is_void) {
                block.create_instruction<CoreIrReturnInst>(void_type());
                return true;
            }
            const CoreIrType *return_type =
                lower_import_type(return_spec->value.type);
            CoreIrValue *return_value = resolve_typed_value_operand(
                return_type, return_spec->value, block,
                bindings, synthetic_index, line_number);
            if (return_type == nullptr || return_value == nullptr) {
                add_error("unsupported LLVM return operand", line_number, 1);
                return false;
            }
            block.create_instruction<CoreIrReturnInst>(void_type(), return_value);
            return true;
        }

        add_error("unsupported LLVM instruction in restricted importer: " +
                      instruction_text,
                  line_number, 1);
        return false;
    }

    bool lower_function_body(const PendingFunctionDefinition &pending) {
        CoreIrFunction &function = *pending.function;
        if (function.get_parameters().empty()) {
            for (std::size_t index = 0; index < pending.parameters.size(); ++index) {
                const ParameterSpec &parameter = pending.parameters[index];
                function.create_parameter<CoreIrParameter>(
                    parameter.type,
                    parameter.name.empty() ? "arg" + std::to_string(index)
                                           : parameter.name);
            }
        }

        if (pending.basic_blocks.empty()) {
            return diagnostics_.empty();
        }

        std::unordered_map<std::string, CoreIrBasicBlock *> block_map;
        for (std::size_t index = 0; index < pending.basic_blocks.size(); ++index) {
            const AArch64LlvmImportBasicBlock &record = pending.basic_blocks[index];
            CoreIrBasicBlock *block =
                function.create_basic_block<CoreIrBasicBlock>(record.label);
            block_map[record.label] = block;
            if (index == 0 && record.label == "0" && !record.instructions.empty()) {
                block_map["1"] = block;
                const std::string &first_result = record.instructions.front().result_name;
                if (!first_result.empty() &&
                    std::all_of(first_result.begin(), first_result.end(),
                                [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                    block_map[first_result] = block;
                }
            }
        }

        std::unordered_map<std::string, ValueBinding> bindings;
        for (const auto &parameter : function.get_parameters()) {
            ValueBinding binding;
            binding.value = parameter.get();
            bindings[parameter->get_name()] = binding;
        }

        std::size_t synthetic_index = 0;
        for (const AArch64LlvmImportBasicBlock &record : pending.basic_blocks) {
            CoreIrBasicBlock *current_block = block_map.at(record.label);
            for (const AArch64LlvmImportInstruction &instruction :
                 record.instructions) {
                if (!parse_instruction(instruction, function, current_block,
                                       block_map, bindings, synthetic_index)) {
                    return false;
                }
            }
        }

        return true;
    }

  public:
    explicit RestrictedLlvmIrImporter(std::string file_path)
        : file_path_(std::move(file_path)) {
        const std::filesystem::path path(file_path_);
        module_ = context_->create_module<CoreIrModule>(
            path.stem().string().empty() ? "llvm_import" : path.stem().string());
        void_type();
    }

    AArch64CoreIrImportedModule import_parsed_module(
        const AArch64LlvmImportModule &parsed_module) {
        source_target_triple_ = parsed_module.source_target_triple;
        module_asm_lines_ = parsed_module.module_asm_lines;
        diagnostics_ = parsed_module.diagnostics;
        if (!diagnostics_.empty()) {
            AArch64CoreIrImportedModule result;
            result.context = std::move(context_);
            result.module = nullptr;
            result.source_target_triple = source_target_triple_;
            result.module_asm_lines = std::move(module_asm_lines_);
            result.diagnostics = std::move(diagnostics_);
            return result;
        }

        for (const AArch64LlvmImportNamedType &named_type :
             parsed_module.named_types) {
            if (!lower_named_type_definition(named_type)) {
                break;
            }
        }

        for (const AArch64LlvmImportGlobal &global : parsed_module.globals) {
            if (!lower_global_definition(global)) {
                break;
            }
        }

        for (const AArch64LlvmImportFunction &function :
             parsed_module.functions) {
            PendingFunctionDefinition pending;
            if (!lower_function_signature(function,
                                          function.is_definition ? &pending
                                                                 : nullptr)) {
                break;
            }
            if (function.is_definition) {
                pending.basic_blocks = function.basic_blocks;
                pending_definitions_.push_back(std::move(pending));
            }
        }

        std::vector<bool> resolved_aliases(parsed_module.aliases.size(), false);
        bool made_progress = true;
        while (made_progress) {
            made_progress = false;
            for (std::size_t index = 0; index < parsed_module.aliases.size();
                 ++index) {
                if (resolved_aliases[index]) {
                    continue;
                }
                if (lower_alias_definition(parsed_module.aliases[index])) {
                    resolved_aliases[index] = true;
                    made_progress = true;
                }
            }
        }
        for (std::size_t index = 0; index < parsed_module.aliases.size(); ++index) {
            if (!resolved_aliases[index]) {
                add_error("unsupported LLVM alias target: " +
                              parsed_module.aliases[index].target_text,
                          parsed_module.aliases[index].line, 1);
            }
        }

        for (std::size_t index = 0; index < pending_definitions_.size(); ++index) {
            if (!lower_function_body(pending_definitions_[index])) {
                break;
            }
        }

        if (!diagnostics_.empty()) {
            AArch64CoreIrImportedModule result;
            result.context = std::move(context_);
            result.module = nullptr;
            result.source_target_triple = source_target_triple_;
            result.module_asm_lines = std::move(module_asm_lines_);
            result.diagnostics = std::move(diagnostics_);
            return result;
        }

        AArch64CoreIrImportedModule result;
        result.context = std::move(context_);
        result.module = module_;
        result.source_target_triple = source_target_triple_;
        result.module_asm_lines = std::move(module_asm_lines_);
        result.diagnostics = std::move(diagnostics_);
        return result;
    }
};

} // namespace

AArch64CoreIrImportedModule lower_llvm_import_model_to_core_ir(
    const AArch64LlvmImportModule &module) {
    RestrictedLlvmIrImporter importer(module.source_name);
    return importer.import_parsed_module(module);
}

} // namespace sysycc
