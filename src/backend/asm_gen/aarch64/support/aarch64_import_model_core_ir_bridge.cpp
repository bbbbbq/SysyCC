#include "backend/asm_gen/aarch64/support/aarch64_import_model_core_ir_bridge.hpp"

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_instruction_parse_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_literal_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
#include <unordered_set>
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

std::string strip_leading_modifiers(const std::string &text) {
    return llvm_import_strip_leading_modifiers(text);
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

std::optional<std::string> canonicalize_float_literal_text(
    CoreIrFloatKind kind, const std::string &literal_text) {
    const std::string trimmed = trim_copy(literal_text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (kind == CoreIrFloatKind::Float128) {
        return canonicalize_fp128_literal_text(trimmed);
    }

    if (starts_with(trimmed, "0x") || starts_with(trimmed, "-0x") ||
        starts_with(trimmed, "+0x") || starts_with(trimmed, "0X") ||
        starts_with(trimmed, "-0X") || starts_with(trimmed, "+0X")) {
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

bool is_import_integer_bit_width(const AArch64LlvmImportType &type,
                                 std::size_t bit_width) {
    return type.kind == AArch64LlvmImportTypeKind::Integer &&
           type.integer_bit_width == bit_width;
}

bool is_import_zero_i128_constant(const AArch64LlvmImportTypedValue &value) {
    if (value.kind != AArch64LlvmImportValueKind::Constant) {
        return false;
    }
    const AArch64LlvmImportConstant &constant = value.constant;
    if (constant.kind == AArch64LlvmImportConstantKind::Integer) {
        return is_import_integer_bit_width(value.type, 128) &&
               constant.integer_value == 0;
    }
    if (constant.kind != AArch64LlvmImportConstantKind::Aggregate ||
        constant.elements.size() != 2) {
        return false;
    }
    return constant.elements[0].kind == AArch64LlvmImportConstantKind::Integer &&
           constant.elements[0].integer_value == 0 &&
           constant.elements[1].kind == AArch64LlvmImportConstantKind::Integer &&
           constant.elements[1].integer_value == 0;
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
    const auto *float_type = dynamic_cast<const CoreIrFloatType *>(constant.get_type());
    if (float_type != nullptr &&
        float_type->get_float_kind() == CoreIrFloatKind::Float128) {
        return std::nullopt;
    }
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
    static constexpr const char *kInlineVecSminV4I32 =
        "__sysycc_aarch64_inline_vec_smin_v4i32";
    static constexpr const char *kInlineVecSmaxV4I32 =
        "__sysycc_aarch64_inline_vec_smax_v4i32";
    static constexpr const char *kInlineVecAddV4I32 =
        "__sysycc_aarch64_inline_vec_add_v4i32";
    static constexpr const char *kInlineVecMulV4I32 =
        "__sysycc_aarch64_inline_vec_mul_v4i32";
    static constexpr const char *kInlineVecAddV4I32SplatLhs =
        "__sysycc_aarch64_inline_vec_add_v4i32_splat_lhs";
    static constexpr const char *kInlineVecAddV4I32SplatRhs =
        "__sysycc_aarch64_inline_vec_add_v4i32_splat_rhs";
    static constexpr const char *kInlineVecMulV4I32SplatLhs =
        "__sysycc_aarch64_inline_vec_mul_v4i32_splat_lhs";
    static constexpr const char *kInlineVecMulV4I32SplatRhs =
        "__sysycc_aarch64_inline_vec_mul_v4i32_splat_rhs";
    static constexpr const char *kInlineCopyV4I32 =
        "__sysycc_aarch64_inline_copy_v4i32";
    static constexpr const char *kInlineZeroV4I32 =
        "__sysycc_aarch64_inline_zero_v4i32";
    static constexpr const char *kInlineSplatLane0V4I32 =
        "__sysycc_aarch64_inline_splat_lane0_v4i32";
    static constexpr const char *kInlineSplatScalarV4I32 =
        "__sysycc_aarch64_inline_splat_scalar_v4i32";
    static constexpr const char *kInlineInsertLane0ZeroedV4I32 =
        "__sysycc_aarch64_inline_insert_lane0_zeroed_v4i32";
    static constexpr const char *kInlineInsertLane0V4I32 =
        "__sysycc_aarch64_inline_insert_lane0_v4i32";
    static constexpr const char *kInlineInsertLane1V4I32 =
        "__sysycc_aarch64_inline_insert_lane1_v4i32";
    static constexpr const char *kInlineInsertLane2V4I32 =
        "__sysycc_aarch64_inline_insert_lane2_v4i32";
    static constexpr const char *kInlineInsertLane3V4I32 =
        "__sysycc_aarch64_inline_insert_lane3_v4i32";
    static constexpr const char *kInlineReduceSminV4I32 =
        "__sysycc_aarch64_inline_reduce_smin_v4i32";
    static constexpr const char *kInlineReduceSmaxV4I32 =
        "__sysycc_aarch64_inline_reduce_smax_v4i32";
    static constexpr const char *kInlineReduceAddV4I32 =
        "__sysycc_aarch64_inline_reduce_add_v4i32";
    static constexpr const char *kInlineReduceSminPairV4I32 =
        "__sysycc_aarch64_inline_reduce_smin_pair_v4i32";
    static constexpr const char *kInlineReduceSmaxPairV4I32 =
        "__sysycc_aarch64_inline_reduce_smax_pair_v4i32";
    static constexpr const char *kInlineReduceAddPairV4I32 =
        "__sysycc_aarch64_inline_reduce_add_pair_v4i32";

    struct ParameterSpec {
        const CoreIrType *type = nullptr;
        std::string name;
    };

    struct PendingFunctionDefinition {
        CoreIrFunction *function = nullptr;
        std::vector<ParameterSpec> parameters;
        std::vector<AArch64LlvmImportBasicBlock> basic_blocks;
    };

    struct PendingPhiIncoming {
        CoreIrPhiInst *phi = nullptr;
        CoreIrBasicBlock *phi_block = nullptr;
        CoreIrBasicBlock *incoming_block = nullptr;
        const CoreIrType *type = nullptr;
        AArch64LlvmImportTypedValue value;
        int line_number = 0;
    };

    struct PendingAggregatePhiIncoming {
        CoreIrPhiInst *phi = nullptr;
        CoreIrBasicBlock *incoming_block = nullptr;
        const CoreIrType *type = nullptr;
        AArch64LlvmImportTypedValue value;
        int line_number = 0;
    };

    struct ValueBinding {
        enum class DeferredVectorPairKind {
            None,
            Add,
            Mul,
            SMin,
            SMax,
        };

        CoreIrValue *value = nullptr;
        CoreIrStackSlot *stack_slot = nullptr;
        CoreIrValue *aggregate_address = nullptr;
        CoreIrValue *reinterpreted_source_value = nullptr;
        const CoreIrType *reinterpreted_source_type = nullptr;
        const CoreIrType *reinterpreted_target_type = nullptr;
        CoreIrValue *lane0_splat_scalar = nullptr;
        const CoreIrType *lane0_splat_vector_type = nullptr;
        CoreIrValue *lane0_zero_insert_scalar = nullptr;
        const CoreIrType *lane0_zero_insert_vector_type = nullptr;
        DeferredVectorPairKind deferred_vector_pair_kind =
            DeferredVectorPairKind::None;
        CoreIrValue *deferred_vector_lhs_address = nullptr;
        CoreIrValue *deferred_vector_rhs_address = nullptr;
        CoreIrValue *deferred_vector_lhs_splat_scalar = nullptr;
        CoreIrValue *deferred_vector_rhs_splat_scalar = nullptr;
        const CoreIrType *deferred_vector_pair_type = nullptr;
        struct WideIntegerValue {
            enum class HighBitFill {
                Zero,
                Sign,
            };
            enum class ShiftKind {
                Shl,
                LShr,
                AShr,
            };
            enum class BitwiseKind {
                And,
                Or,
                Xor,
            };

            struct ConstantWords {
                std::uint64_t low = 0;
                std::uint64_t high = 0;
            };

            struct ShiftOp {
                ShiftKind kind = ShiftKind::Shl;
                std::size_t amount = 0;
            };
            struct BitwiseConstantOp {
                BitwiseKind kind = BitwiseKind::And;
                ConstantWords words;
            };

            CoreIrValue *source_address = nullptr;
            std::size_t source_bit_width = 0;
            HighBitFill high_bit_fill = HighBitFill::Zero;
            std::vector<ShiftOp> shift_ops;
            std::vector<BitwiseConstantOp> bitwise_constant_ops;
        };
        struct WideOverflowAggregate {
            WideIntegerValue wide_value;
            CoreIrValue *overflow_flag = nullptr;
        };
        std::optional<WideIntegerValue> wide_integer;
        std::optional<WideOverflowAggregate> wide_overflow_aggregate;
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
    std::unordered_set<std::string> extern_weak_function_names_;
    std::vector<std::string> module_asm_lines_;
    std::vector<PendingFunctionDefinition> pending_definitions_;
    std::vector<AArch64CodegenDiagnostic> diagnostics_;
    std::string source_target_triple_;
    std::vector<std::vector<CoreIrValue *>> dynamic_alloca_scopes_;

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
            const auto *integer_element = as_integer_type(element_type);
            if (integer_element != nullptr &&
                integer_element->get_bit_width() == 32 && element_count == 4) {
                type = context_->create_type<CoreIrVectorType>(element_type,
                                                               element_count);
            } else {
                type = context_->create_type<CoreIrArrayType>(element_type,
                                                              element_count);
            }
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
            std::string key = type.struct_is_packed ? "<{" : "{";
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
            key += type.struct_is_packed ? "}>" : "}";
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
            if (type.integer_bit_width == 128) {
                const CoreIrType *i64_type =
                    context_->create_type<CoreIrIntegerType>(64);
                lowered = context_->create_type<CoreIrArrayType>(i64_type, 2);
            } else {
                lowered = context_->create_type<CoreIrIntegerType>(
                    type.integer_bit_width);
            }
            break;
        case AArch64LlvmImportTypeKind::Array: {
            if (type.element_types.size() != 1) {
                return nullptr;
            }
            const CoreIrType *element_type = lower_import_type(type.element_types.front());
            if (element_type == nullptr) {
                return nullptr;
            }
            if (type.array_uses_vector_syntax &&
                type.array_element_count == 4) {
                const auto *integer_element = as_integer_type(element_type);
                if (integer_element != nullptr &&
                    integer_element->get_bit_width() == 32) {
                    lowered = context_->create_type<CoreIrVectorType>(
                        element_type, type.array_element_count);
                    break;
                }
            }
            lowered = context_->create_type<CoreIrArrayType>(
                element_type, type.array_element_count);
            break;
        }
        case AArch64LlvmImportTypeKind::Struct: {
            if (type.element_types.size() == 1 &&
                type.element_types.front().kind == AArch64LlvmImportTypeKind::Integer &&
                type.element_types.front().integer_bit_width == 128) {
                lowered = lower_import_type(type.element_types.front());
                break;
            }
            std::vector<const CoreIrType *> element_types;
            for (const AArch64LlvmImportType &element : type.element_types) {
                const CoreIrType *element_type = lower_import_type(element);
                if (element_type == nullptr) {
                    return nullptr;
                }
                element_types.push_back(element_type);
            }
            lowered = context_->create_type<CoreIrStructType>(element_types,
                                                              type.struct_is_packed);
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
            if (float_type->get_float_kind() == CoreIrFloatKind::Float128) {
                llvm::APFloat quad_value(llvm::APFloat::IEEEquad());
                llvm::APInt ap_int(
                    static_cast<unsigned>(constant.cast_source_type.integer_bit_width),
                    source_value, constant.kind ==
                                      AArch64LlvmImportConstantKind::SignedIntToFloat);
                quad_value.convertFromAPInt(
                    ap_int,
                    constant.kind ==
                        AArch64LlvmImportConstantKind::SignedIntToFloat,
                    llvm::APFloat::rmNearestTiesToEven);
                const llvm::APInt bits = quad_value.bitcastToAPInt();
                return context_->create_constant<CoreIrConstantFloat>(
                    type, format_fp128_words_literal(
                              bits.extractBitsAsZExtValue(64, 0),
                              bits.extractBitsAsZExtValue(64, 64)));
            }
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
            if (float_operand != nullptr && float_type != nullptr &&
                float_type->get_float_kind() == CoreIrFloatKind::Float128) {
                llvm::APFloat source_value(llvm::APFloat::IEEEsingle());
                const auto *source_type =
                    dynamic_cast<const CoreIrFloatType *>(float_operand->get_type());
                if (source_type == nullptr) {
                    return nullptr;
                }
                switch (source_type->get_float_kind()) {
                case CoreIrFloatKind::Float16:
                    source_value = llvm::APFloat(llvm::APFloat::IEEEhalf());
                    break;
                case CoreIrFloatKind::Float32:
                    source_value = llvm::APFloat(llvm::APFloat::IEEEsingle());
                    break;
                case CoreIrFloatKind::Float64:
                    source_value = llvm::APFloat(llvm::APFloat::IEEEdouble());
                    break;
                case CoreIrFloatKind::Float128:
                    source_value = llvm::APFloat(llvm::APFloat::IEEEquad());
                    break;
                }
                auto status = source_value.convertFromString(
                    llvm::StringRef(float_operand->get_literal_text()),
                    llvm::APFloat::rmNearestTiesToEven);
                if (!status) {
                    llvm::consumeError(status.takeError());
                    return nullptr;
                }
                bool loses_info = false;
                source_value.convert(llvm::APFloat::IEEEquad(),
                                     llvm::APFloat::rmNearestTiesToEven,
                                     &loses_info);
                const llvm::APInt bits = source_value.bitcastToAPInt();
                return context_->create_constant<CoreIrConstantFloat>(
                    type, format_fp128_words_literal(
                              bits.extractBitsAsZExtValue(64, 0),
                              bits.extractBitsAsZExtValue(64, 64)));
            }
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
        const CoreIrType *element_type = nullptr;
        std::size_t element_count = 0;
        if (const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(vector_type);
            array_type != nullptr) {
            element_type = array_type->get_element_type();
            element_count = array_type->get_element_count();
        } else if (const auto *native_vector_type =
                       dynamic_cast<const CoreIrVectorType *>(vector_type);
                   native_vector_type != nullptr) {
            element_type = native_vector_type->get_element_type();
            element_count = native_vector_type->get_element_count();
        }
        if (element_type == nullptr || element_count == 0 || constant == nullptr) {
            return false;
        }
        lanes.clear();
        lanes.reserve(element_count);
        if (const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            aggregate != nullptr) {
            for (const CoreIrConstant *element : aggregate->get_elements()) {
                lanes.push_back(element);
            }
            while (lanes.size() < element_count) {
                lanes.push_back(make_zero_constant(element_type));
            }
            return lanes.size() == element_count;
        }
        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
            for (std::size_t index = 0; index < element_count; ++index) {
                lanes.push_back(make_zero_constant(element_type));
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
            if (type->get_kind() == CoreIrTypeKind::Vector) {
                const auto *vector_type = static_cast<const CoreIrVectorType *>(type);
                if (constant.elements.size() > vector_type->get_element_count()) {
                    return nullptr;
                }
                for (const AArch64LlvmImportConstant &element : constant.elements) {
                    const CoreIrConstant *lowered = lower_import_constant(
                        vector_type->get_element_type(), element);
                    if (lowered == nullptr) {
                        return nullptr;
                    }
                    elements.push_back(lowered);
                }
                return context_->create_constant<CoreIrConstantAggregate>(
                    type, elements);
            }
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

    struct BlockAddressDeltaEntry {
        std::string lhs_function_name;
        std::string lhs_block_name;
        std::string rhs_function_name;
        std::string rhs_block_name;
    };

    std::optional<BlockAddressDeltaEntry> parse_blockaddress_delta_entry(
        const std::string &text) const {
        auto parse_blockaddress_at =
            [&](const std::string &input, std::size_t &position)
            -> std::optional<std::pair<std::string, std::string>> {
            const std::string marker = "blockaddress(@";
            const std::size_t marker_pos = input.find(marker, position);
            if (marker_pos == std::string::npos) {
                return std::nullopt;
            }
            std::size_t cursor = marker_pos + marker.size();
            const std::size_t comma_pos = input.find(',', cursor);
            if (comma_pos == std::string::npos) {
                return std::nullopt;
            }
            std::string function_name =
                trim_copy(input.substr(cursor, comma_pos - cursor));
            cursor = comma_pos + 1;
            while (cursor < input.size() &&
                   std::isspace(static_cast<unsigned char>(input[cursor])) != 0) {
                ++cursor;
            }
            if (cursor >= input.size() || input[cursor] != '%') {
                return std::nullopt;
            }
            ++cursor;
            const std::size_t label_start = cursor;
            while (cursor < input.size() &&
                   llvm_import_is_identifier_char(input[cursor])) {
                ++cursor;
            }
            if (cursor >= input.size() || input[cursor] != ')' ||
                cursor == label_start) {
                return std::nullopt;
            }
            std::string block_name =
                input.substr(label_start, cursor - label_start);
            position = cursor + 1;
            return std::pair<std::string, std::string>{std::move(function_name),
                                                       std::move(block_name)};
        };

        BlockAddressDeltaEntry entry;
        std::size_t position = 0;
        const auto lhs = parse_blockaddress_at(text, position);
        const auto rhs = parse_blockaddress_at(text, position);
        if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
        }
        entry.lhs_function_name = lhs->first;
        entry.lhs_block_name = lhs->second;
        entry.rhs_function_name = rhs->first;
        entry.rhs_block_name = rhs->second;
        return entry;
    }

    std::optional<std::vector<BlockAddressDeltaEntry>>
    parse_blockaddress_delta_initializer(const AArch64LlvmImportGlobal &global) const {
        if (global.type.kind != AArch64LlvmImportTypeKind::Array ||
            global.type.element_types.size() != 1 ||
            global.type.element_types.front().kind !=
                AArch64LlvmImportTypeKind::Integer) {
            return std::nullopt;
        }
        const std::string text = trim_copy(global.initializer_text);
        if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
            return std::nullopt;
        }
        std::vector<BlockAddressDeltaEntry> entries;
        for (const std::string &entry_text :
             split_top_level(text.substr(1, text.size() - 2), ',')) {
            if (entry_text.empty()) {
                continue;
            }
            const auto entry = parse_blockaddress_delta_entry(entry_text);
            if (!entry.has_value()) {
                return std::nullopt;
            }
            entries.push_back(*entry);
        }
        return entries.empty()
                   ? std::nullopt
                   : std::optional<std::vector<BlockAddressDeltaEntry>>(
                         std::move(entries));
    }

    bool lower_blockaddress_delta_global(const AArch64LlvmImportGlobal &global,
                                         CoreIrGlobal *core_global) {
        const auto entries = parse_blockaddress_delta_initializer(global);
        if (!entries.has_value()) {
            return false;
        }
        const auto *array_type = dynamic_cast<const CoreIrArrayType *>(
            core_global->get_type());
        const auto *element_type =
            array_type == nullptr ? nullptr
                                  : as_integer_type(array_type->get_element_type());
        if (array_type == nullptr || element_type == nullptr ||
            element_type->get_bit_width() != 32 ||
            entries->size() != array_type->get_element_count()) {
            return false;
        }
        if (!global.is_internal_linkage) {
            module_asm_lines_.push_back(".globl " + global.name);
        } else {
            module_asm_lines_.push_back(".local " + global.name);
        }
        module_asm_lines_.push_back(".data");
        module_asm_lines_.push_back(".p2align 2");
        module_asm_lines_.push_back(global.name + ":");
        for (const BlockAddressDeltaEntry &entry : *entries) {
            module_asm_lines_.push_back(
                "  .word " +
                make_aarch64_function_block_label(entry.lhs_function_name,
                                                  entry.lhs_block_name) +
                " - " +
                make_aarch64_function_block_label(entry.rhs_function_name,
                                                  entry.rhs_block_name));
        }
        core_global->set_initializer(nullptr);
        core_global->set_is_internal_linkage(false);
        core_global->set_is_constant(global.is_constant);
        core_global->set_is_external_declaration(true);
        globals_[global.name] = core_global;
        return true;
    }

    bool lower_appending_ctor_dtor_global(const AArch64LlvmImportGlobal &global,
                                          CoreIrGlobal *core_global) {
        const bool is_ctor_table = global.name == "llvm.global_ctors";
        const bool is_dtor_table = global.name == "llvm.global_dtors";
        if ((!is_ctor_table && !is_dtor_table) || !global.initializer.is_valid() ||
            global.initializer.kind != AArch64LlvmImportConstantKind::Aggregate) {
            return false;
        }

        std::vector<std::string> function_names;
        for (const AArch64LlvmImportConstant &entry : global.initializer.elements) {
            if (entry.kind != AArch64LlvmImportConstantKind::Aggregate ||
                entry.elements.size() < 2 ||
                entry.elements[1].kind !=
                    AArch64LlvmImportConstantKind::SymbolReference) {
                return false;
            }
            function_names.push_back(entry.elements[1].symbol_name);
        }

        module_asm_lines_.push_back(std::string(".section ") +
                                    (is_ctor_table ? ".init_array"
                                                   : ".fini_array"));
        module_asm_lines_.push_back(".p2align 3");
        for (const std::string &function_name : function_names) {
            module_asm_lines_.push_back("  .xword " + function_name);
        }

        core_global->set_initializer(nullptr);
        core_global->set_is_constant(true);
        core_global->set_is_external_declaration(true);
        core_global->set_is_internal_linkage(false);
        globals_[global.name] = core_global;
        return true;
    }

    bool lower_global_definition(const AArch64LlvmImportGlobal &global) {
        CoreIrGlobal *core_global = module_->find_global(global.name);
        if (core_global == nullptr) {
            if (!declare_global_definition(global)) {
                return false;
            }
            core_global = module_->find_global(global.name);
        }
        if (core_global == nullptr) {
            return false;
        }

        if (global.is_external_declaration) {
            core_global->set_is_constant(global.is_constant);
            core_global->set_is_external_declaration(true);
            globals_[global.name] = core_global;
            return true;
        }

        if (lower_appending_ctor_dtor_global(global, core_global)) {
            return true;
        }

        if (!global.initializer.is_valid()) {
            if (lower_blockaddress_delta_global(global, core_global)) {
                return true;
            }
            add_error("unsupported LLVM global initializer: " +
                          global.initializer_text,
                      global.line, 1);
            return false;
        }
        const CoreIrConstant *initializer =
            lower_import_constant(core_global->get_type(), global.initializer);
        if (initializer == nullptr) {
            add_error("unsupported LLVM global initializer: " +
                          global.initializer_text,
                      global.line, 1);
            return false;
        }

        core_global->set_initializer(initializer);
        core_global->set_is_constant(global.is_constant);
        globals_[global.name] = core_global;
        return true;
    }

    bool declare_global_definition(const AArch64LlvmImportGlobal &global) {
        if (module_->find_global(global.name) != nullptr) {
            return true;
        }
        const CoreIrType *type = lower_import_type(global.type);
        if (type == nullptr) {
            add_error("unsupported LLVM global type: " + global.type_text,
                      global.line, 1);
            return false;
        }
        const CoreIrConstant *placeholder =
            global.is_external_declaration ? nullptr : make_zero_constant(type);
        if (!global.is_external_declaration && placeholder == nullptr) {
            add_error("unsupported LLVM global type: " + global.type_text,
                      global.line, 1);
            return false;
        }
        CoreIrGlobal *core_global = module_->create_global<CoreIrGlobal>(
            global.name, type, placeholder, global.is_internal_linkage,
            global.is_constant, global.is_external_declaration);
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
        if (function.is_extern_weak && !function.is_definition) {
            extern_weak_function_names_.insert(function.name);
            module_asm_lines_.push_back(".weak " + function.name);
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
        if (operand.kind == AArch64LlvmImportValueKind::ConstantExpressionRaw) {
            return operand.raw_constant_expression_text;
        }
        return "<unknown>";
    }

    std::optional<CoreIrBinaryOpcode> parse_binary_opcode_text(
        const std::string &opcode_text) const {
        if (opcode_text == "add") {
            return CoreIrBinaryOpcode::Add;
        }
        if (opcode_text == "sub") {
            return CoreIrBinaryOpcode::Sub;
        }
        if (opcode_text == "mul") {
            return CoreIrBinaryOpcode::Mul;
        }
        if (opcode_text == "sdiv") {
            return CoreIrBinaryOpcode::SDiv;
        }
        if (opcode_text == "udiv") {
            return CoreIrBinaryOpcode::UDiv;
        }
        if (opcode_text == "srem") {
            return CoreIrBinaryOpcode::SRem;
        }
        if (opcode_text == "urem") {
            return CoreIrBinaryOpcode::URem;
        }
        if (opcode_text == "and") {
            return CoreIrBinaryOpcode::And;
        }
        if (opcode_text == "or") {
            return CoreIrBinaryOpcode::Or;
        }
        if (opcode_text == "xor") {
            return CoreIrBinaryOpcode::Xor;
        }
        if (opcode_text == "shl") {
            return CoreIrBinaryOpcode::Shl;
        }
        if (opcode_text == "lshr") {
            return CoreIrBinaryOpcode::LShr;
        }
        if (opcode_text == "ashr") {
            return CoreIrBinaryOpcode::AShr;
        }
        return std::nullopt;
    }

    CoreIrValue *resolve_raw_constant_expression_value(
        const CoreIrType *type, const AArch64LlvmImportTypedValue &operand,
        CoreIrBasicBlock &block, std::unordered_map<std::string, ValueBinding> &bindings,
        std::size_t &synthetic_index, int line_number) {
        const std::string expression =
            trim_copy(operand.raw_constant_expression_text);
        std::size_t opcode_end = expression.find(' ');
        if (opcode_end == std::string::npos) {
            add_error("unsupported LLVM constant expression: " + expression,
                      line_number, 1);
            return nullptr;
        }
        const std::string opcode_text = expression.substr(0, opcode_end);
        const std::optional<CoreIrBinaryOpcode> opcode =
            parse_binary_opcode_text(opcode_text);
        if (!opcode.has_value()) {
            add_error("unsupported LLVM constant expression: " + expression,
                      line_number, 1);
            return nullptr;
        }
        std::string payload =
            strip_leading_modifiers(trim_copy(expression.substr(opcode_end + 1)));
        if (payload.size() < 2 || payload.front() != '(' || payload.back() != ')') {
            add_error("unsupported LLVM constant expression: " + expression,
                      line_number, 1);
            return nullptr;
        }
        const std::vector<std::string> operands = split_top_level(
            trim_copy(payload.substr(1, payload.size() - 2)), ',');
        if (operands.size() != 2) {
            add_error("unsupported LLVM constant expression: " + expression,
                      line_number, 1);
            return nullptr;
        }
        const std::optional<AArch64LlvmImportTypedValue> lhs_operand =
            parse_typed_value_text(operands[0]);
        const std::optional<AArch64LlvmImportTypedValue> rhs_operand =
            parse_typed_value_text(operands[1]);
        if (!lhs_operand.has_value() || !rhs_operand.has_value()) {
            add_error("unsupported LLVM constant expression: " + expression,
                      line_number, 1);
            return nullptr;
        }
        CoreIrValue *lhs = resolve_typed_value_operand(
            type, *lhs_operand, block, bindings, synthetic_index, line_number);
        CoreIrValue *rhs = resolve_typed_value_operand(
            type, *rhs_operand, block, bindings, synthetic_index, line_number);
        if (lhs == nullptr || rhs == nullptr) {
            return nullptr;
        }
        return block.create_instruction<CoreIrBinaryInst>(
            *opcode, type, "ll.constexpr.bin." + std::to_string(synthetic_index++),
            lhs, rhs);
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
            if (it->second.aggregate_address != nullptr) {
                ResolvedAddress resolved;
                resolved.address_value = it->second.aggregate_address;
                return resolved;
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
                const bool is_extern_weak =
                    extern_weak_function_names_.find(operand.global_name) !=
                    extern_weak_function_names_.end();
                ResolvedAddress resolved;
                resolved.address_value =
                    is_extern_weak
                        ? static_cast<CoreIrValue *>(
                              context_->create_constant<CoreIrConstantNull>(
                                  pointer_type))
                        : block.create_instruction<CoreIrAddressOfFunctionInst>(
                              pointer_type,
                              "ll.function.addr." +
                                  std::to_string(synthetic_index++),
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
        if (operand.kind == AArch64LlvmImportValueKind::ConstantExpressionRaw) {
            ResolvedAddress resolved;
            resolved.address_value = resolve_raw_constant_expression_value(
                lower_import_type(operand.type), operand, block, bindings,
                synthetic_index, line_number);
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
        if (operand.kind == AArch64LlvmImportValueKind::ConstantExpressionRaw) {
            return resolve_raw_constant_expression_value(
                type, operand, block, bindings, synthetic_index, line_number);
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
            if (it->second.reinterpreted_source_value != nullptr) {
                add_error("unsupported LLVM value operand: %" + operand.local_name,
                          line_number, 1);
                return nullptr;
            }
            if (it->second.stack_slot != nullptr) {
                if (type != nullptr &&
                    (type->get_kind() == CoreIrTypeKind::Array ||
                     type->get_kind() == CoreIrTypeKind::Struct)) {
                    return block.create_instruction<CoreIrLoadInst>(
                        type,
                        "ll.agg.load." + std::to_string(synthetic_index++),
                        it->second.stack_slot);
                }
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

    void prune_dead_constant_probe_chain(CoreIrBasicBlock &block,
                                         CoreIrValue *value) {
        auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
        if (instruction == nullptr || instruction->get_parent() != &block ||
            !instruction->get_uses().empty() || instruction->get_is_terminator() ||
            instruction->get_has_side_effect()) {
            return;
        }
        const std::vector<CoreIrValue *> operands = instruction->get_operands();
        auto &instructions = block.get_instructions();
        auto it = std::find_if(
            instructions.begin(), instructions.end(),
            [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
                return candidate.get() == instruction;
            });
        if (it == instructions.end()) {
            return;
        }
        (*it)->detach_operands();
        instructions.erase(it);
        for (CoreIrValue *operand : operands) {
            prune_dead_constant_probe_chain(block, operand);
        }
    }

    std::optional<AArch64LlvmImportTypedValue>
    parse_typed_value_text(const std::string &text) const {
        const std::string normalized = strip_leading_modifiers(trim_copy(text));
        std::size_t position = 0;
        const std::optional<std::string> type_text =
            consume_type_token(normalized, position);
        if (!type_text.has_value()) {
            return std::nullopt;
        }
        const auto parsed_type = parse_llvm_import_type_text(*type_text);
        if (!parsed_type.has_value()) {
            return std::nullopt;
        }
        AArch64LlvmImportTypedValue value;
        value.type_text = *type_text;
        value.type = *parsed_type;
        value.value_text =
            strip_leading_modifiers(trim_copy(normalized.substr(position)));
        if (value.value_text.empty()) {
            return std::nullopt;
        }
        if (value.value_text.front() == '%') {
            value.kind = AArch64LlvmImportValueKind::Local;
            value.local_name = value.value_text.substr(1);
            return value;
        }
        if (value.value_text.front() == '@') {
            value.kind = AArch64LlvmImportValueKind::Global;
            value.global_name = value.value_text.substr(1);
            return value;
        }
        if (const auto constant =
                parse_llvm_import_constant_text(value.type, value.value_text);
            constant.has_value()) {
            value.kind = AArch64LlvmImportValueKind::Constant;
            value.constant = *constant;
            return value;
        }
        return std::nullopt;
    }

    std::size_t parse_optional_alignment_text(const std::string &text) const {
        const std::string trimmed = trim_copy(text);
        if (!starts_with(trimmed, "align ")) {
            return 0;
        }
        try {
            return static_cast<std::size_t>(
                std::stoull(trim_copy(trimmed.substr(6))));
        } catch (...) {
            return 0;
        }
    }

    bool is_atomic_ordering_token(const std::string &token) const {
        static const std::unordered_set<std::string> orderings = {
            "unordered", "monotonic", "acquire", "release", "acq_rel",
            "seq_cst"};
        return orderings.find(token) != orderings.end();
    }

    std::string strip_trailing_atomic_order_tokens(const std::string &text,
                                                   std::size_t token_count) const {
        std::string trimmed = trim_copy(text);
        for (std::size_t index = 0; index < token_count; ++index) {
            const std::size_t last_space = trimmed.rfind(' ');
            if (last_space == std::string::npos) {
                return text;
            }
            const std::string token = trim_copy(trimmed.substr(last_space + 1));
            if (!is_atomic_ordering_token(token)) {
                return text;
            }
            trimmed = trim_copy(trimmed.substr(0, last_space));
        }
        return trimmed;
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
        const auto *base_pointer_type =
            dynamic_cast<const CoreIrPointerType *>(base_address->get_type());
        if (base_pointer_type != nullptr &&
            base_pointer_type->get_pointee_type() != aggregate_type) {
            return create_byte_offset_typed_address(
                block, base_address,
                static_cast<std::size_t>(index_value) *
                    get_type_size(array_type->get_element_type()),
                array_type->get_element_type(), synthetic_index);
        }
        std::vector<CoreIrValue *> indices;
        indices.push_back(get_i32_constant(0));
        indices.push_back(get_i32_constant(index_value));
        return block.create_instruction<CoreIrGetElementPtrInst>(
            pointer_to(array_type->get_element_type()),
            "ll.vec.gep." + std::to_string(synthetic_index++), base_address,
            indices);
    }

    const CoreIrType *aggregate_element_type(const CoreIrType *aggregate_type,
                                             std::uint64_t index_value) const {
        if (const auto *array_type = as_array_type(aggregate_type);
            array_type != nullptr) {
            if (index_value >= array_type->get_element_count()) {
                return nullptr;
            }
            return array_type->get_element_type();
        }
        if (const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(aggregate_type);
            struct_type != nullptr) {
            if (index_value >= struct_type->get_element_types().size()) {
                return nullptr;
            }
            return struct_type->get_element_types()[index_value];
        }
        return nullptr;
    }

    CoreIrValue *create_aggregate_subvalue_address(
        CoreIrBasicBlock &block, CoreIrValue *base_address,
        const CoreIrType *aggregate_type, std::uint64_t index_value,
        std::size_t &synthetic_index) {
        const CoreIrType *element_type =
            aggregate_element_type(aggregate_type, index_value);
        if (base_address == nullptr || element_type == nullptr) {
            return nullptr;
        }
        const auto *base_pointer_type =
            dynamic_cast<const CoreIrPointerType *>(base_address->get_type());
        if (base_pointer_type != nullptr &&
            base_pointer_type->get_pointee_type() != aggregate_type) {
            std::size_t byte_offset = 0;
            if (const auto *array_type = as_array_type(aggregate_type);
                array_type != nullptr) {
                byte_offset = static_cast<std::size_t>(index_value) *
                              get_type_size(array_type->get_element_type());
            } else if (const auto *struct_type =
                           dynamic_cast<const CoreIrStructType *>(aggregate_type);
                       struct_type != nullptr) {
                byte_offset =
                    get_struct_member_offset(struct_type,
                                             static_cast<std::size_t>(index_value));
            } else {
                return nullptr;
            }
            return create_byte_offset_typed_address(
                block, base_address, byte_offset, element_type, synthetic_index);
        }
        std::vector<CoreIrValue *> indices;
        indices.push_back(get_i32_constant(0));
        indices.push_back(get_i32_constant(index_value));
        return block.create_instruction<CoreIrGetElementPtrInst>(
            pointer_to(element_type),
            "ll.agg.gep." + std::to_string(synthetic_index++), base_address,
            indices);
    }

    const CoreIrType *pointer_pointee_type(const CoreIrValue *value) const {
        if (value == nullptr || value->get_type() == nullptr) {
            return nullptr;
        }
        const auto *pointer_type =
            dynamic_cast<const CoreIrPointerType *>(value->get_type());
        return pointer_type == nullptr ? nullptr : pointer_type->get_pointee_type();
    }

    std::size_t pointer_pointee_alignment(const CoreIrValue *value) const {
        const CoreIrType *pointee_type = pointer_pointee_type(value);
        return pointee_type == nullptr ? 1 : get_type_alignment(pointee_type);
    }

    const CoreIrType *integer_type_for_size(std::size_t size) {
        switch (size) {
        case 1:
            return parse_type_text("i8");
        case 2:
            return parse_type_text("i16");
        case 4:
            return parse_type_text("i32");
        case 8:
            return parse_type_text("i64");
        default:
            return nullptr;
        }
    }

    CoreIrValue *create_byte_offset_typed_address(
        CoreIrBasicBlock &block, CoreIrValue *base_address, std::size_t offset,
        const CoreIrType *pointee_type, std::size_t &synthetic_index) {
        const CoreIrType *i64_type = parse_type_text("i64");
        if (base_address == nullptr || pointee_type == nullptr || i64_type == nullptr) {
            return nullptr;
        }
        CoreIrValue *address_int = block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::PtrToInt, i64_type,
            "ll.byteaddr.ptrtoint." + std::to_string(synthetic_index++),
            base_address);
        if (offset != 0) {
            const CoreIrConstant *offset_constant =
                context_->create_constant<CoreIrConstantInt>(i64_type, offset);
            address_int = block.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Add, i64_type,
                "ll.byteaddr.add." + std::to_string(synthetic_index++),
                address_int, const_cast<CoreIrConstant *>(offset_constant));
        }
        return block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::IntToPtr, pointer_to(pointee_type),
            "ll.byteaddr.inttoptr." + std::to_string(synthetic_index++),
            address_int);
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
        if (is_i32x4_array_type(slot->get_allocated_type()) && elements.size() == 4 &&
            std::all_of(elements.begin(), elements.end(),
                        [&](const CoreIrConstant *element) {
                            const auto *int_constant =
                                dynamic_cast<const CoreIrConstantInt *>(element);
                            const auto *first_constant =
                                dynamic_cast<const CoreIrConstantInt *>(elements.front());
                            return int_constant != nullptr &&
                                   first_constant != nullptr &&
                                   int_constant->get_type() ==
                                       first_constant->get_type() &&
                                   int_constant->get_value() ==
                                       first_constant->get_value();
                        })) {
            const CoreIrType *ptr_type = parse_type_text("ptr");
            if (ptr_type == nullptr) {
                return false;
            }
            const CoreIrFunctionType *helper_type =
                context_->create_type<CoreIrFunctionType>(
                    void_type(),
                    std::vector<const CoreIrType *>{ptr_type,
                                                    elements.front()->get_type()},
                    false);
            CoreIrFunction *helper = nullptr;
            if (auto it = functions_.find(kInlineSplatScalarV4I32);
                it != functions_.end()) {
                helper = it->second;
            } else {
                helper = module_->create_function<CoreIrFunction>(
                    kInlineSplatScalarV4I32, helper_type, false, false);
                functions_[kInlineSplatScalarV4I32] = helper;
            }
            block.create_instruction<CoreIrCallInst>(
                void_type(), "", helper->get_name(), helper_type,
                std::vector<CoreIrValue *>{slot_address,
                                           const_cast<CoreIrConstant *>(
                                               elements.front())});
            return true;
        }
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

    bool materialize_constant_aggregate_to_address(
        const CoreIrConstantAggregate *aggregate, const CoreIrType *aggregate_type,
        CoreIrValue *destination_address, CoreIrBasicBlock &block,
        std::size_t &synthetic_index) {
        if (aggregate == nullptr || aggregate_type == nullptr ||
            destination_address == nullptr) {
            return false;
        }
        const auto *array_type = as_array_type(aggregate_type);
        if (array_type != nullptr) {
            const auto &elements = aggregate->get_elements();
            if (is_i32x4_array_type(aggregate_type) && elements.size() == 4 &&
                std::all_of(elements.begin(), elements.end(),
                            [&](const CoreIrConstant *element) {
                                const auto *int_constant =
                                    dynamic_cast<const CoreIrConstantInt *>(element);
                                const auto *first_constant =
                                    dynamic_cast<const CoreIrConstantInt *>(
                                        elements.front());
                                return int_constant != nullptr &&
                                       first_constant != nullptr &&
                                       int_constant->get_type() ==
                                           first_constant->get_type() &&
                                       int_constant->get_value() ==
                                           first_constant->get_value();
                            })) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return false;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(),
                        std::vector<const CoreIrType *>{ptr_type,
                                                        elements.front()->get_type()},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto it = functions_.find(kInlineSplatScalarV4I32);
                    it != functions_.end()) {
                    helper = it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineSplatScalarV4I32, helper_type, false, false);
                    functions_[kInlineSplatScalarV4I32] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{destination_address,
                                               const_cast<CoreIrConstant *>(
                                                   elements.front())});
                return true;
            }
            for (std::size_t index = 0; index < elements.size(); ++index) {
                CoreIrValue *element_address = create_element_address(
                    block, destination_address, aggregate_type,
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
        const auto *struct_type =
            dynamic_cast<const CoreIrStructType *>(aggregate_type);
        if (struct_type != nullptr) {
            const auto &elements = aggregate->get_elements();
            if (elements.size() != struct_type->get_element_types().size()) {
                return false;
            }
            for (std::size_t index = 0; index < elements.size(); ++index) {
                CoreIrValue *element_address = create_element_address(
                    block, destination_address, aggregate_type,
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
        return false;
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
        if (is_i32x4_array_type(type)) {
            const CoreIrType *ptr_type = parse_type_text("ptr");
            if (ptr_type == nullptr) {
                return false;
            }
            const CoreIrFunctionType *helper_type =
                context_->create_type<CoreIrFunctionType>(
                    void_type(), std::vector<const CoreIrType *>{ptr_type}, false);
            CoreIrFunction *helper = nullptr;
            if (auto it = functions_.find(kInlineZeroV4I32); it != functions_.end()) {
                helper = it->second;
            } else {
                helper = module_->create_function<CoreIrFunction>(
                    kInlineZeroV4I32, helper_type, false, false);
                functions_[kInlineZeroV4I32] = helper;
            }
            block.create_instruction<CoreIrCallInst>(
                void_type(), "", helper->get_name(), helper_type,
                std::vector<CoreIrValue *>{slot_address});
            return true;
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

    bool materialize_zero_initializer_to_address(const CoreIrType *type,
                                                 CoreIrValue *destination_address,
                                                 CoreIrBasicBlock &block,
                                                 std::size_t &synthetic_index) {
        if (type == nullptr || destination_address == nullptr) {
            return false;
        }
        const auto *array_type = as_array_type(type);
        if (array_type != nullptr) {
            if (is_i32x4_array_type(type)) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return false;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(), std::vector<const CoreIrType *>{ptr_type},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto it = functions_.find(kInlineZeroV4I32);
                    it != functions_.end()) {
                    helper = it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineZeroV4I32, helper_type, false, false);
                    functions_[kInlineZeroV4I32] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{destination_address});
                return true;
            }
            for (std::size_t index = 0; index < array_type->get_element_count();
                 ++index) {
                CoreIrValue *element_address = create_element_address(
                    block, destination_address, type,
                    static_cast<std::uint64_t>(index), synthetic_index);
                const CoreIrConstant *zero =
                    make_zero_constant(array_type->get_element_type());
                if (element_address == nullptr || zero == nullptr) {
                    return false;
                }
                block.create_instruction<CoreIrStoreInst>(
                    void_type(), const_cast<CoreIrConstant *>(zero),
                    element_address);
            }
            return true;
        }
        const auto *struct_type =
            dynamic_cast<const CoreIrStructType *>(type);
        if (struct_type != nullptr) {
            for (std::size_t index = 0;
                 index < struct_type->get_element_types().size(); ++index) {
                CoreIrValue *element_address = create_element_address(
                    block, destination_address, type,
                    static_cast<std::uint64_t>(index), synthetic_index);
                const CoreIrConstant *zero =
                    make_zero_constant(struct_type->get_element_types()[index]);
                if (element_address == nullptr || zero == nullptr) {
                    return false;
                }
                block.create_instruction<CoreIrStoreInst>(
                    void_type(), const_cast<CoreIrConstant *>(zero),
                    element_address);
            }
            return true;
        }
        return false;
    }

    bool try_materialize_special_aggregate_binding_to_address(
        const CoreIrType *aggregate_type, const AArch64LlvmImportTypedValue &operand,
        CoreIrBasicBlock &block,
        std::unordered_map<std::string, ValueBinding> &bindings,
        CoreIrValue *destination_address, int line_number) {
        if (destination_address == nullptr ||
            operand.kind != AArch64LlvmImportValueKind::Local) {
            return false;
        }
        const auto it = bindings.find(operand.local_name);
        if (it == bindings.end() || !is_i32x4_array_type(aggregate_type)) {
            return false;
        }
        const CoreIrType *ptr_type = parse_type_text("ptr");
        if (ptr_type == nullptr) {
            return false;
        }
        if (it->second.deferred_vector_pair_kind !=
                ValueBinding::DeferredVectorPairKind::None &&
            it->second.deferred_vector_pair_type == aggregate_type) {
            const char *helper_name = nullptr;
            std::vector<const CoreIrType *> helper_argument_types{ptr_type};
            std::vector<CoreIrValue *> helper_arguments{destination_address};
            if (it->second.deferred_vector_lhs_splat_scalar != nullptr &&
                it->second.deferred_vector_rhs_address != nullptr) {
                switch (it->second.deferred_vector_pair_kind) {
                case ValueBinding::DeferredVectorPairKind::Add:
                    helper_name = kInlineVecAddV4I32SplatLhs;
                    break;
                case ValueBinding::DeferredVectorPairKind::Mul:
                    helper_name = kInlineVecMulV4I32SplatLhs;
                    break;
                default:
                    break;
                }
                if (helper_name != nullptr) {
                    helper_argument_types.push_back(
                        it->second.deferred_vector_lhs_splat_scalar->get_type());
                    helper_argument_types.push_back(ptr_type);
                    helper_arguments.push_back(
                        it->second.deferred_vector_lhs_splat_scalar);
                    helper_arguments.push_back(
                        it->second.deferred_vector_rhs_address);
                }
            } else if (it->second.deferred_vector_rhs_splat_scalar != nullptr &&
                       it->second.deferred_vector_lhs_address != nullptr) {
                switch (it->second.deferred_vector_pair_kind) {
                case ValueBinding::DeferredVectorPairKind::Add:
                    helper_name = kInlineVecAddV4I32SplatRhs;
                    break;
                case ValueBinding::DeferredVectorPairKind::Mul:
                    helper_name = kInlineVecMulV4I32SplatRhs;
                    break;
                default:
                    break;
                }
                if (helper_name != nullptr) {
                    helper_argument_types.push_back(ptr_type);
                    helper_argument_types.push_back(
                        it->second.deferred_vector_rhs_splat_scalar->get_type());
                    helper_arguments.push_back(
                        it->second.deferred_vector_lhs_address);
                    helper_arguments.push_back(
                        it->second.deferred_vector_rhs_splat_scalar);
                }
            } else if (it->second.deferred_vector_lhs_address != nullptr &&
                       it->second.deferred_vector_rhs_address != nullptr) {
                switch (it->second.deferred_vector_pair_kind) {
                case ValueBinding::DeferredVectorPairKind::Add:
                    helper_name = kInlineVecAddV4I32;
                    break;
                case ValueBinding::DeferredVectorPairKind::Mul:
                    helper_name = kInlineVecMulV4I32;
                    break;
                case ValueBinding::DeferredVectorPairKind::SMin:
                    helper_name = kInlineVecSminV4I32;
                    break;
                case ValueBinding::DeferredVectorPairKind::SMax:
                    helper_name = kInlineVecSmaxV4I32;
                    break;
                case ValueBinding::DeferredVectorPairKind::None:
                    break;
                }
                if (helper_name != nullptr) {
                    helper_argument_types.push_back(ptr_type);
                    helper_argument_types.push_back(ptr_type);
                    helper_arguments.push_back(
                        it->second.deferred_vector_lhs_address);
                    helper_arguments.push_back(
                        it->second.deferred_vector_rhs_address);
                }
            }
            if (helper_name != nullptr) {
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(), helper_argument_types, false);
                CoreIrFunction *helper = nullptr;
                if (auto fn_it = functions_.find(helper_name);
                    fn_it != functions_.end()) {
                    helper = fn_it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        helper_name, helper_type, false, false);
                    functions_[helper_name] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type, helper_arguments);
                return true;
            }
        }
        if (it->second.lane0_splat_scalar != nullptr &&
            it->second.lane0_splat_vector_type == aggregate_type) {
            const CoreIrFunctionType *helper_type =
                context_->create_type<CoreIrFunctionType>(
                    void_type(),
                    std::vector<const CoreIrType *>{ptr_type,
                                                    it->second.lane0_splat_scalar
                                                        ->get_type()},
                    false);
            CoreIrFunction *helper = nullptr;
            if (auto fn_it = functions_.find(kInlineSplatScalarV4I32);
                fn_it != functions_.end()) {
                helper = fn_it->second;
            } else {
                helper = module_->create_function<CoreIrFunction>(
                    kInlineSplatScalarV4I32, helper_type, false, false);
                functions_[kInlineSplatScalarV4I32] = helper;
            }
            block.create_instruction<CoreIrCallInst>(
                void_type(), "", helper->get_name(), helper_type,
                std::vector<CoreIrValue *>{destination_address,
                                           it->second.lane0_splat_scalar});
            return true;
        }
        if (it->second.lane0_zero_insert_scalar != nullptr &&
            it->second.lane0_zero_insert_vector_type == aggregate_type) {
            const CoreIrFunctionType *helper_type =
                context_->create_type<CoreIrFunctionType>(
                    void_type(),
                    std::vector<const CoreIrType *>{ptr_type,
                                                    it->second
                                                        .lane0_zero_insert_scalar
                                                        ->get_type()},
                    false);
            CoreIrFunction *helper = nullptr;
            if (auto fn_it = functions_.find(kInlineInsertLane0ZeroedV4I32);
                fn_it != functions_.end()) {
                helper = fn_it->second;
            } else {
                helper = module_->create_function<CoreIrFunction>(
                    kInlineInsertLane0ZeroedV4I32, helper_type, false, false);
                functions_[kInlineInsertLane0ZeroedV4I32] = helper;
            }
            block.create_instruction<CoreIrCallInst>(
                void_type(), "", helper->get_name(), helper_type,
                std::vector<CoreIrValue *>{destination_address,
                                           it->second.lane0_zero_insert_scalar});
            return true;
        }
        (void)line_number;
        return false;
    }

    CoreIrValue *find_lane0_splat_scalar_binding(
        const CoreIrType *aggregate_type, const AArch64LlvmImportTypedValue &operand,
        const std::unordered_map<std::string, ValueBinding> &bindings) const {
        if (operand.kind != AArch64LlvmImportValueKind::Local ||
            !is_i32x4_array_type(aggregate_type)) {
            return nullptr;
        }
        const auto it = bindings.find(operand.local_name);
        if (it == bindings.end() ||
            it->second.lane0_splat_scalar == nullptr ||
            it->second.lane0_splat_vector_type != aggregate_type) {
            return nullptr;
        }
        return it->second.lane0_splat_scalar;
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
            if (it->second.aggregate_address != nullptr) {
                return it->second.aggregate_address;
            }
            if (it->second.stack_slot != nullptr) {
                return materialize_stack_slot_address(block, it->second.stack_slot,
                                                      synthetic_index);
            }
            if (it->second.deferred_vector_pair_kind !=
                    ValueBinding::DeferredVectorPairKind::None &&
                it->second.deferred_vector_pair_type == aggregate_type &&
                is_i32x4_array_type(aggregate_type)) {
                CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                    "ll.vec.pair." + operand.local_name, aggregate_type,
                    get_storage_alignment(aggregate_type));
                CoreIrValue *slot_address =
                    materialize_stack_slot_address(block, slot, synthetic_index);
                if (slot_address == nullptr ||
                    !try_materialize_special_aggregate_binding_to_address(
                        aggregate_type, operand, block, bindings, slot_address,
                        line_number)) {
                    return nullptr;
                }
                ValueBinding updated = it->second;
                updated.stack_slot = slot;
                bindings[operand.local_name] = updated;
                return slot_address;
            }
            if (it->second.lane0_splat_scalar != nullptr &&
                it->second.lane0_splat_vector_type == aggregate_type &&
                is_i32x4_array_type(aggregate_type)) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return nullptr;
                }
                CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                    "ll.splat." + operand.local_name, aggregate_type,
                    get_storage_alignment(aggregate_type));
                CoreIrValue *slot_address =
                    materialize_stack_slot_address(block, slot, synthetic_index);
                if (slot_address == nullptr) {
                    return nullptr;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(),
                        std::vector<const CoreIrType *>{ptr_type,
                                                        it->second.lane0_splat_scalar
                                                            ->get_type()},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto fn_it = functions_.find(kInlineSplatScalarV4I32);
                    fn_it != functions_.end()) {
                    helper = fn_it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineSplatScalarV4I32, helper_type, false, false);
                    functions_[kInlineSplatScalarV4I32] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{slot_address,
                                               it->second.lane0_splat_scalar});
                ValueBinding updated = it->second;
                updated.stack_slot = slot;
                bindings[operand.local_name] = updated;
                return slot_address;
            }
            if (it->second.lane0_zero_insert_scalar != nullptr &&
                it->second.lane0_zero_insert_vector_type == aggregate_type &&
                is_i32x4_array_type(aggregate_type)) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return nullptr;
                }
                CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                    "ll.insert0." + operand.local_name, aggregate_type,
                    get_storage_alignment(aggregate_type));
                CoreIrValue *slot_address =
                    materialize_stack_slot_address(block, slot, synthetic_index);
                if (slot_address == nullptr) {
                    return nullptr;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(),
                        std::vector<const CoreIrType *>{ptr_type,
                                                        it->second
                                                            .lane0_zero_insert_scalar
                                                            ->get_type()},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto fn_it = functions_.find(kInlineInsertLane0ZeroedV4I32);
                    fn_it != functions_.end()) {
                    helper = fn_it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineInsertLane0ZeroedV4I32, helper_type, false, false);
                    functions_[kInlineInsertLane0ZeroedV4I32] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{slot_address,
                                               it->second.lane0_zero_insert_scalar});
                ValueBinding updated = it->second;
                updated.stack_slot = slot;
                bindings[operand.local_name] = updated;
                return slot_address;
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
        if (!is_aggregate_type(aggregate_type) || destination_address == nullptr ||
            source_address == nullptr) {
            return false;
        }
        if (get_type_size(aggregate_type) == 16) {
            const CoreIrType *ptr_type = parse_type_text("ptr");
            if (ptr_type == nullptr) {
                return false;
            }
            const CoreIrFunctionType *helper_type =
                context_->create_type<CoreIrFunctionType>(
                    void_type(), std::vector<const CoreIrType *>{ptr_type, ptr_type},
                    false);
            CoreIrFunction *helper = nullptr;
            if (auto it = functions_.find(kInlineCopyV4I32); it != functions_.end()) {
                helper = it->second;
            } else {
                helper = module_->create_function<CoreIrFunction>(
                    kInlineCopyV4I32, helper_type, false, false);
                functions_[kInlineCopyV4I32] = helper;
            }
            block.create_instruction<CoreIrCallInst>(
                void_type(), "", helper->get_name(), helper_type,
                std::vector<CoreIrValue *>{destination_address, source_address});
            return true;
        }
        const std::size_t total_size = get_type_size(aggregate_type);
        const std::size_t max_chunk_alignment =
            std::max<std::size_t>(1, std::min(pointer_pointee_alignment(source_address),
                                              pointer_pointee_alignment(
                                                  destination_address)));
        for (std::size_t offset = 0; offset < total_size;) {
            std::size_t chunk_size = 1;
            for (const std::size_t candidate : {std::size_t{8}, std::size_t{4},
                                                std::size_t{2}, std::size_t{1}}) {
                if (offset + candidate <= total_size && offset % candidate == 0 &&
                    max_chunk_alignment >= candidate) {
                    chunk_size = candidate;
                    break;
                }
            }
            const CoreIrType *chunk_type = integer_type_for_size(chunk_size);
            CoreIrValue *source_chunk = create_byte_offset_typed_address(
                block, source_address, offset, chunk_type, synthetic_index);
            CoreIrValue *destination_chunk = create_byte_offset_typed_address(
                block, destination_address, offset, chunk_type, synthetic_index);
            if (chunk_type == nullptr || source_chunk == nullptr ||
                destination_chunk == nullptr) {
                return false;
            }
            CoreIrValue *loaded = block.create_instruction<CoreIrLoadInst>(
                chunk_type, "ll.memcpy.load." + std::to_string(synthetic_index++),
                source_chunk);
            block.create_instruction<CoreIrStoreInst>(void_type(), loaded,
                                                      destination_chunk);
            offset += chunk_size;
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

    bool bind_reinterpreted_result(
        const std::string &name, CoreIrValue *source_value,
        const CoreIrType *source_type, const CoreIrType *target_type,
        std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.reinterpreted_source_value = source_value;
        binding.reinterpreted_source_type = source_type;
        binding.reinterpreted_target_type = target_type;
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

    bool bind_aggregate_address_result(
        const std::string &name, CoreIrValue *aggregate_address,
        std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.aggregate_address = aggregate_address;
        bindings[name] = binding;
        return true;
    }

    bool bind_wide_integer_result(
        const std::string &name, const ValueBinding::WideIntegerValue &wide_integer,
        std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.wide_integer = wide_integer;
        bindings[name] = binding;
        return true;
    }

    bool bind_wide_overflow_aggregate_result(
        const std::string &name,
        const ValueBinding::WideOverflowAggregate &wide_overflow_aggregate,
        std::unordered_map<std::string, ValueBinding> &bindings) {
        if (name.empty()) {
            return true;
        }
        ValueBinding binding;
        binding.wide_overflow_aggregate = wide_overflow_aggregate;
        bindings[name] = binding;
        return true;
    }

    std::optional<ValueBinding::WideIntegerValue> lookup_wide_integer_value(
        const AArch64LlvmImportTypedValue &operand,
        const std::unordered_map<std::string, ValueBinding> &bindings) const {
        if (operand.kind != AArch64LlvmImportValueKind::Local) {
            return std::nullopt;
        }
        const auto it = bindings.find(operand.local_name);
        if (it == bindings.end() || !it->second.wide_integer.has_value()) {
            return std::nullopt;
        }
        return it->second.wide_integer;
    }

    std::optional<ValueBinding::WideIntegerValue> resolve_wide_integer_value(
        const AArch64LlvmImportTypedValue &operand, CoreIrBasicBlock &block,
        std::unordered_map<std::string, ValueBinding> &bindings,
        std::size_t &synthetic_index) {
        if (operand.kind != AArch64LlvmImportValueKind::Local ||
            operand.type.kind != AArch64LlvmImportTypeKind::Integer ||
            operand.type.integer_bit_width <= 64 ||
            operand.type.integer_bit_width == 128) {
            return std::nullopt;
        }
        const auto binding_it = bindings.find(operand.local_name);
        if (binding_it == bindings.end()) {
            return std::nullopt;
        }
        if (binding_it->second.wide_integer.has_value()) {
            return binding_it->second.wide_integer;
        }
        if (binding_it->second.stack_slot == nullptr) {
            return std::nullopt;
        }
        ValueBinding::WideIntegerValue wide_integer;
        wide_integer.source_address = materialize_stack_slot_address(
            block, binding_it->second.stack_slot, synthetic_index);
        wide_integer.source_bit_width = operand.type.integer_bit_width;
        return wide_integer.source_address == nullptr
                   ? std::nullopt
                   : std::optional<ValueBinding::WideIntegerValue>(wide_integer);
    }

    CoreIrValue *materialize_scalar_value_to_address(
        CoreIrFunction &function, CoreIrBasicBlock &block, const CoreIrType *type,
        CoreIrValue *value, std::size_t &synthetic_index) {
        if (type == nullptr || value == nullptr) {
            return nullptr;
        }
        CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
            "ll.wide.scalar." + std::to_string(synthetic_index++), type,
            get_storage_alignment(type));
        CoreIrValue *slot_address =
            materialize_stack_slot_address(block, slot, synthetic_index);
        if (slot_address == nullptr) {
            return nullptr;
        }
        block.create_instruction<CoreIrStoreInst>(void_type(), value, slot);
        return slot_address;
    }

    CoreIrFunction *get_or_create_is_fpclass_helper(CoreIrFloatKind float_kind) {
        std::string helper_name;
        const CoreIrType *value_type = nullptr;
        switch (float_kind) {
        case CoreIrFloatKind::Float32:
            helper_name = "__sysycc_is_fpclass_f32";
            value_type = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float32);
            break;
        case CoreIrFloatKind::Float64:
            helper_name = "__sysycc_is_fpclass_f64";
            value_type = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float64);
            break;
        case CoreIrFloatKind::Float128:
            helper_name = "__sysycc_is_fpclass_f128";
            value_type = context_->create_type<CoreIrFloatType>(
                CoreIrFloatKind::Float128);
            break;
        case CoreIrFloatKind::Float16:
        default:
            return nullptr;
        }

        const CoreIrType *i1_type = parse_type_text("i1");
        const CoreIrType *i32_type = parse_type_text("i32");
        if (value_type == nullptr || i1_type == nullptr || i32_type == nullptr) {
            return nullptr;
        }

        const CoreIrFunctionType *helper_type =
            context_->create_type<CoreIrFunctionType>(
                i1_type, std::vector<const CoreIrType *>{value_type, i32_type},
                false);
        CoreIrFunction *helper = module_->find_function(helper_name);
        if (helper == nullptr) {
            helper = module_->create_function<CoreIrFunction>(helper_name,
                                                              helper_type, true,
                                                              false);
        } else {
            helper->set_function_type(helper_type);
            helper->set_is_internal_linkage(true);
        }
        functions_[helper_name] = helper;
        if (!helper->get_basic_blocks().empty()) {
            return helper;
        }

        if (helper->get_parameters().empty()) {
            helper->create_parameter<CoreIrParameter>(value_type, "value");
            helper->create_parameter<CoreIrParameter>(i32_type, "mask");
        }
        if (helper->get_parameters().size() != 2) {
            return nullptr;
        }

        CoreIrValue *value_param = helper->get_parameters()[0].get();
        CoreIrValue *mask_param = helper->get_parameters()[1].get();
        CoreIrBasicBlock &entry =
            *helper->create_basic_block<CoreIrBasicBlock>("entry");
        std::size_t synthetic_index = 0;

        auto as_mutable_constant = [&](const CoreIrConstant *constant)
            -> CoreIrValue * {
            return constant == nullptr ? nullptr
                                       : static_cast<CoreIrValue *>(
                                             const_cast<CoreIrConstant *>(
                                                 constant));
        };
        auto integer_constant = [&](const CoreIrType *type,
                                    std::uint64_t bits) -> CoreIrValue * {
            return as_mutable_constant(
                context_->create_constant<CoreIrConstantInt>(type, bits));
        };
        auto bool_constant = [&](bool value) -> CoreIrValue * {
            return integer_constant(i1_type, value ? 1 : 0);
        };
        auto bool_not = [&](CoreIrValue *value,
                            const std::string &tag) -> CoreIrValue * {
            return entry.create_instruction<CoreIrCompareInst>(
                CoreIrComparePredicate::Equal, i1_type,
                tag + std::to_string(synthetic_index++), value, bool_constant(false));
        };
        auto bool_and = [&](CoreIrValue *lhs, CoreIrValue *rhs,
                            const std::string &tag) -> CoreIrValue * {
            return entry.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, i1_type,
                tag + std::to_string(synthetic_index++), lhs, rhs);
        };
        auto bool_or = [&](CoreIrValue *lhs, CoreIrValue *rhs,
                           const std::string &tag) -> CoreIrValue * {
            return entry.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Or, i1_type,
                tag + std::to_string(synthetic_index++), lhs, rhs);
        };
        auto make_integer_compare =
            [&](CoreIrComparePredicate predicate, const CoreIrType *type,
                CoreIrValue *lhs, std::uint64_t rhs_bits,
                const std::string &tag) -> CoreIrValue * {
                return entry.create_instruction<CoreIrCompareInst>(
                    predicate, i1_type,
                    tag + std::to_string(synthetic_index++), lhs,
                    integer_constant(type, rhs_bits));
            };
        auto make_integer_mask =
            [&](const CoreIrType *type, CoreIrValue *lhs, std::uint64_t rhs_bits,
                const std::string &tag) -> CoreIrValue * {
                return entry.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::And, type,
                    tag + std::to_string(synthetic_index++), lhs,
                    integer_constant(type, rhs_bits));
            };
        auto mask_selected = [&](std::uint64_t class_mask,
                                 const std::string &tag) -> CoreIrValue * {
            CoreIrValue *masked = entry.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, i32_type,
                tag + ".mask." + std::to_string(synthetic_index++), mask_param,
                integer_constant(i32_type, class_mask));
            return entry.create_instruction<CoreIrCompareInst>(
                CoreIrComparePredicate::NotEqual, i1_type,
                tag + ".sel." + std::to_string(synthetic_index++), masked,
                integer_constant(i32_type, 0));
        };

        CoreIrValue *storage_address = materialize_scalar_value_to_address(
            *helper, entry, value_type, value_param, synthetic_index);
        if (storage_address == nullptr) {
            return nullptr;
        }

        CoreIrValue *sign_flag = nullptr;
        CoreIrValue *is_snan = nullptr;
        CoreIrValue *is_qnan = nullptr;
        CoreIrValue *is_inf = nullptr;
        CoreIrValue *is_normal = nullptr;
        CoreIrValue *is_subnormal = nullptr;
        CoreIrValue *is_zero = nullptr;

        if (float_kind == CoreIrFloatKind::Float32 ||
            float_kind == CoreIrFloatKind::Float64) {
            const std::size_t bit_width =
                float_kind == CoreIrFloatKind::Float32 ? 32 : 64;
            const CoreIrType *int_type =
                parse_type_text(bit_width == 32 ? "i32" : "i64");
            if (int_type == nullptr) {
                return nullptr;
            }
            CoreIrValue *bits = entry.create_instruction<CoreIrLoadInst>(
                int_type, "ll.isfpclass.bits." + std::to_string(synthetic_index++),
                storage_address);
            const std::uint64_t sign_mask =
                bit_width == 32 ? 0x80000000ULL : 0x8000000000000000ULL;
            const std::uint64_t exp_mask =
                bit_width == 32 ? 0x7f800000ULL : 0x7ff0000000000000ULL;
            const std::uint64_t mantissa_mask =
                bit_width == 32 ? 0x007fffffULL : 0x000fffffffffffffULL;
            const std::uint64_t quiet_mask =
                bit_width == 32 ? 0x00400000ULL : 0x0008000000000000ULL;

            CoreIrValue *sign_bits =
                make_integer_mask(int_type, bits, sign_mask, "ll.isfpclass.sign.");
            CoreIrValue *exp_bits =
                make_integer_mask(int_type, bits, exp_mask, "ll.isfpclass.exp.");
            CoreIrValue *mantissa_bits = make_integer_mask(
                int_type, bits, mantissa_mask, "ll.isfpclass.mant.");
            CoreIrValue *quiet_bits = make_integer_mask(
                int_type, mantissa_bits, quiet_mask, "ll.isfpclass.quiet.");

            sign_flag = make_integer_compare(CoreIrComparePredicate::NotEqual,
                                             int_type, sign_bits, 0,
                                             "ll.isfpclass.sign.ne.");
            CoreIrValue *exp_all_ones = make_integer_compare(
                CoreIrComparePredicate::Equal, int_type, exp_bits, exp_mask,
                "ll.isfpclass.exp.ones.");
            CoreIrValue *exp_zero = make_integer_compare(
                CoreIrComparePredicate::Equal, int_type, exp_bits, 0,
                "ll.isfpclass.exp.zero.");
            CoreIrValue *mantissa_zero = make_integer_compare(
                CoreIrComparePredicate::Equal, int_type, mantissa_bits, 0,
                "ll.isfpclass.mant.zero.");
            CoreIrValue *mantissa_nonzero =
                bool_not(mantissa_zero, "ll.isfpclass.mant.nz.");
            CoreIrValue *quiet_nonzero = make_integer_compare(
                CoreIrComparePredicate::NotEqual, int_type, quiet_bits, 0,
                "ll.isfpclass.quiet.nz.");

            CoreIrValue *is_nan = bool_and(exp_all_ones, mantissa_nonzero,
                                           "ll.isfpclass.nan.");
            is_qnan = bool_and(is_nan, quiet_nonzero, "ll.isfpclass.qnan.");
            is_snan = bool_and(
                is_nan, bool_not(quiet_nonzero, "ll.isfpclass.quiet.not."),
                "ll.isfpclass.snan.");
            is_inf = bool_and(exp_all_ones, mantissa_zero, "ll.isfpclass.inf.");
            is_zero = bool_and(exp_zero, mantissa_zero, "ll.isfpclass.zero.");
            is_subnormal =
                bool_and(exp_zero, mantissa_nonzero, "ll.isfpclass.sub.");
            is_normal = bool_not(bool_or(exp_zero, exp_all_ones,
                                         "ll.isfpclass.special."),
                                 "ll.isfpclass.normal.not.");
        } else {
            const CoreIrType *i64_type = parse_type_text("i64");
            if (i64_type == nullptr) {
                return nullptr;
            }
            CoreIrValue *low_address = create_byte_offset_typed_address(
                entry, storage_address, 0, i64_type, synthetic_index);
            CoreIrValue *high_address = create_byte_offset_typed_address(
                entry, storage_address, 8, i64_type, synthetic_index);
            if (low_address == nullptr || high_address == nullptr) {
                return nullptr;
            }
            CoreIrValue *low_bits = entry.create_instruction<CoreIrLoadInst>(
                i64_type, "ll.isfpclass.low." + std::to_string(synthetic_index++),
                low_address);
            CoreIrValue *high_bits = entry.create_instruction<CoreIrLoadInst>(
                i64_type,
                "ll.isfpclass.high." + std::to_string(synthetic_index++),
                high_address);
            CoreIrValue *sign_bits = make_integer_mask(
                i64_type, high_bits, 0x8000000000000000ULL,
                "ll.isfpclass.sign.");
            CoreIrValue *exp_bits = make_integer_mask(
                i64_type, high_bits, 0x7fff000000000000ULL,
                "ll.isfpclass.exp.");
            CoreIrValue *mantissa_hi = make_integer_mask(
                i64_type, high_bits, 0x0000ffffffffffffULL,
                "ll.isfpclass.mhi.");
            CoreIrValue *quiet_bits = make_integer_mask(
                i64_type, high_bits, 0x0000800000000000ULL,
                "ll.isfpclass.quiet.");

            sign_flag = make_integer_compare(CoreIrComparePredicate::NotEqual,
                                             i64_type, sign_bits, 0,
                                             "ll.isfpclass.sign.ne.");
            CoreIrValue *exp_all_ones = make_integer_compare(
                CoreIrComparePredicate::Equal, i64_type, exp_bits,
                0x7fff000000000000ULL, "ll.isfpclass.exp.ones.");
            CoreIrValue *exp_zero = make_integer_compare(
                CoreIrComparePredicate::Equal, i64_type, exp_bits, 0,
                "ll.isfpclass.exp.zero.");
            CoreIrValue *mantissa_hi_zero = make_integer_compare(
                CoreIrComparePredicate::Equal, i64_type, mantissa_hi, 0,
                "ll.isfpclass.mhi.zero.");
            CoreIrValue *mantissa_lo_zero = make_integer_compare(
                CoreIrComparePredicate::Equal, i64_type, low_bits, 0,
                "ll.isfpclass.mlo.zero.");
            CoreIrValue *mantissa_zero = bool_and(
                mantissa_hi_zero, mantissa_lo_zero, "ll.isfpclass.mant.zero.");
            CoreIrValue *mantissa_nonzero =
                bool_not(mantissa_zero, "ll.isfpclass.mant.nz.");
            CoreIrValue *quiet_nonzero = make_integer_compare(
                CoreIrComparePredicate::NotEqual, i64_type, quiet_bits, 0,
                "ll.isfpclass.quiet.nz.");

            CoreIrValue *is_nan = bool_and(exp_all_ones, mantissa_nonzero,
                                           "ll.isfpclass.nan.");
            is_qnan = bool_and(is_nan, quiet_nonzero, "ll.isfpclass.qnan.");
            is_snan = bool_and(
                is_nan, bool_not(quiet_nonzero, "ll.isfpclass.quiet.not."),
                "ll.isfpclass.snan.");
            is_inf = bool_and(exp_all_ones, mantissa_zero, "ll.isfpclass.inf.");
            is_zero = bool_and(exp_zero, mantissa_zero, "ll.isfpclass.zero.");
            is_subnormal =
                bool_and(exp_zero, mantissa_nonzero, "ll.isfpclass.sub.");
            is_normal = bool_not(bool_or(exp_zero, exp_all_ones,
                                         "ll.isfpclass.special."),
                                 "ll.isfpclass.normal.not.");
        }

        CoreIrValue *result_value = bool_constant(false);
        auto accumulate_class = [&](std::uint64_t class_mask, CoreIrValue *predicate,
                                    const std::string &tag) {
            CoreIrValue *selected = mask_selected(class_mask, tag);
            CoreIrValue *contribution =
                bool_and(selected, predicate, tag + ".contrib.");
            result_value = bool_or(result_value, contribution, tag + ".acc.");
        };

        CoreIrValue *not_sign =
            bool_not(sign_flag, "ll.isfpclass.sign.not.");
        accumulate_class(0x0001, is_snan, "ll.isfpclass.snan.");
        accumulate_class(0x0002, is_qnan, "ll.isfpclass.qnan.");
        accumulate_class(0x0004,
                         bool_and(sign_flag, is_inf, "ll.isfpclass.neginf."),
                         "ll.isfpclass.acc.neginf.");
        accumulate_class(
            0x0008, bool_and(sign_flag, is_normal, "ll.isfpclass.negnorm."),
            "ll.isfpclass.acc.negnorm.");
        accumulate_class(
            0x0010,
            bool_and(sign_flag, is_subnormal, "ll.isfpclass.negsub."),
            "ll.isfpclass.acc.negsub.");
        accumulate_class(0x0020,
                         bool_and(sign_flag, is_zero, "ll.isfpclass.negzero."),
                         "ll.isfpclass.acc.negzero.");
        accumulate_class(0x0040,
                         bool_and(not_sign, is_zero, "ll.isfpclass.poszero."),
                         "ll.isfpclass.acc.poszero.");
        accumulate_class(
            0x0080, bool_and(not_sign, is_subnormal, "ll.isfpclass.possub."),
            "ll.isfpclass.acc.possub.");
        accumulate_class(
            0x0100, bool_and(not_sign, is_normal, "ll.isfpclass.posnorm."),
            "ll.isfpclass.acc.posnorm.");
        accumulate_class(0x0200,
                         bool_and(not_sign, is_inf, "ll.isfpclass.posinf."),
                         "ll.isfpclass.acc.posinf.");

        entry.create_instruction<CoreIrReturnInst>(void_type(), result_value);
        return helper;
    }

    std::optional<std::uint64_t> parse_import_u64_constant(
        const AArch64LlvmImportTypedValue &operand) const {
        if (operand.kind != AArch64LlvmImportValueKind::Constant) {
            return std::nullopt;
        }
        if (operand.constant.kind == AArch64LlvmImportConstantKind::Integer) {
            return operand.constant.integer_value;
        }
        if (operand.constant.kind == AArch64LlvmImportConstantKind::Aggregate &&
            operand.constant.elements.size() == 2 &&
            operand.constant.elements[0].kind ==
                AArch64LlvmImportConstantKind::Integer &&
            operand.constant.elements[1].kind ==
                AArch64LlvmImportConstantKind::Integer &&
            operand.constant.elements[1].integer_value == 0) {
            return operand.constant.elements[0].integer_value;
        }
        return std::nullopt;
    }

    std::optional<unsigned __int128> parse_import_u128_literal(
        const std::string &text) const {
        std::string trimmed = trim_copy(text);
        if (trimmed.empty()) {
            return std::nullopt;
        }
        bool negative = false;
        if (trimmed.front() == '+' || trimmed.front() == '-') {
            negative = trimmed.front() == '-';
            trimmed.erase(trimmed.begin());
        }
        if (trimmed.empty()) {
            return std::nullopt;
        }
        int base = 10;
        if (starts_with(trimmed, "0x") || starts_with(trimmed, "0X")) {
            base = 16;
            trimmed = trimmed.substr(2);
        }
        if (trimmed.empty()) {
            return std::nullopt;
        }
        unsigned __int128 value = 0;
        for (char ch : trimmed) {
            unsigned digit = 0;
            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                digit = static_cast<unsigned>(ch - '0');
            } else if (base == 16 &&
                       std::isxdigit(static_cast<unsigned char>(ch)) != 0) {
                digit = static_cast<unsigned>(std::tolower(
                            static_cast<unsigned char>(ch)) -
                        'a' + 10);
            } else {
                return std::nullopt;
            }
            if (digit >= static_cast<unsigned>(base)) {
                return std::nullopt;
            }
            value = value * static_cast<unsigned>(base) + digit;
        }
        return negative ? static_cast<unsigned __int128>(0) - value : value;
    }

    std::optional<ValueBinding::WideIntegerValue::ConstantWords>
    parse_import_constant_words(const AArch64LlvmImportTypedValue &operand,
                               std::size_t bit_width) const {
        if (operand.kind != AArch64LlvmImportValueKind::Constant) {
            return std::nullopt;
        }
        const auto parsed = parse_import_u128_literal(operand.value_text);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        unsigned __int128 bits = *parsed;
        if (bit_width < 128) {
            const unsigned __int128 mask =
                bit_width == 0 ? 0
                               : ((static_cast<unsigned __int128>(1) << bit_width) -
                                  1);
            bits &= mask;
        }
        ValueBinding::WideIntegerValue::ConstantWords words;
        words.low = static_cast<std::uint64_t>(bits);
        words.high = static_cast<std::uint64_t>(bits >> 64);
        return words;
    }

    bool constant_words_bit_is_one(
        const ValueBinding::WideIntegerValue::ConstantWords &words,
        std::size_t bit_index) const {
        if (bit_index < 64) {
            return ((words.low >> bit_index) & 1ULL) != 0;
        }
        if (bit_index < 128) {
            return ((words.high >> (bit_index - 64)) & 1ULL) != 0;
        }
        return false;
    }

    ValueBinding::WideIntegerValue::ConstantWords shift_constant_words(
        ValueBinding::WideIntegerValue::ConstantWords words,
        ValueBinding::WideIntegerValue::ShiftKind kind, std::size_t amount,
        std::size_t bit_width) const {
        unsigned __int128 combined =
            (static_cast<unsigned __int128>(words.high) << 64U) |
            static_cast<unsigned __int128>(words.low);
        if (amount >= 128) {
            combined = 0;
        } else if (kind == ValueBinding::WideIntegerValue::ShiftKind::Shl) {
            combined <<= amount;
        } else {
            combined >>= amount;
        }
        if (bit_width < 128) {
            const unsigned __int128 mask =
                bit_width == 0
                    ? 0
                    : ((static_cast<unsigned __int128>(1) << bit_width) - 1);
            combined &= mask;
        }
        ValueBinding::WideIntegerValue::ConstantWords shifted;
        shifted.low = static_cast<std::uint64_t>(combined);
        shifted.high = static_cast<std::uint64_t>(combined >> 64U);
        return shifted;
    }

    struct WideIntegerBitSource {
        enum class Kind {
            Zero,
            One,
            Source,
            InvertedSource,
        };

        Kind kind = Kind::Zero;
        std::size_t source_bit_index = 0;
    };

    std::optional<WideIntegerBitSource> evaluate_wide_integer_bit_source(
        const ValueBinding::WideIntegerValue &wide_integer,
        std::size_t result_bit_index) const {
        WideIntegerBitSource source;
        const std::optional<std::size_t> traced_source_bit =
            trace_wide_integer_source_bit(wide_integer, result_bit_index);
        if (traced_source_bit.has_value()) {
            source.kind = WideIntegerBitSource::Kind::Source;
            source.source_bit_index = *traced_source_bit;
        } else if (wide_integer.shift_ops.empty() &&
                   result_bit_index >= wide_integer.source_bit_width &&
                   wide_integer.source_bit_width != 0 &&
                   wide_integer.high_bit_fill ==
                       ValueBinding::WideIntegerValue::HighBitFill::Sign) {
            source.kind = WideIntegerBitSource::Kind::Source;
            source.source_bit_index = wide_integer.source_bit_width - 1;
        }
        for (const auto &op : wide_integer.bitwise_constant_ops) {
            const bool constant_bit =
                constant_words_bit_is_one(op.words, result_bit_index);
            switch (op.kind) {
            case ValueBinding::WideIntegerValue::BitwiseKind::And:
                if (!constant_bit) {
                    source.kind = WideIntegerBitSource::Kind::Zero;
                    source.source_bit_index = 0;
                }
                break;
            case ValueBinding::WideIntegerValue::BitwiseKind::Or:
                if (constant_bit) {
                    source.kind = WideIntegerBitSource::Kind::One;
                    source.source_bit_index = 0;
                }
                break;
            case ValueBinding::WideIntegerValue::BitwiseKind::Xor:
                if (!constant_bit) {
                    break;
                }
                switch (source.kind) {
                case WideIntegerBitSource::Kind::Zero:
                    source.kind = WideIntegerBitSource::Kind::One;
                    break;
                case WideIntegerBitSource::Kind::One:
                    source.kind = WideIntegerBitSource::Kind::Zero;
                    break;
                case WideIntegerBitSource::Kind::Source:
                    source.kind = WideIntegerBitSource::Kind::InvertedSource;
                    break;
                case WideIntegerBitSource::Kind::InvertedSource:
                    source.kind = WideIntegerBitSource::Kind::Source;
                    break;
                }
                break;
            }
        }
        return source;
    }

    std::optional<std::size_t> trace_wide_integer_source_bit(
        const ValueBinding::WideIntegerValue &wide_integer,
        std::size_t result_bit_index) const {
        if (wide_integer.source_bit_width == 0) {
            return std::nullopt;
        }
        std::size_t source_bit_index = result_bit_index;
        for (auto it = wide_integer.shift_ops.rbegin();
             it != wide_integer.shift_ops.rend(); ++it) {
            switch (it->kind) {
            case ValueBinding::WideIntegerValue::ShiftKind::Shl:
                if (source_bit_index < it->amount) {
                    return std::nullopt;
                }
                source_bit_index -= it->amount;
                break;
            case ValueBinding::WideIntegerValue::ShiftKind::LShr:
                if (source_bit_index >
                    std::numeric_limits<std::size_t>::max() - it->amount) {
                    return std::nullopt;
                }
                source_bit_index += it->amount;
                if (source_bit_index >= wide_integer.source_bit_width) {
                    return std::nullopt;
                }
                break;
            case ValueBinding::WideIntegerValue::ShiftKind::AShr:
                if (source_bit_index >
                    std::numeric_limits<std::size_t>::max() - it->amount) {
                    source_bit_index = wide_integer.source_bit_width - 1;
                } else {
                    source_bit_index += it->amount;
                    if (source_bit_index >= wide_integer.source_bit_width) {
                        source_bit_index = wide_integer.source_bit_width - 1;
                    }
                }
                break;
            }
        }
        return source_bit_index >= wide_integer.source_bit_width
                   ? std::nullopt
                   : std::optional<std::size_t>(source_bit_index);
    }

    CoreIrValue *load_wide_integer_source_bit(
        CoreIrBasicBlock &block, const ValueBinding::WideIntegerValue &wide_integer,
        std::size_t source_bit_index, std::size_t &synthetic_index,
        std::unordered_map<std::size_t, CoreIrValue *> &bit_cache) {
        if (auto it = bit_cache.find(source_bit_index); it != bit_cache.end()) {
            return it->second;
        }
        const CoreIrType *i8_type = parse_type_text("i8");
        const CoreIrType *i64_type = parse_type_text("i64");
        if (i8_type == nullptr || i64_type == nullptr ||
            wide_integer.source_address == nullptr ||
            source_bit_index >= wide_integer.source_bit_width) {
            return nullptr;
        }
        const std::size_t byte_offset = source_bit_index / 8;
        const std::size_t bit_in_byte = source_bit_index % 8;
        CoreIrValue *byte_address = create_byte_offset_typed_address(
            block, wide_integer.source_address, byte_offset, i8_type,
            synthetic_index);
        if (byte_address == nullptr) {
            return nullptr;
        }
        CoreIrValue *byte_value = block.create_instruction<CoreIrLoadInst>(
            i8_type, "ll.wide.bit.load." + std::to_string(synthetic_index++),
            byte_address);
        if (bit_in_byte != 0) {
            const CoreIrConstant *shift_constant =
                context_->create_constant<CoreIrConstantInt>(i8_type, bit_in_byte);
            byte_value = block.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::LShr, i8_type,
                "ll.wide.bit.lshr." + std::to_string(synthetic_index++),
                byte_value, const_cast<CoreIrConstant *>(shift_constant));
        }
        const CoreIrConstant *one_constant =
            context_->create_constant<CoreIrConstantInt>(i8_type, 1);
        CoreIrValue *masked = block.create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::And, i8_type,
            "ll.wide.bit.mask." + std::to_string(synthetic_index++), byte_value,
            const_cast<CoreIrConstant *>(one_constant));
        CoreIrValue *bit_value = block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::ZeroExtend, i64_type,
            "ll.wide.bit.zext." + std::to_string(synthetic_index++), masked);
        bit_cache.emplace(source_bit_index, bit_value);
        return bit_value;
    }

    CoreIrValue *materialize_truncated_wide_integer_value(
        CoreIrBasicBlock &block, const ValueBinding::WideIntegerValue &wide_integer,
        const CoreIrType *target_type, std::size_t &synthetic_index) {
        const auto *target_integer_type = as_integer_type(target_type);
        const CoreIrType *i64_type = parse_type_text("i64");
        if (target_integer_type == nullptr || i64_type == nullptr ||
            target_integer_type->get_bit_width() == 0 ||
            target_integer_type->get_bit_width() > 64) {
            return nullptr;
        }
        CoreIrValue *accumulator =
            context_->create_constant<CoreIrConstantInt>(i64_type, 0);
        std::unordered_map<std::size_t, CoreIrValue *> bit_cache;
        const std::size_t target_bits = target_integer_type->get_bit_width();
        for (std::size_t bit = 0; bit < target_bits; ++bit) {
            const std::optional<std::size_t> source_bit_index =
                trace_wide_integer_source_bit(wide_integer, bit);
            if (!source_bit_index.has_value()) {
                continue;
            }
            CoreIrValue *bit_value = load_wide_integer_source_bit(
                block, wide_integer, *source_bit_index, synthetic_index,
                bit_cache);
            if (bit_value == nullptr) {
                return nullptr;
            }
            if (bit != 0) {
                const CoreIrConstant *shift_constant =
                    context_->create_constant<CoreIrConstantInt>(i64_type, bit);
                bit_value = block.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Shl, i64_type,
                    "ll.wide.trunc.shl." + std::to_string(synthetic_index++),
                    bit_value, const_cast<CoreIrConstant *>(shift_constant));
            }
            accumulator = block.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Or, i64_type,
                "ll.wide.trunc.or." + std::to_string(synthetic_index++),
                accumulator, bit_value);
        }
        if (target_bits == 64) {
            return accumulator;
        }
        return block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::Truncate, target_type,
            "ll.wide.trunc.cast." + std::to_string(synthetic_index++),
            accumulator);
    }

    CoreIrValue *materialize_wide_integer_bit_value(
        CoreIrBasicBlock &block, const ValueBinding::WideIntegerValue &wide_integer,
        const WideIntegerBitSource &bit_source, std::size_t &synthetic_index,
        std::unordered_map<std::size_t, CoreIrValue *> &bit_cache) {
        const CoreIrType *i64_type = parse_type_text("i64");
        if (i64_type == nullptr) {
            return nullptr;
        }
        switch (bit_source.kind) {
        case WideIntegerBitSource::Kind::Zero:
            return context_->create_constant<CoreIrConstantInt>(i64_type, 0);
        case WideIntegerBitSource::Kind::One:
            return context_->create_constant<CoreIrConstantInt>(i64_type, 1);
        case WideIntegerBitSource::Kind::Source:
        case WideIntegerBitSource::Kind::InvertedSource: {
            CoreIrValue *bit_value = load_wide_integer_source_bit(
                block, wide_integer, bit_source.source_bit_index, synthetic_index,
                bit_cache);
            if (bit_value == nullptr ||
                bit_source.kind != WideIntegerBitSource::Kind::InvertedSource) {
                return bit_value;
            }
            const CoreIrConstant *one_constant =
                context_->create_constant<CoreIrConstantInt>(i64_type, 1);
            return block.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Xor, i64_type,
                "ll.wide.bit.invert." + std::to_string(synthetic_index++),
                bit_value, const_cast<CoreIrConstant *>(one_constant));
        }
        }
        return nullptr;
    }

    bool materialize_wide_integer_to_address(
        CoreIrBasicBlock &block, const ValueBinding::WideIntegerValue &wide_integer,
        CoreIrValue *destination_address, std::size_t &synthetic_index,
        std::optional<std::size_t> max_bit_width = std::nullopt) {
        const CoreIrType *i64_type = parse_type_text("i64");
        const CoreIrType *i8_type = parse_type_text("i8");
        if (i64_type == nullptr || i8_type == nullptr ||
            destination_address == nullptr || wide_integer.source_bit_width == 0) {
            return false;
        }
        std::unordered_map<std::size_t, CoreIrValue *> bit_cache;
        const std::size_t stored_bit_width =
            max_bit_width.has_value()
                ? *max_bit_width
                : wide_integer.source_bit_width;
        const std::size_t byte_count = (stored_bit_width + 7U) / 8U;
        for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
            CoreIrValue *byte_value =
                context_->create_constant<CoreIrConstantInt>(i64_type, 0);
            for (std::size_t bit_in_byte = 0; bit_in_byte < 8; ++bit_in_byte) {
                const std::size_t result_bit_index = byte_index * 8U + bit_in_byte;
                if (result_bit_index >= stored_bit_width) {
                    break;
                }
                const std::optional<WideIntegerBitSource> bit_source =
                    evaluate_wide_integer_bit_source(wide_integer, result_bit_index);
                if (!bit_source.has_value()) {
                    return false;
                }
                CoreIrValue *bit_value = materialize_wide_integer_bit_value(
                    block, wide_integer, *bit_source, synthetic_index, bit_cache);
                if (bit_value == nullptr) {
                    return false;
                }
                if (bit_in_byte != 0) {
                    const CoreIrConstant *shift_constant =
                        context_->create_constant<CoreIrConstantInt>(
                            i64_type, bit_in_byte);
                    bit_value = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Shl, i64_type,
                        "ll.wide.byte.shl." + std::to_string(synthetic_index++),
                        bit_value, const_cast<CoreIrConstant *>(shift_constant));
                }
                byte_value = block.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Or, i64_type,
                    "ll.wide.byte.or." + std::to_string(synthetic_index++),
                    byte_value, bit_value);
            }
            CoreIrValue *byte_as_i8 = block.create_instruction<CoreIrCastInst>(
                CoreIrCastKind::Truncate, i8_type,
                "ll.wide.byte.trunc." + std::to_string(synthetic_index++),
                byte_value);
            CoreIrValue *byte_address = create_byte_offset_typed_address(
                block, destination_address, byte_index, i8_type, synthetic_index);
            if (byte_address == nullptr) {
                return false;
            }
            block.create_instruction<CoreIrStoreInst>(void_type(), byte_as_i8,
                                                      byte_address);
        }
        return true;
    }

    CoreIrStackSlot *materialize_wide_integer_bitwise_result(
        CoreIrFunction &function, CoreIrBasicBlock &block,
        const ValueBinding::WideIntegerValue &lhs,
        const ValueBinding::WideIntegerValue &rhs, const CoreIrType *result_type,
        CoreIrBinaryOpcode opcode, std::size_t bit_width,
        std::size_t &synthetic_index) {
        (void)result_type;
        const CoreIrType *i8_type = parse_type_text("i8");
        const CoreIrType *i64_type = parse_type_text("i64");
        if (i8_type == nullptr || i64_type == nullptr || bit_width == 0) {
            return nullptr;
        }
        const std::size_t byte_count = (bit_width + 7U) / 8U;
        const CoreIrType *byte_array_type =
            context_->create_type<CoreIrArrayType>(i8_type, byte_count);
        CoreIrStackSlot *result_slot =
            function.create_stack_slot<CoreIrStackSlot>(
                "ll.wide.bin.result." + std::to_string(synthetic_index++),
                byte_array_type, 1);
        CoreIrValue *result_address =
            materialize_stack_slot_address(block, result_slot, synthetic_index);
        if (result_address == nullptr) {
            return nullptr;
        }

        std::unordered_map<std::size_t, CoreIrValue *> lhs_bit_cache;
        std::unordered_map<std::size_t, CoreIrValue *> rhs_bit_cache;
        for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
            CoreIrValue *byte_value =
                context_->create_constant<CoreIrConstantInt>(i64_type, 0);
            for (std::size_t bit_in_byte = 0; bit_in_byte < 8; ++bit_in_byte) {
                const std::size_t result_bit_index = byte_index * 8U + bit_in_byte;
                if (result_bit_index >= bit_width) {
                    break;
                }
                const auto lhs_source =
                    evaluate_wide_integer_bit_source(lhs, result_bit_index);
                const auto rhs_source =
                    evaluate_wide_integer_bit_source(rhs, result_bit_index);
                if (!lhs_source.has_value() || !rhs_source.has_value()) {
                    return nullptr;
                }
                CoreIrValue *lhs_bit = materialize_wide_integer_bit_value(
                    block, lhs, *lhs_source, synthetic_index, lhs_bit_cache);
                CoreIrValue *rhs_bit = materialize_wide_integer_bit_value(
                    block, rhs, *rhs_source, synthetic_index, rhs_bit_cache);
                if (lhs_bit == nullptr || rhs_bit == nullptr) {
                    return nullptr;
                }
                CoreIrValue *combined_bit = block.create_instruction<CoreIrBinaryInst>(
                    opcode, i64_type,
                    "ll.wide.bin.bit." + std::to_string(synthetic_index++), lhs_bit,
                    rhs_bit);
                if (bit_in_byte != 0) {
                    const CoreIrConstant *shift_constant =
                        context_->create_constant<CoreIrConstantInt>(
                            i64_type, bit_in_byte);
                    combined_bit = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Shl, i64_type,
                        "ll.wide.bin.shl." + std::to_string(synthetic_index++),
                        combined_bit, const_cast<CoreIrConstant *>(shift_constant));
                }
                byte_value = block.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Or, i64_type,
                    "ll.wide.bin.byte.or." + std::to_string(synthetic_index++),
                    byte_value, combined_bit);
            }
            CoreIrValue *result_byte_address = create_byte_offset_typed_address(
                block, result_address, byte_index, i8_type, synthetic_index);
            if (result_byte_address == nullptr) {
                return nullptr;
            }
            CoreIrValue *byte_as_i8 = block.create_instruction<CoreIrCastInst>(
                CoreIrCastKind::Truncate, i8_type,
                "ll.wide.bin.byte.trunc." + std::to_string(synthetic_index++),
                byte_value);
            block.create_instruction<CoreIrStoreInst>(void_type(), byte_as_i8,
                                                      result_byte_address);
        }
        return result_slot;
    }

    CoreIrValue *materialize_wide_integer_eq_compare(
        CoreIrFunction &function, CoreIrBasicBlock &block,
        const ValueBinding::WideIntegerValue &lhs,
        const ValueBinding::WideIntegerValue &rhs, std::size_t bit_width,
        std::size_t &synthetic_index) {
        const CoreIrType *i1_type = parse_type_text("i1");
        const CoreIrType *i8_type = parse_type_text("i8");
        if (i1_type == nullptr || i8_type == nullptr) {
            return nullptr;
        }
        const std::size_t byte_count = (bit_width + 7U) / 8U;
        const CoreIrType *byte_array_type =
            context_->create_type<CoreIrArrayType>(i8_type, byte_count);
        CoreIrStackSlot *lhs_slot = function.create_stack_slot<CoreIrStackSlot>(
            "ll.wide.cmp.lhs." + std::to_string(synthetic_index++), byte_array_type,
            1);
        CoreIrStackSlot *rhs_slot = function.create_stack_slot<CoreIrStackSlot>(
            "ll.wide.cmp.rhs." + std::to_string(synthetic_index++), byte_array_type,
            1);
        CoreIrValue *lhs_address =
            materialize_stack_slot_address(block, lhs_slot, synthetic_index);
        CoreIrValue *rhs_address =
            materialize_stack_slot_address(block, rhs_slot, synthetic_index);
        if (lhs_address == nullptr || rhs_address == nullptr ||
            !materialize_wide_integer_to_address(block, lhs, lhs_address,
                                                 synthetic_index, bit_width) ||
            !materialize_wide_integer_to_address(block, rhs, rhs_address,
                                                 synthetic_index, bit_width)) {
            return nullptr;
        }
        CoreIrValue *all_equal =
            context_->create_constant<CoreIrConstantInt>(i1_type, 1);
        for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
            CoreIrValue *lhs_byte_address = create_byte_offset_typed_address(
                block, lhs_address, byte_index, i8_type, synthetic_index);
            CoreIrValue *rhs_byte_address = create_byte_offset_typed_address(
                block, rhs_address, byte_index, i8_type, synthetic_index);
            if (lhs_byte_address == nullptr || rhs_byte_address == nullptr) {
                return nullptr;
            }
            CoreIrValue *lhs_byte = block.create_instruction<CoreIrLoadInst>(
                i8_type, "ll.wide.cmp.lhs.byte." + std::to_string(synthetic_index++),
                lhs_byte_address);
            CoreIrValue *rhs_byte = block.create_instruction<CoreIrLoadInst>(
                i8_type, "ll.wide.cmp.rhs.byte." + std::to_string(synthetic_index++),
                rhs_byte_address);
            CoreIrValue *byte_eq = block.create_instruction<CoreIrCompareInst>(
                CoreIrComparePredicate::Equal, i1_type,
                "ll.wide.cmp.byte.eq." + std::to_string(synthetic_index++),
                lhs_byte, rhs_byte);
            all_equal = block.create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, i1_type,
                "ll.wide.cmp.all.eq." + std::to_string(synthetic_index++),
                all_equal, byte_eq);
        }
        return all_equal;
    }

    CoreIrValue *cast_pointer_to_opaque_ptr(CoreIrBasicBlock &block,
                                            CoreIrValue *value,
                                            std::size_t &synthetic_index) {
        const CoreIrType *i64_type = parse_type_text("i64");
        const CoreIrType *opaque_ptr_type = parse_type_text("ptr");
        if (value == nullptr || i64_type == nullptr || opaque_ptr_type == nullptr) {
            return nullptr;
        }
        CoreIrValue *as_int = block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::PtrToInt, i64_type,
            "ll.ptr.cast.toint." + std::to_string(synthetic_index++), value);
        return block.create_instruction<CoreIrCastInst>(
            CoreIrCastKind::IntToPtr, opaque_ptr_type,
            "ll.ptr.cast.toptr." + std::to_string(synthetic_index++), as_int);
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

    bool is_i32x4_array_type(const CoreIrType *type) const {
        const auto *array_type = as_array_type(type);
        const auto *element_type =
            array_type == nullptr ? nullptr
                                  : as_integer_type(array_type->get_element_type());
        return array_type != nullptr && element_type != nullptr &&
               array_type->get_element_count() == 4 &&
               element_type->get_bit_width() == 32;
    }

    bool is_zero_or_poison_constant_typed_value(
        const AArch64LlvmImportTypedValue &value) const {
        return value.kind == AArch64LlvmImportValueKind::Constant &&
               (value.constant.kind == AArch64LlvmImportConstantKind::ZeroInitializer ||
                value.constant.kind == AArch64LlvmImportConstantKind::PoisonValue);
    }

    bool is_lane0_zero_seed_constant_typed_value(
        const AArch64LlvmImportTypedValue &value) const {
        if (is_zero_or_poison_constant_typed_value(value)) {
            return true;
        }
        if (value.kind != AArch64LlvmImportValueKind::Constant ||
            value.constant.kind != AArch64LlvmImportConstantKind::Aggregate ||
            value.constant.elements.size() != 4) {
            return false;
        }
        for (std::size_t lane = 1; lane < value.constant.elements.size(); ++lane) {
            const auto &element = value.constant.elements[lane];
            if (element.kind == AArch64LlvmImportConstantKind::ZeroInitializer ||
                element.kind == AArch64LlvmImportConstantKind::PoisonValue) {
                continue;
            }
            if (element.kind != AArch64LlvmImportConstantKind::Integer ||
                element.integer_value != 0) {
                return false;
            }
        }
        return true;
    }

    bool parse_instruction(const AArch64LlvmImportInstruction &instruction,
                           CoreIrFunction &function,
                           CoreIrBasicBlock *&current_block,
                           std::unordered_map<std::string, CoreIrBasicBlock *>
                               &block_map,
                           std::unordered_map<std::string, ValueBinding> &bindings,
                           std::size_t &synthetic_index,
                           std::vector<PendingPhiIncoming> &pending_phi_incomings,
                           std::vector<PendingAggregatePhiIncoming>
                               &pending_aggregate_phi_incomings) {
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
            if (spec.type.kind == AArch64LlvmImportTypeKind::Integer &&
                spec.type.integer_bit_width > 64 &&
                spec.type.integer_bit_width != 128 &&
                (opcode == CoreIrBinaryOpcode::Shl ||
                 opcode == CoreIrBinaryOpcode::LShr ||
                 opcode == CoreIrBinaryOpcode::AShr)) {
                std::optional<ValueBinding::WideIntegerValue> wide_integer =
                    resolve_wide_integer_value(spec.lhs, block, bindings,
                                               synthetic_index);
                const std::optional<std::uint64_t> shift_amount =
                    parse_import_u64_constant(spec.rhs);
                if (wide_integer.has_value() && shift_amount.has_value()) {
                    ValueBinding::WideIntegerValue updated = *wide_integer;
                    ValueBinding::WideIntegerValue::ShiftOp shift_op;
                    shift_op.kind =
                        opcode == CoreIrBinaryOpcode::Shl
                            ? ValueBinding::WideIntegerValue::ShiftKind::Shl
                            : (opcode == CoreIrBinaryOpcode::LShr
                                   ? ValueBinding::WideIntegerValue::ShiftKind::LShr
                                   : ValueBinding::WideIntegerValue::ShiftKind::AShr);
                    shift_op.amount = static_cast<std::size_t>(*shift_amount);
                    updated.shift_ops.push_back(shift_op);
                    for (auto &bitwise_op : updated.bitwise_constant_ops) {
                        bitwise_op.words = shift_constant_words(
                            bitwise_op.words, shift_op.kind, shift_op.amount,
                            spec.type.integer_bit_width);
                    }
                    return bind_wide_integer_result(result_name, updated, bindings);
                }
            }
            if (spec.type.kind == AArch64LlvmImportTypeKind::Integer &&
                spec.type.integer_bit_width > 64 &&
                spec.type.integer_bit_width != 128 &&
                (opcode == CoreIrBinaryOpcode::And ||
                 opcode == CoreIrBinaryOpcode::Or ||
                 opcode == CoreIrBinaryOpcode::Xor)) {
                std::optional<ValueBinding::WideIntegerValue> lhs_wide_integer =
                    resolve_wide_integer_value(spec.lhs, block, bindings,
                                               synthetic_index);
                const auto constant_words = parse_import_constant_words(
                    spec.rhs, spec.type.integer_bit_width);
                if (lhs_wide_integer.has_value() && constant_words.has_value()) {
                    ValueBinding::WideIntegerValue updated = *lhs_wide_integer;
                    ValueBinding::WideIntegerValue::BitwiseConstantOp bitwise_op;
                    bitwise_op.kind =
                        opcode == CoreIrBinaryOpcode::And
                            ? ValueBinding::WideIntegerValue::BitwiseKind::And
                            : (opcode == CoreIrBinaryOpcode::Or
                                   ? ValueBinding::WideIntegerValue::BitwiseKind::Or
                                   : ValueBinding::WideIntegerValue::BitwiseKind::Xor);
                    bitwise_op.words = *constant_words;
                    updated.bitwise_constant_ops.push_back(bitwise_op);
                    return bind_wide_integer_result(result_name, updated, bindings);
                }
                const auto rhs_wide_integer = resolve_wide_integer_value(
                    spec.rhs, block, bindings, synthetic_index);
                if (lhs_wide_integer.has_value() && rhs_wide_integer.has_value()) {
                    CoreIrStackSlot *result_slot =
                        materialize_wide_integer_bitwise_result(
                            function, block, *lhs_wide_integer, *rhs_wide_integer,
                            type, opcode, spec.type.integer_bit_width,
                            synthetic_index);
                    if (result_slot == nullptr) {
                        add_error("unsupported LLVM wide integer bitwise merge: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    return bind_stack_slot_result(result_name, result_slot,
                                                  bindings);
                }
            }
            const auto *i128_array_type = as_array_type(type);
            auto is_lowered_i128_type = [&](const CoreIrType *candidate) {
                const auto *array_type = as_array_type(candidate);
                const auto *element_type =
                    array_type == nullptr ? nullptr
                                          : as_integer_type(
                                                array_type->get_element_type());
                return array_type != nullptr && element_type != nullptr &&
                       array_type->get_element_count() == 2 &&
                       element_type->get_bit_width() == 64;
            };
            auto get_or_create_runtime_decl =
                [&](const std::string &name,
                    const CoreIrFunctionType *function_type) -> CoreIrFunction * {
                if (auto it = functions_.find(name); it != functions_.end()) {
                    return it->second;
                }
                CoreIrFunction *callee = module_->create_function<CoreIrFunction>(
                    name, function_type, false, false);
                functions_[name] = callee;
                return callee;
            };
            auto lower_inline_v4i32_binary_helper =
                [&](const std::string &helper_name) -> bool {
                    if (!is_i32x4_array_type(type)) {
                        return false;
                    }
                    const CoreIrType *ptr_type = parse_type_text("ptr");
                    if (ptr_type == nullptr) {
                        return false;
                    }
                    CoreIrValue *lhs_address =
                        ensure_addressable_aggregate_typed_operand(
                            type, spec.lhs, function, block, bindings,
                            synthetic_index, line_number);
                    CoreIrValue *rhs_address =
                        ensure_addressable_aggregate_typed_operand(
                            type, spec.rhs, function, block, bindings,
                            synthetic_index, line_number);
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? "ll.inline.binary.vec." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            type, get_storage_alignment(type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    if (lhs_address == nullptr || rhs_address == nullptr ||
                        result_address == nullptr) {
                        return false;
                    }
                    const CoreIrFunctionType *helper_type =
                        context_->create_type<CoreIrFunctionType>(
                            void_type(),
                            std::vector<const CoreIrType *>{ptr_type, ptr_type,
                                                            ptr_type},
                            false);
                    CoreIrFunction *helper =
                        get_or_create_runtime_decl(helper_name, helper_type);
                    block.create_instruction<CoreIrCallInst>(
                        void_type(), "", helper->get_name(), helper_type,
                        std::vector<CoreIrValue *>{result_address, lhs_address,
                                                   rhs_address});
                    return bind_stack_slot_result(result_name, result_slot,
                                                  bindings);
                };
            if (is_import_integer_bit_width(spec.type, 128) &&
                is_lowered_i128_type(type)) {
                if (opcode != CoreIrBinaryOpcode::Add &&
                    opcode != CoreIrBinaryOpcode::Sub &&
                    opcode != CoreIrBinaryOpcode::Mul &&
                    opcode != CoreIrBinaryOpcode::And &&
                    opcode != CoreIrBinaryOpcode::Or &&
                    opcode != CoreIrBinaryOpcode::Xor &&
                    opcode != CoreIrBinaryOpcode::SDiv &&
                    opcode != CoreIrBinaryOpcode::UDiv &&
                    opcode != CoreIrBinaryOpcode::SRem &&
                    opcode != CoreIrBinaryOpcode::URem &&
                    opcode != CoreIrBinaryOpcode::Shl &&
                    opcode != CoreIrBinaryOpcode::LShr &&
                    opcode != CoreIrBinaryOpcode::AShr) {
                    add_error("unsupported LLVM i128 binary opcode: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *lhs = resolve_typed_value_operand(
                    type, spec.lhs, block, bindings, synthetic_index, line_number);
                CoreIrValue *rhs = resolve_typed_value_operand(
                    type, spec.rhs, block, bindings, synthetic_index, line_number);
                if (lhs == nullptr || rhs == nullptr) {
                    return false;
                }
                auto spill_i128_operand = [&](CoreIrValue *value,
                                              const AArch64LlvmImportTypedValue &typed_value,
                                              const std::string &tag) -> CoreIrValue * {
                    if (typed_value.kind == AArch64LlvmImportValueKind::Constant) {
                        return ensure_addressable_aggregate_typed_operand(
                            type, typed_value, function, block, bindings,
                            synthetic_index, line_number);
                    }
                    CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                        tag + std::to_string(synthetic_index++), type,
                        get_storage_alignment(type));
                    CoreIrValue *slot_address =
                        materialize_stack_slot_address(block, slot, synthetic_index);
                    if (slot_address == nullptr) {
                        return nullptr;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(), value, slot);
                    return slot_address;
                };
                auto load_i128_word = [&](CoreIrValue *address, std::size_t word_index,
                                          const std::string &tag) -> CoreIrValue * {
                    CoreIrValue *element_address = create_element_address(
                        block, address, type, static_cast<std::uint64_t>(word_index),
                        synthetic_index);
                    return element_address == nullptr
                               ? nullptr
                               : block.create_instruction<CoreIrLoadInst>(
                                     i128_array_type->get_element_type(),
                                     tag + std::to_string(synthetic_index++),
                                     element_address);
                };
                auto store_i128_words = [&](CoreIrValue *low_result,
                                            CoreIrValue *high_result,
                                            const std::string &slot_prefix) -> bool {
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? slot_prefix + std::to_string(synthetic_index++)
                                : result_name,
                            type, get_storage_alignment(type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    CoreIrValue *low_address = create_element_address(
                        block, result_address, type, 0, synthetic_index);
                    CoreIrValue *high_address = create_element_address(
                        block, result_address, type, 1, synthetic_index);
                    if (result_address == nullptr || low_address == nullptr ||
                        high_address == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(), low_result,
                                                              low_address);
                    block.create_instruction<CoreIrStoreInst>(void_type(), high_result,
                                                              high_address);
                    return bind_stack_slot_result(result_name, result_slot, bindings);
                };
                const CoreIrType *i64_type = i128_array_type->get_element_type();
                auto make_i64_constant = [&](std::uint64_t value) -> CoreIrValue * {
                    return context_->create_constant<CoreIrConstantInt>(i64_type, value);
                };
                if (opcode == CoreIrBinaryOpcode::Add ||
                    opcode == CoreIrBinaryOpcode::Sub) {
                    const CoreIrType *i1_type = parse_type_text("i1");
                    CoreIrValue *lhs_address =
                        spill_i128_operand(lhs, spec.lhs, "ll.i128.bin.lhs.");
                    CoreIrValue *rhs_address =
                        spill_i128_operand(rhs, spec.rhs, "ll.i128.bin.rhs.");
                    if (i64_type == nullptr || i1_type == nullptr ||
                        lhs_address == nullptr || rhs_address == nullptr) {
                        return false;
                    }
                    CoreIrValue *lhs_low =
                        load_i128_word(lhs_address, 0, "ll.i128.bin.l.low.");
                    CoreIrValue *lhs_high =
                        load_i128_word(lhs_address, 1, "ll.i128.bin.l.high.");
                    CoreIrValue *rhs_low =
                        load_i128_word(rhs_address, 0, "ll.i128.bin.r.low.");
                    CoreIrValue *rhs_high =
                        load_i128_word(rhs_address, 1, "ll.i128.bin.r.high.");
                    if (lhs_low == nullptr || lhs_high == nullptr ||
                        rhs_low == nullptr || rhs_high == nullptr) {
                        return false;
                    }
                    CoreIrValue *low_result = block.create_instruction<CoreIrBinaryInst>(
                        opcode, i64_type,
                        "ll.i128.bin.low." + std::to_string(synthetic_index++),
                        lhs_low, rhs_low);
                    CoreIrValue *carry_flag = block.create_instruction<CoreIrCompareInst>(
                        CoreIrComparePredicate::UnsignedLess, i1_type,
                        "ll.i128.bin.carry." + std::to_string(synthetic_index++),
                        opcode == CoreIrBinaryOpcode::Add ? low_result : lhs_low,
                        opcode == CoreIrBinaryOpcode::Add ? lhs_low : rhs_low);
                    CoreIrValue *carry_value = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::ZeroExtend, i64_type,
                        "ll.i128.bin.carry.zext." +
                            std::to_string(synthetic_index++),
                        carry_flag);
                    CoreIrValue *high_base = block.create_instruction<CoreIrBinaryInst>(
                        opcode, i64_type,
                        "ll.i128.bin.high.base." +
                            std::to_string(synthetic_index++),
                        lhs_high, rhs_high);
                    CoreIrValue *high_result = block.create_instruction<CoreIrBinaryInst>(
                        opcode == CoreIrBinaryOpcode::Add ? CoreIrBinaryOpcode::Add
                                                          : CoreIrBinaryOpcode::Sub,
                        i64_type,
                        "ll.i128.bin.high." + std::to_string(synthetic_index++),
                        high_base, carry_value);
                    return store_i128_words(low_result, high_result,
                                            "ll.i128.bin.slot.");
                }
                if (opcode == CoreIrBinaryOpcode::And ||
                    opcode == CoreIrBinaryOpcode::Or ||
                    opcode == CoreIrBinaryOpcode::Xor) {
                    CoreIrValue *lhs_address =
                        spill_i128_operand(lhs, spec.lhs, "ll.i128.bin.lhs.");
                    CoreIrValue *rhs_address =
                        spill_i128_operand(rhs, spec.rhs, "ll.i128.bin.rhs.");
                    if (lhs_address == nullptr || rhs_address == nullptr) {
                        return false;
                    }
                    CoreIrValue *lhs_low =
                        load_i128_word(lhs_address, 0, "ll.i128.bin.l.low.");
                    CoreIrValue *lhs_high =
                        load_i128_word(lhs_address, 1, "ll.i128.bin.l.high.");
                    CoreIrValue *rhs_low =
                        load_i128_word(rhs_address, 0, "ll.i128.bin.r.low.");
                    CoreIrValue *rhs_high =
                        load_i128_word(rhs_address, 1, "ll.i128.bin.r.high.");
                    if (lhs_low == nullptr || lhs_high == nullptr ||
                        rhs_low == nullptr || rhs_high == nullptr) {
                        return false;
                    }
                    CoreIrValue *low_result = block.create_instruction<CoreIrBinaryInst>(
                        opcode, i64_type,
                        "ll.i128.bin.low." + std::to_string(synthetic_index++),
                        lhs_low, rhs_low);
                    CoreIrValue *high_result =
                        block.create_instruction<CoreIrBinaryInst>(
                            opcode, i64_type,
                            "ll.i128.bin.high." + std::to_string(synthetic_index++),
                            lhs_high, rhs_high);
                    return store_i128_words(low_result, high_result,
                                            "ll.i128.bin.slot.");
                }
                if (opcode == CoreIrBinaryOpcode::Shl ||
                    opcode == CoreIrBinaryOpcode::LShr ||
                    opcode == CoreIrBinaryOpcode::AShr) {
                    const std::optional<std::uint64_t> shift_amount =
                        parse_import_u64_constant(spec.rhs);
                    CoreIrValue *lhs_address =
                        spill_i128_operand(lhs, spec.lhs, "ll.i128.bin.lhs.");
                    if (lhs_address == nullptr) {
                        add_error("unsupported LLVM i128 shift shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    if (!shift_amount.has_value()) {
                        const CoreIrType *i32_type = parse_type_text("i32");
                        auto materialize_shift_value = [&]() -> CoreIrValue * {
                            if (i32_type == nullptr) {
                                return nullptr;
                            }
                            CoreIrValue *rhs_address =
                                ensure_addressable_aggregate_typed_operand(
                                    type, spec.rhs, function, block, bindings,
                                    synthetic_index, line_number);
                            if (rhs_address == nullptr) {
                                return nullptr;
                            }
                            CoreIrValue *rhs_low_address = create_element_address(
                                block, rhs_address, type, 0, synthetic_index);
                            if (rhs_low_address == nullptr) {
                                return nullptr;
                            }
                            CoreIrValue *rhs_low_word =
                                block.create_instruction<CoreIrLoadInst>(
                                    i64_type,
                                    "ll.i128.bin.shift.low." +
                                        std::to_string(synthetic_index++),
                                    rhs_low_address);
                            return block.create_instruction<CoreIrCastInst>(
                                CoreIrCastKind::Truncate, i32_type,
                                "ll.i128.bin.shift.trunc." +
                                    std::to_string(synthetic_index++),
                                rhs_low_word);
                        };
                        std::string runtime_name;
                        switch (opcode) {
                        case CoreIrBinaryOpcode::Shl:
                            runtime_name = "__ashlti3";
                            break;
                        case CoreIrBinaryOpcode::LShr:
                            runtime_name = "__lshrti3";
                            break;
                        case CoreIrBinaryOpcode::AShr:
                            runtime_name = "__ashrti3";
                            break;
                        default:
                            break;
                        }
                        CoreIrValue *lhs_value = lhs;
                        if (spec.lhs.kind == AArch64LlvmImportValueKind::Constant) {
                            lhs_value = block.create_instruction<CoreIrLoadInst>(
                                type,
                                "ll.i128.bin.arg.lhs." +
                                    std::to_string(synthetic_index++),
                                lhs_address);
                        }
                        CoreIrValue *shift_value = materialize_shift_value();
                        if (lhs_value == nullptr || shift_value == nullptr ||
                            i32_type == nullptr) {
                            add_error("unsupported LLVM i128 shift shape: " + line,
                                      line_number, 1);
                            return false;
                        }
                        const CoreIrFunctionType *runtime_type =
                            context_->create_type<CoreIrFunctionType>(
                                type,
                                std::vector<const CoreIrType *>{type, i32_type},
                                false);
                        CoreIrFunction *callee =
                            get_or_create_runtime_decl(runtime_name, runtime_type);
                        CoreIrValue *call_value = block.create_instruction<
                            CoreIrCallInst>(
                            type,
                            result_name.empty()
                                ? "ll.i128.bin.call." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            callee->get_name(), runtime_type,
                            std::vector<CoreIrValue *>{lhs_value, shift_value});
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                result_name.empty()
                                    ? "ll.i128.bin.slot." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                type, get_storage_alignment(type));
                        block.create_instruction<CoreIrStoreInst>(void_type(),
                                                                  call_value,
                                                                  result_slot);
                        return bind_stack_slot_result(result_name, result_slot,
                                                      bindings);
                    }
                    CoreIrValue *lhs_low =
                        load_i128_word(lhs_address, 0, "ll.i128.bin.l.low.");
                    CoreIrValue *lhs_high =
                        load_i128_word(lhs_address, 1, "ll.i128.bin.l.high.");
                    if (lhs_low == nullptr || lhs_high == nullptr) {
                        return false;
                    }
                    const std::uint64_t shift = *shift_amount;
                    const CoreIrConstant *shift63 = context_->create_constant<
                        CoreIrConstantInt>(i64_type, 63);
                    CoreIrValue *sign_word = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::AShr, i64_type,
                        "ll.i128.bin.sign." + std::to_string(synthetic_index++),
                        lhs_high, const_cast<CoreIrConstant *>(shift63));
                    CoreIrValue *low_result = nullptr;
                    CoreIrValue *high_result = nullptr;
                    if (shift == 0) {
                        low_result = lhs_low;
                        high_result = lhs_high;
                    } else if (shift >= 128) {
                        if (opcode == CoreIrBinaryOpcode::AShr) {
                            low_result = sign_word;
                            high_result = sign_word;
                        } else {
                            low_result = make_i64_constant(0);
                            high_result = make_i64_constant(0);
                        }
                    } else if (shift >= 64) {
                        const std::uint64_t inner_shift = shift - 64;
                        if (opcode == CoreIrBinaryOpcode::Shl) {
                            low_result = make_i64_constant(0);
                            high_result =
                                inner_shift == 0
                                    ? lhs_low
                                    : block.create_instruction<CoreIrBinaryInst>(
                                          CoreIrBinaryOpcode::Shl, i64_type,
                                          "ll.i128.bin.shl.high." +
                                              std::to_string(synthetic_index++),
                                          lhs_low, make_i64_constant(inner_shift));
                        } else if (opcode == CoreIrBinaryOpcode::LShr) {
                            high_result = make_i64_constant(0);
                            low_result =
                                inner_shift == 0
                                    ? lhs_high
                                    : block.create_instruction<CoreIrBinaryInst>(
                                          CoreIrBinaryOpcode::LShr, i64_type,
                                          "ll.i128.bin.lshr.low." +
                                              std::to_string(synthetic_index++),
                                          lhs_high, make_i64_constant(inner_shift));
                        } else {
                            high_result = sign_word;
                            low_result =
                                inner_shift == 0
                                    ? lhs_high
                                    : block.create_instruction<CoreIrBinaryInst>(
                                          CoreIrBinaryOpcode::AShr, i64_type,
                                          "ll.i128.bin.ashr.low." +
                                              std::to_string(synthetic_index++),
                                          lhs_high, make_i64_constant(inner_shift));
                        }
                    } else {
                        const std::uint64_t carry_shift = 64 - shift;
                        if (opcode == CoreIrBinaryOpcode::Shl) {
                            low_result = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Shl, i64_type,
                                "ll.i128.bin.shl.low." +
                                    std::to_string(synthetic_index++),
                                lhs_low, make_i64_constant(shift));
                            CoreIrValue *high_shift =
                                block.create_instruction<CoreIrBinaryInst>(
                                    CoreIrBinaryOpcode::Shl, i64_type,
                                    "ll.i128.bin.shl.high.base." +
                                        std::to_string(synthetic_index++),
                                    lhs_high, make_i64_constant(shift));
                            CoreIrValue *carry = block.create_instruction<
                                CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::LShr, i64_type,
                                "ll.i128.bin.shl.carry." +
                                    std::to_string(synthetic_index++),
                                lhs_low, make_i64_constant(carry_shift));
                            high_result = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Or, i64_type,
                                "ll.i128.bin.shl.high." +
                                    std::to_string(synthetic_index++),
                                high_shift, carry);
                        } else {
                            CoreIrValue *low_shift = block.create_instruction<
                                CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::LShr, i64_type,
                                "ll.i128.bin.shr.low.base." +
                                    std::to_string(synthetic_index++),
                                lhs_low, make_i64_constant(shift));
                            CoreIrValue *carry = block.create_instruction<
                                CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Shl, i64_type,
                                "ll.i128.bin.shr.carry." +
                                    std::to_string(synthetic_index++),
                                lhs_high, make_i64_constant(carry_shift));
                            low_result = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Or, i64_type,
                                "ll.i128.bin.shr.low." +
                                    std::to_string(synthetic_index++),
                                low_shift, carry);
                            high_result = block.create_instruction<CoreIrBinaryInst>(
                                opcode == CoreIrBinaryOpcode::AShr
                                    ? CoreIrBinaryOpcode::AShr
                                    : CoreIrBinaryOpcode::LShr,
                                i64_type,
                                "ll.i128.bin.shr.high." +
                                    std::to_string(synthetic_index++),
                                lhs_high, make_i64_constant(shift));
                        }
                    }
                    return store_i128_words(low_result, high_result,
                                            "ll.i128.bin.slot.");
                }
                auto materialize_runtime_i128_argument =
                    [&](CoreIrValue *value,
                        const AArch64LlvmImportTypedValue &typed_value,
                        const std::string &tag) -> CoreIrValue * {
                    if (typed_value.kind != AArch64LlvmImportValueKind::Constant) {
                        return value;
                    }
                    CoreIrValue *address = ensure_addressable_aggregate_typed_operand(
                        type, typed_value, function, block, bindings,
                        synthetic_index, line_number);
                    return address == nullptr
                               ? nullptr
                               : block.create_instruction<CoreIrLoadInst>(
                                     type, tag + std::to_string(synthetic_index++),
                                     address);
                };
                lhs = materialize_runtime_i128_argument(
                    lhs, spec.lhs, "ll.i128.bin.arg.lhs.");
                rhs = materialize_runtime_i128_argument(
                    rhs, spec.rhs, "ll.i128.bin.arg.rhs.");
                if (lhs == nullptr || rhs == nullptr) {
                    return false;
                }
                std::string runtime_name;
                switch (opcode) {
                case CoreIrBinaryOpcode::Add:
                    runtime_name = "__addti3";
                    break;
                case CoreIrBinaryOpcode::Sub:
                    runtime_name = "__subti3";
                    break;
                case CoreIrBinaryOpcode::Mul:
                    runtime_name = "__multi3";
                    break;
                case CoreIrBinaryOpcode::SDiv:
                    runtime_name = "__divti3";
                    break;
                case CoreIrBinaryOpcode::UDiv:
                    runtime_name = "__udivti3";
                    break;
                case CoreIrBinaryOpcode::SRem:
                    runtime_name = "__modti3";
                    break;
                case CoreIrBinaryOpcode::URem:
                    runtime_name = "__umodti3";
                    break;
                default:
                    break;
                }
                const CoreIrFunctionType *runtime_type =
                    context_->create_type<CoreIrFunctionType>(
                        type, std::vector<const CoreIrType *>{type, type}, false);
                CoreIrFunction *callee =
                    get_or_create_runtime_decl(runtime_name, runtime_type);
                CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                    type,
                    result_name.empty()
                        ? "ll.i128.bin.call." + std::to_string(synthetic_index++)
                        : result_name,
                    callee->get_name(), runtime_type,
                    std::vector<CoreIrValue *>{lhs, rhs});
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.i128.bin.slot." +
                                  std::to_string(synthetic_index++)
                            : result_name,
                        type, get_storage_alignment(type));
                block.create_instruction<CoreIrStoreInst>(void_type(), call_value,
                                                          result_slot);
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            if (const auto *array_type = as_array_type(type); array_type != nullptr) {
                if (is_i32x4_array_type(type)) {
                    if (opcode == CoreIrBinaryOpcode::Add &&
                        lower_inline_v4i32_binary_helper(kInlineVecAddV4I32)) {
                        return true;
                    }
                    if (opcode == CoreIrBinaryOpcode::Mul &&
                        lower_inline_v4i32_binary_helper(kInlineVecMulV4I32)) {
                        return true;
                    }
                }
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
            const CoreIrType *target_type = lower_import_type(spec.target_type);
            if (target_type == nullptr) {
                add_error("unsupported LLVM cast target type", line_number, 1);
                return false;
            }
            if ((kind == CoreIrCastKind::ZeroExtend ||
                 kind == CoreIrCastKind::SignExtend) &&
                spec.target_type.kind == AArch64LlvmImportTypeKind::Integer &&
                spec.target_type.integer_bit_width > 64 &&
                spec.target_type.integer_bit_width != 128) {
                ValueBinding::WideIntegerValue wide_integer;
                wide_integer.source_bit_width =
                    spec.source_type.kind == AArch64LlvmImportTypeKind::Integer
                        ? spec.source_type.integer_bit_width
                        : 0;
                wide_integer.high_bit_fill =
                    kind == CoreIrCastKind::SignExtend
                        ? ValueBinding::WideIntegerValue::HighBitFill::Sign
                        : ValueBinding::WideIntegerValue::HighBitFill::Zero;
                if (spec.source_type.kind == AArch64LlvmImportTypeKind::Integer &&
                    spec.source_type.integer_bit_width <= 64) {
                    CoreIrValue *operand = resolve_typed_value_operand(
                        source_type, spec.source_value, block, bindings,
                        synthetic_index, line_number);
                    if (operand == nullptr) {
                        add_error("unsupported LLVM wide integer cast source: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    wide_integer.source_address = materialize_scalar_value_to_address(
                        function, block, source_type, operand, synthetic_index);
                    if (wide_integer.source_address == nullptr) {
                        return false;
                    }
                    return bind_wide_integer_result(result_name, wide_integer,
                                                    bindings);
                }
                if (is_import_integer_bit_width(spec.source_type, 128)) {
                    CoreIrValue *source_address =
                        ensure_addressable_aggregate_typed_operand(
                            source_type, spec.source_value, function, block,
                            bindings, synthetic_index, line_number);
                    if (source_address == nullptr) {
                        add_error("unsupported LLVM wide integer cast source: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    wide_integer.source_address = source_address;
                    wide_integer.source_bit_width = 128;
                    return bind_wide_integer_result(result_name, wide_integer,
                                                    bindings);
                }
            }
            const auto *target_array_type = as_array_type(target_type);
            const auto *target_element_type =
                target_array_type == nullptr
                    ? nullptr
                    : as_integer_type(target_array_type->get_element_type());
            if ((kind == CoreIrCastKind::ZeroExtend ||
                 kind == CoreIrCastKind::SignExtend) &&
                is_integer_import_type(spec.source_type) &&
                spec.source_type.integer_bit_width <= 64 &&
                is_import_integer_bit_width(spec.target_type, 128) &&
                target_array_type != nullptr && target_element_type != nullptr &&
                target_array_type->get_element_count() == 2 &&
                target_element_type->get_bit_width() == 64) {
                CoreIrValue *operand = resolve_typed_value_operand(
                    source_type, spec.source_value, block, bindings,
                    synthetic_index, line_number);
                const CoreIrType *i64_type = target_array_type->get_element_type();
                if (operand == nullptr || i64_type == nullptr) {
                    add_error("unsupported LLVM integer-to-i128 cast: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *low_word = operand;
                if (spec.source_type.integer_bit_width < 64) {
                    low_word = block.create_instruction<CoreIrCastInst>(
                        kind, i64_type,
                        "ll.i128.cast.low." + std::to_string(synthetic_index++),
                        operand);
                }
                CoreIrValue *high_word = nullptr;
                if (kind == CoreIrCastKind::ZeroExtend) {
                    high_word =
                        context_->create_constant<CoreIrConstantInt>(i64_type, 0);
                } else {
                    const CoreIrConstant *shift_constant =
                        context_->create_constant<CoreIrConstantInt>(i64_type, 63);
                    high_word = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::AShr, i64_type,
                        "ll.i128.cast.high." + std::to_string(synthetic_index++),
                        low_word, const_cast<CoreIrConstant *>(shift_constant));
                }
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.i128.cast.slot." +
                                  std::to_string(synthetic_index++)
                            : result_name,
                        target_type, get_storage_alignment(target_type));
                CoreIrValue *result_address = materialize_stack_slot_address(
                    block, result_slot, synthetic_index);
                CoreIrValue *low_address = create_element_address(
                    block, result_address, target_type, 0, synthetic_index);
                CoreIrValue *high_address = create_element_address(
                    block, result_address, target_type, 1, synthetic_index);
                if (result_address == nullptr || low_address == nullptr ||
                    high_address == nullptr) {
                    return false;
                }
                block.create_instruction<CoreIrStoreInst>(void_type(), low_word,
                                                          low_address);
                block.create_instruction<CoreIrStoreInst>(void_type(), high_word,
                                                          high_address);
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            if (kind == CoreIrCastKind::Truncate &&
                spec.source_type.kind == AArch64LlvmImportTypeKind::Integer &&
                spec.source_type.integer_bit_width > 64 &&
                spec.source_type.integer_bit_width != 128) {
                std::optional<ValueBinding::WideIntegerValue> wide_integer =
                    resolve_wide_integer_value(spec.source_value, block, bindings,
                                               synthetic_index);
                if (wide_integer.has_value()) {
                    if (is_import_integer_bit_width(spec.target_type, 128) &&
                        target_array_type != nullptr && target_element_type != nullptr &&
                        target_array_type->get_element_count() == 2 &&
                        target_element_type->get_bit_width() == 64) {
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                result_name.empty()
                                    ? "ll.i128.cast.slot." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                target_type, get_storage_alignment(target_type));
                        CoreIrValue *result_address =
                            materialize_stack_slot_address(block, result_slot,
                                                           synthetic_index);
                        if (result_address == nullptr ||
                            !materialize_wide_integer_to_address(
                                block, *wide_integer, result_address,
                                synthetic_index, 128)) {
                            add_error("unsupported LLVM wide integer truncate: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        return bind_stack_slot_result(result_name, result_slot,
                                                      bindings);
                    }
                    CoreIrValue *materialized =
                        materialize_truncated_wide_integer_value(
                            block, *wide_integer, target_type, synthetic_index);
                    if (materialized == nullptr) {
                        add_error("unsupported LLVM wide integer truncate: " + line,
                                  line_number, 1);
                        return false;
                    }
                    return bind_instruction_result(result_name, materialized,
                                                   bindings);
                }
            }
            if (kind == CoreIrCastKind::Truncate &&
                is_import_integer_bit_width(spec.source_type, 128) &&
                is_integer_import_type(spec.target_type) &&
                spec.target_type.integer_bit_width <= 64) {
                CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                    source_type, spec.source_value, function, block, bindings,
                    synthetic_index, line_number);
                const CoreIrType *i64_type = parse_type_text("i64");
                if (source_address == nullptr || i64_type == nullptr) {
                    add_error("unsupported LLVM i128 truncate: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *low_address = create_element_address(
                    block, source_address, source_type, 0, synthetic_index);
                if (low_address == nullptr) {
                    return false;
                }
                CoreIrValue *low_word = block.create_instruction<CoreIrLoadInst>(
                    i64_type, "ll.i128.cast.low." + std::to_string(synthetic_index++),
                    low_address);
                if (spec.target_type.integer_bit_width == 64) {
                    return bind_instruction_result(result_name, low_word, bindings);
                }
                return bind_instruction_result(
                    result_name,
                    block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::Truncate, target_type, result_name,
                        low_word),
                    bindings);
            }
            const auto *source_array_type = as_array_type(source_type);
            const auto *target_cast_array_type = as_array_type(target_type);
            if ((kind == CoreIrCastKind::SignedIntToFloat ||
                 kind == CoreIrCastKind::UnsignedIntToFloat) &&
                is_i32x4_vector_type(source_type) &&
                target_cast_array_type != nullptr &&
                target_cast_array_type->get_element_count() == 4 &&
                dynamic_cast<const CoreIrFloatType *>(
                    target_cast_array_type->get_element_type()) != nullptr) {
                const auto *source_vector_type =
                    static_cast<const CoreIrVectorType *>(source_type);
                CoreIrValue *source_value = resolve_typed_value_operand(
                    source_type, spec.source_value, block, bindings,
                    synthetic_index, line_number);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.cast." + std::to_string(synthetic_index++)
                            : result_name,
                        target_type, get_storage_alignment(target_type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot,
                                                   synthetic_index);
                if (source_value == nullptr || result_address == nullptr) {
                    return false;
                }
                for (std::size_t lane = 0; lane < source_vector_type->get_element_count();
                     ++lane) {
                    CoreIrValue *lane_index =
                        get_i32_constant(static_cast<std::uint64_t>(lane));
                    CoreIrValue *lane_source =
                        block.create_instruction<CoreIrExtractElementInst>(
                            source_vector_type->get_element_type(),
                            "ll.vec.cast.extract." +
                                std::to_string(synthetic_index++),
                            source_value, lane_index);
                    CoreIrValue *lane_value =
                        block.create_instruction<CoreIrCastInst>(
                            kind, target_cast_array_type->get_element_type(),
                            "ll.vec.cast.lane." +
                                std::to_string(synthetic_index++),
                            lane_source);
                    CoreIrValue *destination_element = create_element_address(
                        block, result_address, target_type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    if (lane_source == nullptr || lane_value == nullptr ||
                        destination_element == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(
                        void_type(), lane_value, destination_element);
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            if (source_array_type != nullptr && target_cast_array_type != nullptr &&
                source_array_type->get_element_count() ==
                    target_cast_array_type->get_element_count()) {
                CoreIrValue *source_address =
                    ensure_addressable_aggregate_typed_operand(
                        source_type, spec.source_value, function, block,
                        bindings, synthetic_index, line_number);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.cast." + std::to_string(synthetic_index++)
                            : result_name,
                        target_type, get_storage_alignment(target_type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot,
                                                   synthetic_index);
                if (source_address == nullptr || result_address == nullptr) {
                    return false;
                }
                for (std::size_t lane = 0;
                     lane < source_array_type->get_element_count(); ++lane) {
                    CoreIrValue *source_element = create_element_address(
                        block, source_address, source_type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    CoreIrValue *destination_element = create_element_address(
                        block, result_address, target_type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    if (source_element == nullptr ||
                        destination_element == nullptr) {
                        return false;
                    }
                    CoreIrValue *lane_source =
                        block.create_instruction<CoreIrLoadInst>(
                            source_array_type->get_element_type(),
                            "ll.vec.cast.src." +
                                std::to_string(synthetic_index++),
                            source_element);
                    CoreIrValue *lane_value =
                        block.create_instruction<CoreIrCastInst>(
                            kind, target_cast_array_type->get_element_type(),
                            "ll.vec.cast.lane." +
                                std::to_string(synthetic_index++),
                            lane_source);
                    block.create_instruction<CoreIrStoreInst>(
                        void_type(), lane_value, destination_element);
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            CoreIrValue *operand = resolve_typed_value_operand(
                source_type, spec.source_value, block, bindings,
                synthetic_index, line_number);
            if (operand == nullptr) {
                add_error("unsupported LLVM cast target type", line_number, 1);
                return false;
            }
            if ((kind == CoreIrCastKind::FloatToSignedInt ||
                 kind == CoreIrCastKind::FloatToUnsignedInt) &&
                is_float_import_type(spec.source_type) &&
                is_import_integer_bit_width(spec.target_type, 128) &&
                target_array_type != nullptr && target_element_type != nullptr &&
                target_array_type->get_element_count() == 2 &&
                target_element_type->get_bit_width() == 64) {
                std::string runtime_name;
                if (kind == CoreIrCastKind::FloatToUnsignedInt) {
                    add_error("unsupported LLVM float-to-u128 cast: " + line,
                              line_number, 1);
                    return false;
                }
                switch (spec.source_type.kind) {
                case AArch64LlvmImportTypeKind::Float32:
                    runtime_name = "__fixsfti";
                    break;
                case AArch64LlvmImportTypeKind::Float64:
                    runtime_name = "__fixdfti";
                    break;
                default:
                    add_error("unsupported LLVM float-to-i128 cast source type: " +
                                  line,
                              line_number, 1);
                    return false;
                }
                const CoreIrFunctionType *runtime_type =
                    context_->create_type<CoreIrFunctionType>(
                        target_type, std::vector<const CoreIrType *>{source_type},
                        false);
                CoreIrFunction *callee = nullptr;
                if (auto it = functions_.find(runtime_name); it != functions_.end()) {
                    callee = it->second;
                } else {
                    callee = module_->create_function<CoreIrFunction>(
                        runtime_name, runtime_type, false, false);
                    functions_[runtime_name] = callee;
                }
                CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                    target_type,
                    result_name.empty()
                        ? "ll.i128.cast.call." + std::to_string(synthetic_index++)
                        : result_name,
                    callee->get_name(), runtime_type,
                    std::vector<CoreIrValue *>{operand});
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.i128.cast.slot." +
                                  std::to_string(synthetic_index++)
                            : result_name,
                        target_type, get_storage_alignment(target_type));
                block.create_instruction<CoreIrStoreInst>(void_type(), call_value,
                                                          result_slot);
                return bind_stack_slot_result(result_name, result_slot, bindings);
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
            if (same_type || pointer_identity) {
                return bind_instruction_result(result_name, operand, bindings);
            }
            if (get_type_size(source_type) != get_type_size(target_type)) {
                add_error("unsupported LLVM identity-style cast: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *source_address = nullptr;
            if (is_aggregate_type(source_type)) {
                source_address = ensure_addressable_aggregate_typed_operand(
                    source_type, spec.source_value, function, block, bindings,
                    synthetic_index, line_number);
            } else {
                CoreIrStackSlot *source_slot = function.create_stack_slot<CoreIrStackSlot>(
                    "ll.cast.src." + std::to_string(synthetic_index++), source_type,
                    get_storage_alignment(source_type));
                source_address =
                    materialize_stack_slot_address(block, source_slot, synthetic_index);
                if (source_address == nullptr) {
                    return false;
                }
                block.create_instruction<CoreIrStoreInst>(void_type(), operand,
                                                          source_slot);
            }
            if (source_address == nullptr) {
                return false;
            }
            if (is_aggregate_type(target_type)) {
                CoreIrStackSlot *result_slot = function.create_stack_slot<CoreIrStackSlot>(
                    result_name.empty()
                        ? "ll.cast.dst." + std::to_string(synthetic_index++)
                        : result_name,
                    target_type, get_storage_alignment(target_type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot, synthetic_index);
                if (result_address == nullptr ||
                    !copy_aggregate_between_addresses(block, target_type,
                                                     result_address, source_address,
                                                     synthetic_index)) {
                    return false;
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            CoreIrValue *target_address = create_byte_offset_typed_address(
                block, source_address, 0, target_type, synthetic_index);
            if (target_address == nullptr) {
                return false;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrLoadInst>(target_type, result_name,
                                                         target_address),
                bindings);
        };

        auto lower_fp128_bitcast_sign_compare =
            [&](const AArch64LlvmImportTypedValue &reinterpreted_operand,
                CoreIrComparePredicate predicate) -> bool {
            if (predicate != CoreIrComparePredicate::SignedLess &&
                predicate != CoreIrComparePredicate::SignedGreaterEqual) {
                return false;
            }
            if (reinterpreted_operand.kind != AArch64LlvmImportValueKind::Local) {
                return false;
            }
            const auto binding_it = bindings.find(reinterpreted_operand.local_name);
            if (binding_it == bindings.end()) {
                return false;
            }
            const ValueBinding &binding = binding_it->second;
            if (binding.reinterpreted_source_value == nullptr ||
                binding.reinterpreted_source_type == nullptr ||
                binding.reinterpreted_target_type == nullptr ||
                !is_float_type(binding.reinterpreted_source_type) ||
                as_float_type(binding.reinterpreted_source_type)->get_float_kind() !=
                    CoreIrFloatKind::Float128) {
                return false;
            }
            const auto *target_integer =
                as_integer_type(binding.reinterpreted_target_type);
            if (target_integer == nullptr || target_integer->get_bit_width() != 128) {
                return false;
            }
            CoreIrStackSlot *storage = function.create_stack_slot<CoreIrStackSlot>(
                "ll.fp128.reinterpret." + std::to_string(synthetic_index++),
                binding.reinterpreted_source_type,
                get_storage_alignment(binding.reinterpreted_source_type));
            block.create_instruction<CoreIrStoreInst>(
                void_type(), binding.reinterpreted_source_value, storage);
            CoreIrValue *storage_address =
                materialize_stack_slot_address(block, storage, synthetic_index);
            const CoreIrType *i64_type = parse_type_text("i64");
            const CoreIrType *i1_type = parse_type_text("i1");
            if (storage_address == nullptr || i64_type == nullptr ||
                i1_type == nullptr) {
                return false;
            }
            CoreIrValue *high_word_address = create_byte_offset_typed_address(
                block, storage_address, 8, i64_type, synthetic_index);
            if (high_word_address == nullptr) {
                return false;
            }
            CoreIrValue *high_word = block.create_instruction<CoreIrLoadInst>(
                i64_type, "ll.fp128.signword." + std::to_string(synthetic_index++),
                high_word_address);
            const CoreIrConstant *zero =
                context_->create_constant<CoreIrConstantInt>(i64_type, 0);
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrCompareInst>(
                    predicate, i1_type, result_name, high_word,
                    const_cast<CoreIrConstant *>(zero)),
                bindings);
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
                const CoreIrType *source_type =
                    lower_import_type(cast_spec->source_type);
                const CoreIrType *target_type =
                    lower_import_type(cast_spec->target_type);
                if (source_type != nullptr && target_type != nullptr &&
                    import_type_to_core_float_kind(cast_spec->source_type) ==
                        std::optional<CoreIrFloatKind>(CoreIrFloatKind::Float128) &&
                    is_import_integer_bit_width(cast_spec->target_type, 128)) {
                    CoreIrValue *source_value = resolve_typed_value_operand(
                        source_type, cast_spec->source_value, block, bindings,
                        synthetic_index, line_number);
                    if (source_value == nullptr) {
                        return false;
                    }
                    const CoreIrType *reinterpret_target_type =
                        context_->create_type<CoreIrIntegerType>(128);
                    return bind_reinterpreted_result(result_name, source_value,
                                                    source_type,
                                                    reinterpret_target_type != nullptr
                                                        ? reinterpret_target_type
                                                        : target_type,
                                                    bindings);
                }
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
            (opcode_text == "fneg" || opcode_text == "freeze")) {
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
            if (opcode_text == "freeze") {
                if (!result_name.empty() &&
                    unary_spec->operand.kind ==
                        AArch64LlvmImportValueKind::Local) {
                    const auto binding_it =
                        bindings.find(unary_spec->operand.local_name);
                    if (binding_it != bindings.end()) {
                        bindings[result_name] = binding_it->second;
                        return true;
                    }
                }
                return result_name.empty()
                           ? true
                           : bind_instruction_result(result_name, operand,
                                                     bindings);
            }
            if (const auto *array_type = as_array_type(type); array_type != nullptr) {
                CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                    type, unary_spec->operand, function, block, bindings,
                    synthetic_index, line_number);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.unary." + std::to_string(synthetic_index++)
                            : result_name,
                        type, get_storage_alignment(type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot,
                                                   synthetic_index);
                if (source_address == nullptr || result_address == nullptr) {
                    return false;
                }
                for (std::size_t lane = 0; lane < array_type->get_element_count();
                     ++lane) {
                    CoreIrValue *source_element = create_element_address(
                        block, source_address, type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    CoreIrValue *destination_element = create_element_address(
                        block, result_address, type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    if (source_element == nullptr ||
                        destination_element == nullptr) {
                        return false;
                    }
                    CoreIrValue *source_value =
                        block.create_instruction<CoreIrLoadInst>(
                            array_type->get_element_type(),
                            "ll.vec.unary.src." +
                                std::to_string(synthetic_index++),
                            source_element);
                    CoreIrValue *lane_value =
                        block.create_instruction<CoreIrUnaryInst>(
                            CoreIrUnaryOpcode::Negate,
                            array_type->get_element_type(),
                            "ll.vec.unary.lane." +
                                std::to_string(synthetic_index++),
                            source_value);
                    block.create_instruction<CoreIrStoreInst>(
                        void_type(), lane_value, destination_element);
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
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
            const CoreIrType *operand_type =
                lower_import_type(compare_spec->lhs.type);
            const CoreIrType *i1_type = parse_type_text("i1");
            if (compare_spec->is_float_compare && i1_type != nullptr &&
                is_float_type(operand_type)) {
                CoreIrValue *lhs = resolve_typed_value_operand(
                    operand_type, compare_spec->lhs, block, bindings,
                    synthetic_index, line_number);
                CoreIrValue *rhs = resolve_typed_value_operand(
                    operand_type, compare_spec->rhs, block, bindings,
                    synthetic_index, line_number);
                if (lhs == nullptr || rhs == nullptr) {
                    return false;
                }
                auto make_float_cmp = [&](CoreIrComparePredicate predicate,
                                          CoreIrValue *lhs_value,
                                          CoreIrValue *rhs_value,
                                          const std::string &tag) -> CoreIrValue * {
                    return block.create_instruction<CoreIrCompareInst>(
                        predicate, i1_type,
                        tag + std::to_string(synthetic_index++), lhs_value,
                        rhs_value);
                };
                auto make_bool_binary = [&](CoreIrBinaryOpcode opcode,
                                            CoreIrValue *lhs_value,
                                            CoreIrValue *rhs_value,
                                            const std::string &tag) -> CoreIrValue * {
                    return block.create_instruction<CoreIrBinaryInst>(
                        opcode, i1_type, tag + std::to_string(synthetic_index++),
                        lhs_value, rhs_value);
                };
                auto make_i1_constant = [&](bool value) -> CoreIrConstant * {
                    return context_->create_constant<CoreIrConstantInt>(
                        i1_type, value ? 1 : 0);
                };

                CoreIrValue *eq_value = make_float_cmp(
                    CoreIrComparePredicate::Equal, lhs, rhs, "ll.fcmp.eq.");
                CoreIrValue *ne_value = make_float_cmp(
                    CoreIrComparePredicate::NotEqual, lhs, rhs, "ll.fcmp.ne.");
                CoreIrValue *lt_value = make_float_cmp(
                    CoreIrComparePredicate::SignedLess, lhs, rhs, "ll.fcmp.lt.");
                CoreIrValue *le_value = make_float_cmp(
                    CoreIrComparePredicate::SignedLessEqual, lhs, rhs,
                    "ll.fcmp.le.");
                CoreIrValue *gt_value = make_float_cmp(
                    CoreIrComparePredicate::SignedGreater, lhs, rhs,
                    "ll.fcmp.gt.");
                CoreIrValue *ge_value = make_float_cmp(
                    CoreIrComparePredicate::SignedGreaterEqual, lhs, rhs,
                    "ll.fcmp.ge.");
                CoreIrValue *lhs_nan = make_float_cmp(
                    CoreIrComparePredicate::NotEqual, lhs, lhs,
                    "ll.fcmp.uno.lhs.");
                CoreIrValue *rhs_nan = make_float_cmp(
                    CoreIrComparePredicate::NotEqual, rhs, rhs,
                    "ll.fcmp.uno.rhs.");
                CoreIrValue *unordered_value = make_bool_binary(
                    CoreIrBinaryOpcode::Or, lhs_nan, rhs_nan, "ll.fcmp.uno.or.");
                CoreIrValue *ordered_value = block.create_instruction<CoreIrCompareInst>(
                    CoreIrComparePredicate::Equal, i1_type,
                    "ll.fcmp.ord." + std::to_string(synthetic_index++),
                    unordered_value,
                    const_cast<CoreIrConstant *>(make_i1_constant(false)));

                CoreIrValue *result_value = nullptr;
                const std::string &predicate_text = compare_spec->predicate_text;
                if (predicate_text == "false") {
                    result_value = const_cast<CoreIrConstant *>(
                        make_i1_constant(false));
                } else if (predicate_text == "true") {
                    result_value = const_cast<CoreIrConstant *>(
                        make_i1_constant(true));
                } else if (predicate_text == "ord") {
                    result_value = ordered_value;
                } else if (predicate_text == "uno") {
                    result_value = unordered_value;
                } else if (predicate_text == "oeq") {
                    result_value = eq_value;
                } else if (predicate_text == "one") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::And, ordered_value, ne_value,
                        "ll.fcmp.one.and.");
                } else if (predicate_text == "olt") {
                    result_value = lt_value;
                } else if (predicate_text == "ole") {
                    result_value = le_value;
                } else if (predicate_text == "ogt") {
                    result_value = gt_value;
                } else if (predicate_text == "oge") {
                    result_value = ge_value;
                } else if (predicate_text == "ueq") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::Or, unordered_value, eq_value,
                        "ll.fcmp.ueq.or.");
                } else if (predicate_text == "une") {
                    result_value = ne_value;
                } else if (predicate_text == "ult") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::Or, unordered_value, lt_value,
                        "ll.fcmp.ult.or.");
                } else if (predicate_text == "ule") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::Or, unordered_value, le_value,
                        "ll.fcmp.ule.or.");
                } else if (predicate_text == "ugt") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::Or, unordered_value, gt_value,
                        "ll.fcmp.ugt.or.");
                } else if (predicate_text == "uge") {
                    result_value = make_bool_binary(
                        CoreIrBinaryOpcode::Or, unordered_value, ge_value,
                        "ll.fcmp.uge.or.");
                }
                if (result_value != nullptr) {
                    return bind_instruction_result(result_name, result_value,
                                                   bindings);
                }
            }
            std::optional<CoreIrComparePredicate> predicate =
                parse_compare_predicate(compare_spec->predicate_text,
                                        compare_spec->is_float_compare);
            if (compare_spec->is_same_sign && predicate.has_value()) {
                switch (*predicate) {
                case CoreIrComparePredicate::UnsignedLess:
                    predicate = CoreIrComparePredicate::SignedLess;
                    break;
                case CoreIrComparePredicate::UnsignedLessEqual:
                    predicate = CoreIrComparePredicate::SignedLessEqual;
                    break;
                case CoreIrComparePredicate::UnsignedGreater:
                    predicate = CoreIrComparePredicate::SignedGreater;
                    break;
                case CoreIrComparePredicate::UnsignedGreaterEqual:
                    predicate = CoreIrComparePredicate::SignedGreaterEqual;
                    break;
                default:
                    break;
                }
            }
            if (!predicate.has_value()) {
                if (compare_spec->is_float_compare &&
                    (compare_spec->predicate_text == "uno" ||
                     compare_spec->predicate_text == "ord")) {
                    const CoreIrType *i1_type = parse_type_text("i1");
                    const CoreIrType *operand_type =
                        lower_import_type(compare_spec->lhs.type);
                    CoreIrValue *lhs = resolve_typed_value_operand(
                        operand_type, compare_spec->lhs, block, bindings,
                        synthetic_index, line_number);
                    CoreIrValue *rhs = resolve_typed_value_operand(
                        operand_type, compare_spec->rhs, block, bindings,
                        synthetic_index, line_number);
                    if (i1_type == nullptr || operand_type == nullptr ||
                        lhs == nullptr || rhs == nullptr) {
                        add_error("unsupported LLVM compare predicate: " +
                                      compare_spec->predicate_text,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *lhs_nan = block.create_instruction<CoreIrCompareInst>(
                        CoreIrComparePredicate::NotEqual, i1_type,
                        "ll.fcmp.uno.lhs." + std::to_string(synthetic_index++),
                        lhs, lhs);
                    CoreIrValue *rhs_nan = block.create_instruction<CoreIrCompareInst>(
                        CoreIrComparePredicate::NotEqual, i1_type,
                        "ll.fcmp.uno.rhs." + std::to_string(synthetic_index++),
                        rhs, rhs);
                    CoreIrValue *unordered_value =
                        block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::Or, i1_type,
                            "ll.fcmp.uno.or." + std::to_string(synthetic_index++),
                            lhs_nan, rhs_nan);
                    if (compare_spec->predicate_text == "uno") {
                        return bind_instruction_result(result_name,
                                                       unordered_value, bindings);
                    }
                    CoreIrConstant *false_constant =
                        context_->create_constant<CoreIrConstantInt>(i1_type, 0);
                    return bind_instruction_result(
                        result_name,
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::Equal, i1_type, result_name,
                            unordered_value,
                            const_cast<CoreIrConstant *>(false_constant)),
                        bindings);
                }
                add_error("unsupported LLVM compare predicate: " +
                              compare_spec->predicate_text,
                          line_number, 1);
                return false;
            }
            if (is_import_zero_i128_constant(compare_spec->rhs) &&
                lower_fp128_bitcast_sign_compare(compare_spec->lhs, *predicate)) {
                return true;
            }
            if (compare_spec->lhs.type.kind == AArch64LlvmImportTypeKind::Integer &&
                compare_spec->lhs.type.integer_bit_width > 64 &&
                compare_spec->lhs.type.integer_bit_width != 128 &&
                (*predicate == CoreIrComparePredicate::Equal ||
                 *predicate == CoreIrComparePredicate::NotEqual)) {
                const auto lhs_wide = resolve_wide_integer_value(
                    compare_spec->lhs, block, bindings, synthetic_index);
                const auto rhs_wide = resolve_wide_integer_value(
                    compare_spec->rhs, block, bindings, synthetic_index);
                if (lhs_wide.has_value() && rhs_wide.has_value() &&
                    i1_type != nullptr) {
                    CoreIrValue *eq_value = materialize_wide_integer_eq_compare(
                        function, block, *lhs_wide, *rhs_wide,
                        compare_spec->lhs.type.integer_bit_width, synthetic_index);
                    if (eq_value == nullptr) {
                        add_error("unsupported LLVM wide integer compare: " + line,
                                  line_number, 1);
                        return false;
                    }
                    if (*predicate == CoreIrComparePredicate::Equal) {
                        return bind_instruction_result(result_name, eq_value,
                                                       bindings);
                    }
                    const CoreIrConstant *true_constant =
                        context_->create_constant<CoreIrConstantInt>(i1_type, 1);
                    return bind_instruction_result(
                        result_name,
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::NotEqual, i1_type,
                            result_name.empty()
                                ? "ll.wide.cmp.ne." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            eq_value, const_cast<CoreIrConstant *>(true_constant)),
                        bindings);
                }
            }
            CoreIrValue *lhs = resolve_typed_value_operand(
                operand_type, compare_spec->lhs, block, bindings,
                synthetic_index, line_number);
            CoreIrValue *rhs = resolve_typed_value_operand(
                operand_type, compare_spec->rhs, block, bindings,
                synthetic_index, line_number);
            if (lhs == nullptr || rhs == nullptr) {
                return false;
            }
            const auto *i128_array_type = as_array_type(operand_type);
            const auto *i128_element_type =
                i128_array_type == nullptr
                    ? nullptr
                    : as_integer_type(i128_array_type->get_element_type());
            if (is_import_integer_bit_width(compare_spec->lhs.type, 128) &&
                i1_type != nullptr && i128_array_type != nullptr &&
                i128_element_type != nullptr &&
                i128_array_type->get_element_count() == 2 &&
                i128_element_type->get_bit_width() == 64) {
                auto spill_i128_value = [&](CoreIrValue *value,
                                            const std::string &tag) -> CoreIrValue * {
                    CoreIrStackSlot *slot = function.create_stack_slot<CoreIrStackSlot>(
                        tag + std::to_string(synthetic_index++), operand_type,
                        get_storage_alignment(operand_type));
                    CoreIrValue *slot_address =
                        materialize_stack_slot_address(block, slot, synthetic_index);
                    if (slot_address == nullptr) {
                        return nullptr;
                    }
                    if (const auto *aggregate_constant =
                            dynamic_cast<const CoreIrConstantAggregate *>(value);
                        aggregate_constant != nullptr) {
                        if (!materialize_constant_aggregate_to_slot(
                                aggregate_constant, slot, block,
                                synthetic_index)) {
                            return nullptr;
                        }
                    } else if (dynamic_cast<const CoreIrConstantZeroInitializer *>(
                                   value) != nullptr) {
                        if (!materialize_zero_initializer_to_slot(
                                operand_type, slot, block, synthetic_index)) {
                            return nullptr;
                        }
                    } else {
                        block.create_instruction<CoreIrStoreInst>(void_type(), value,
                                                                  slot);
                    }
                    return slot_address;
                };
                auto load_i128_word = [&](CoreIrValue *address, std::size_t word_index,
                                          const std::string &tag) -> CoreIrValue * {
                    CoreIrValue *element_address = create_element_address(
                        block, address, operand_type,
                        static_cast<std::uint64_t>(word_index), synthetic_index);
                    return element_address == nullptr
                               ? nullptr
                               : block.create_instruction<CoreIrLoadInst>(
                                     i128_array_type->get_element_type(),
                                     tag + std::to_string(synthetic_index++),
                                     element_address);
                };
                auto make_i1_constant = [&](bool value) -> CoreIrConstant * {
                    return context_->create_constant<CoreIrConstantInt>(i1_type,
                                                                        value ? 1 : 0);
                };
                auto make_cmp = [&](CoreIrComparePredicate predicate,
                                    CoreIrValue *lhs_value,
                                    CoreIrValue *rhs_value,
                                    const std::string &tag) -> CoreIrValue * {
                    return block.create_instruction<CoreIrCompareInst>(
                        predicate, i1_type, tag + std::to_string(synthetic_index++),
                        lhs_value, rhs_value);
                };
                auto make_select = [&](CoreIrValue *condition,
                                       CoreIrValue *true_value,
                                       CoreIrValue *false_value,
                                       const std::string &tag) -> CoreIrValue * {
                    return block.create_instruction<CoreIrSelectInst>(
                        i1_type, tag + std::to_string(synthetic_index++), condition,
                        true_value, false_value);
                };
                CoreIrValue *lhs_address =
                    spill_i128_value(lhs, "ll.i128.cmp.lhs.");
                CoreIrValue *rhs_address =
                    spill_i128_value(rhs, "ll.i128.cmp.rhs.");
                if (lhs_address == nullptr || rhs_address == nullptr) {
                    return false;
                }
                CoreIrValue *lhs_low =
                    load_i128_word(lhs_address, 0, "ll.i128.cmp.l.low.");
                CoreIrValue *lhs_high =
                    load_i128_word(lhs_address, 1, "ll.i128.cmp.l.high.");
                CoreIrValue *rhs_low =
                    load_i128_word(rhs_address, 0, "ll.i128.cmp.r.low.");
                CoreIrValue *rhs_high =
                    load_i128_word(rhs_address, 1, "ll.i128.cmp.r.high.");
                if (lhs_low == nullptr || lhs_high == nullptr || rhs_low == nullptr ||
                    rhs_high == nullptr) {
                    return false;
                }
                CoreIrValue *high_eq =
                    make_cmp(CoreIrComparePredicate::Equal, lhs_high, rhs_high,
                             "ll.i128.cmp.h.eq.");
                CoreIrValue *low_eq =
                    make_cmp(CoreIrComparePredicate::Equal, lhs_low, rhs_low,
                             "ll.i128.cmp.l.eq.");
                CoreIrValue *result_value = nullptr;
                switch (*predicate) {
                case CoreIrComparePredicate::Equal:
                    result_value = make_select(
                        high_eq, low_eq, make_i1_constant(false),
                        result_name.empty() ? "ll.i128.cmp.eq." : result_name);
                    break;
                case CoreIrComparePredicate::NotEqual: {
                    CoreIrValue *low_ne = make_cmp(CoreIrComparePredicate::NotEqual,
                                                   lhs_low, rhs_low,
                                                   "ll.i128.cmp.l.ne.");
                    result_value = make_select(
                        high_eq, low_ne, make_i1_constant(true),
                        result_name.empty() ? "ll.i128.cmp.ne." : result_name);
                    break;
                }
                case CoreIrComparePredicate::SignedLess:
                case CoreIrComparePredicate::SignedLessEqual:
                case CoreIrComparePredicate::SignedGreater:
                case CoreIrComparePredicate::SignedGreaterEqual:
                case CoreIrComparePredicate::UnsignedLess:
                case CoreIrComparePredicate::UnsignedLessEqual:
                case CoreIrComparePredicate::UnsignedGreater:
                case CoreIrComparePredicate::UnsignedGreaterEqual: {
                    const bool is_signed =
                        *predicate == CoreIrComparePredicate::SignedLess ||
                        *predicate == CoreIrComparePredicate::SignedLessEqual ||
                        *predicate == CoreIrComparePredicate::SignedGreater ||
                        *predicate == CoreIrComparePredicate::SignedGreaterEqual;
                    const bool is_less =
                        *predicate == CoreIrComparePredicate::SignedLess ||
                        *predicate == CoreIrComparePredicate::SignedLessEqual ||
                        *predicate == CoreIrComparePredicate::UnsignedLess ||
                        *predicate == CoreIrComparePredicate::UnsignedLessEqual;
                    const bool or_equal =
                        *predicate == CoreIrComparePredicate::SignedLessEqual ||
                        *predicate ==
                            CoreIrComparePredicate::SignedGreaterEqual ||
                        *predicate == CoreIrComparePredicate::UnsignedLessEqual ||
                        *predicate ==
                            CoreIrComparePredicate::UnsignedGreaterEqual;
                    CoreIrValue *high_order_cmp = make_cmp(
                        is_less ? (is_signed ? CoreIrComparePredicate::SignedLess
                                             : CoreIrComparePredicate::UnsignedLess)
                                : (is_signed ? CoreIrComparePredicate::SignedGreater
                                             : CoreIrComparePredicate::UnsignedGreater),
                        lhs_high, rhs_high, "ll.i128.cmp.h.ord.");
                    CoreIrValue *low_cmp = make_cmp(
                        is_less ? (or_equal ? CoreIrComparePredicate::UnsignedLessEqual
                                            : CoreIrComparePredicate::UnsignedLess)
                                : (or_equal
                                       ? CoreIrComparePredicate::UnsignedGreaterEqual
                                       : CoreIrComparePredicate::UnsignedGreater),
                        lhs_low, rhs_low, "ll.i128.cmp.l.ord.");
                    result_value = make_select(
                        high_eq, low_cmp, high_order_cmp,
                        result_name.empty() ? "ll.i128.cmp.ord." : result_name);
                    break;
                }
                default:
                    break;
                }
                if (result_value == nullptr) {
                    add_error("unsupported LLVM i128 compare predicate: " + line,
                              line_number, 1);
                    return false;
                }
                return bind_instruction_result(result_name, result_value, bindings);
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
            if (is_supported_scalar_storage_type(result_type) &&
                !is_float_type(result_type) &&
                condition_type != nullptr &&
                dynamic_cast<const CoreIrIntegerType *>(condition_type) != nullptr) {
                return bind_instruction_result(
                    result_name,
                    block.create_instruction<CoreIrSelectInst>(
                        result_type, result_name, condition, true_value,
                        false_value),
                    bindings);
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
            if (is_i32x4_vector_type(vector_type)) {
                const auto *core_vector_type =
                    static_cast<const CoreIrVectorType *>(vector_type);
                const CoreIrType *index_type =
                    lower_import_type(extract_spec->index_value.type);
                CoreIrValue *vector_value = resolve_typed_value_operand(
                    vector_type, extract_spec->vector_value, block, bindings,
                    synthetic_index, line_number);
                CoreIrValue *index_value = resolve_typed_value_operand(
                    index_type, extract_spec->index_value, block, bindings,
                    synthetic_index, line_number);
                if (index_type == nullptr || vector_value == nullptr ||
                    index_value == nullptr) {
                    return false;
                }
                return bind_instruction_result(
                    result_name,
                    block.create_instruction<CoreIrExtractElementInst>(
                        core_vector_type->get_element_type(), result_name,
                        vector_value, index_value),
                    bindings);
            }
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
            if (is_i32x4_vector_type(vector_type)) {
                const CoreIrType *element_type =
                    lower_import_type(insert_spec->element_value.type);
                const CoreIrType *index_type =
                    lower_import_type(insert_spec->index_value.type);
                CoreIrValue *source_value = resolve_typed_value_operand(
                    vector_type, insert_spec->vector_value, block, bindings,
                    synthetic_index, line_number);
                CoreIrValue *element_value = resolve_typed_value_operand(
                    element_type, insert_spec->element_value, block,
                    bindings, synthetic_index, line_number);
                CoreIrValue *index_value = resolve_typed_value_operand(
                    index_type, insert_spec->index_value, block, bindings,
                    synthetic_index, line_number);
                if (element_type == nullptr || index_type == nullptr ||
                    source_value == nullptr || element_value == nullptr ||
                    index_value == nullptr) {
                    return false;
                }
                return bind_instruction_result(
                    result_name,
                    block.create_instruction<CoreIrInsertElementInst>(
                        vector_type, result_name, source_value, element_value,
                        index_value),
                    bindings);
            }
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
            if (is_i32x4_array_type(vector_type) &&
                is_lane0_zero_seed_constant_typed_value(insert_spec->vector_value) &&
                insert_spec->index_value.kind ==
                    AArch64LlvmImportValueKind::Constant &&
                insert_spec->index_value.constant.kind ==
                    AArch64LlvmImportConstantKind::Integer &&
                insert_spec->index_value.constant.integer_value == 0 &&
                !result_name.empty()) {
                CoreIrValue *element_value = resolve_typed_value_operand(
                    element_type, insert_spec->element_value, block, bindings,
                    synthetic_index, line_number);
                if (element_value == nullptr) {
                    return false;
                }
                ValueBinding binding;
                binding.lane0_zero_insert_scalar = element_value;
                binding.lane0_zero_insert_vector_type = vector_type;
                bindings[result_name] = binding;
                return true;
            }
            if (is_i32x4_array_type(vector_type) &&
                insert_spec->index_value.kind ==
                    AArch64LlvmImportValueKind::Constant &&
                insert_spec->index_value.constant.kind ==
                    AArch64LlvmImportConstantKind::Integer &&
                insert_spec->index_value.constant.integer_value >= 0 &&
                insert_spec->index_value.constant.integer_value < 4) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                CoreIrValue *source_address =
                    ensure_addressable_aggregate_typed_operand(
                        vector_type, insert_spec->vector_value, function,
                        block, bindings, synthetic_index, line_number);
                CoreIrValue *element_value = resolve_typed_value_operand(
                    element_type, insert_spec->element_value, block,
                    bindings, synthetic_index, line_number);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.insert." +
                                  std::to_string(synthetic_index++)
                            : result_name,
                        vector_type, get_storage_alignment(vector_type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot,
                                                   synthetic_index);
                if (ptr_type == nullptr || source_address == nullptr ||
                    element_value == nullptr || result_address == nullptr) {
                    return false;
                }
                const char *helper_name = nullptr;
                switch (insert_spec->index_value.constant.integer_value) {
                case 0:
                    helper_name = kInlineInsertLane0V4I32;
                    break;
                case 1:
                    helper_name = kInlineInsertLane1V4I32;
                    break;
                case 2:
                    helper_name = kInlineInsertLane2V4I32;
                    break;
                case 3:
                    helper_name = kInlineInsertLane3V4I32;
                    break;
                default:
                    break;
                }
                if (helper_name == nullptr) {
                    return false;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(),
                        std::vector<const CoreIrType *>{ptr_type, ptr_type,
                                                        element_type},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto callee_it = functions_.find(helper_name);
                    callee_it != functions_.end()) {
                    helper = callee_it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        helper_name, helper_type, false, false);
                    functions_[helper_name] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{result_address, source_address,
                                               element_value});
                return bind_stack_slot_result(result_name, result_slot, bindings);
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
            if (is_i32x4_vector_type(lhs_type) &&
                is_i32x4_vector_type(rhs_type) &&
                is_i32x4_vector_type(mask_type)) {
                const CoreIrConstant *mask_constant =
                    shuffle_spec->mask_value.kind ==
                            AArch64LlvmImportValueKind::Constant
                        ? lower_import_constant(mask_type,
                                                shuffle_spec->mask_value.constant)
                        : nullptr;
                std::vector<const CoreIrConstant *> mask_lanes;
                if (mask_constant == nullptr ||
                    !expand_vector_constant_lanes(mask_type, mask_constant,
                                                  mask_lanes) ||
                    mask_lanes.size() != 4) {
                    add_error("unsupported LLVM shufflevector mask: " + line,
                              line_number, 1);
                    return false;
                }
                bool only_lhs_lanes = true;
                std::vector<CoreIrValue *> lowered_mask_lanes;
                lowered_mask_lanes.reserve(mask_lanes.size());
                for (const CoreIrConstant *lane : mask_lanes) {
                    const auto *lane_int =
                        dynamic_cast<const CoreIrConstantInt *>(lane);
                    if (lane_int == nullptr || lane_int->get_value() >= 4) {
                        only_lhs_lanes = false;
                        break;
                    }
                    lowered_mask_lanes.push_back(
                        const_cast<CoreIrConstant *>(lane));
                }
                if (!only_lhs_lanes) {
                    add_error("unsupported non-lhs <4 x i32> shufflevector mask: " +
                                  line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *lhs_value = resolve_typed_value_operand(
                    lhs_type, shuffle_spec->lhs_value, block, bindings,
                    synthetic_index, line_number);
                CoreIrValue *rhs_value = resolve_typed_value_operand(
                    rhs_type, shuffle_spec->rhs_value, block, bindings,
                    synthetic_index, line_number);
                if (lhs_value == nullptr || rhs_value == nullptr) {
                    return false;
                }
                return bind_instruction_result(
                    result_name,
                    block.create_instruction<CoreIrShuffleVectorInst>(
                        lhs_type, result_name, lhs_value, rhs_value,
                        lowered_mask_lanes),
                    bindings);
            }
            const auto *lhs_array = as_array_type(lhs_type);
            const auto *rhs_array = as_array_type(rhs_type);
            const bool mask_is_native_v4i32 = is_i32x4_vector_type(mask_type);
            const CoreIrConstant *mask_constant =
                shuffle_spec->mask_value.kind == AArch64LlvmImportValueKind::Constant
                    ? lower_import_constant(mask_type,
                                            shuffle_spec->mask_value.constant)
                    : nullptr;
            std::vector<const CoreIrConstant *> mask_lanes;
            if (!expand_vector_constant_lanes(mask_type, mask_constant, mask_lanes)) {
                add_error("unsupported LLVM shufflevector mask: " + line,
                          line_number, 1);
                return false;
            }
            const bool is_lane0_splat_array =
                lhs_array != nullptr && mask_is_native_v4i32 &&
                is_zero_or_poison_constant_typed_value(shuffle_spec->rhs_value) &&
                lhs_array->get_element_count() == mask_lanes.size() &&
                std::all_of(mask_lanes.begin(), mask_lanes.end(),
                            [](const CoreIrConstant *lane) {
                                const auto *mask_element =
                                    dynamic_cast<const CoreIrConstantInt *>(lane);
                                return mask_element != nullptr &&
                                       mask_element->get_value() == 0;
                            });
            if (is_lane0_splat_array && !is_i32x4_array_type(lhs_type)) {
                CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                    lhs_type, shuffle_spec->lhs_value, function, block,
                    bindings, synthetic_index, line_number);
                if (lhs_address == nullptr) {
                    add_error("unsupported LLVM shufflevector operand types: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *source_element = create_element_address(
                    block, lhs_address, lhs_type, 0, synthetic_index);
                if (source_element == nullptr) {
                    return false;
                }
                CoreIrValue *lane_value = block.create_instruction<CoreIrLoadInst>(
                    lhs_array->get_element_type(),
                    "ll.vec.splat.load." + std::to_string(synthetic_index++),
                    source_element);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.splat." + std::to_string(synthetic_index++)
                            : result_name,
                        lhs_type, get_storage_alignment(lhs_type));
                CoreIrValue *result_address =
                    materialize_stack_slot_address(block, result_slot,
                                                   synthetic_index);
                if (lane_value == nullptr || result_address == nullptr) {
                    return false;
                }
                for (std::size_t lane = 0; lane < lhs_array->get_element_count();
                     ++lane) {
                    CoreIrValue *destination_element = create_element_address(
                        block, result_address, lhs_type,
                        static_cast<std::uint64_t>(lane), synthetic_index);
                    if (destination_element == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(
                        void_type(), lane_value, destination_element);
                }
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            if (lhs_array == nullptr || rhs_array == nullptr) {
                add_error("unsupported LLVM shufflevector operand types: " + line,
                          line_number, 1);
                return false;
            }
            if (mask_is_native_v4i32 &&
                is_zero_or_poison_constant_typed_value(shuffle_spec->rhs_value)) {
                const auto *lhs_element =
                    as_integer_type(lhs_array->get_element_type());
                if (lhs_element == nullptr || lhs_element->get_bit_width() != 32 ||
                    mask_lanes.size() != 4) {
                    add_error("unsupported LLVM shufflevector operand types: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                    lhs_type, shuffle_spec->lhs_value, function, block,
                    bindings, synthetic_index, line_number);
                if (lhs_address == nullptr) {
                    add_error("unsupported LLVM shufflevector operand types: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *result_value =
                    context_->create_constant<CoreIrConstantZeroInitializer>(
                        mask_type);
                for (std::size_t lane = 0; lane < mask_lanes.size(); ++lane) {
                    const auto *mask_element =
                        dynamic_cast<const CoreIrConstantInt *>(mask_lanes[lane]);
                    if (mask_element == nullptr ||
                        mask_element->get_value() >= lhs_array->get_element_count()) {
                        add_error("unsupported LLVM shufflevector mask: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *source_element = create_element_address(
                        block, lhs_address, lhs_type, mask_element->get_value(),
                        synthetic_index);
                    if (source_element == nullptr) {
                        return false;
                    }
                    CoreIrValue *loaded = block.create_instruction<CoreIrLoadInst>(
                        lhs_array->get_element_type(),
                        "ll.vec.shuffle.load." +
                            std::to_string(synthetic_index++),
                        source_element);
                    result_value = block.create_instruction<CoreIrInsertElementInst>(
                        mask_type,
                        "ll.vec.shuffle.insert." +
                            std::to_string(synthetic_index++),
                        result_value, loaded,
                        get_i32_constant(static_cast<std::uint64_t>(lane)));
                }
                return bind_instruction_result(result_name, result_value, bindings);
            }
            const bool is_lane0_splat_v4i32 =
                is_i32x4_array_type(lhs_type) &&
                lhs_array->get_element_count() == mask_lanes.size() &&
                std::all_of(mask_lanes.begin(), mask_lanes.end(),
                            [](const CoreIrConstant *lane) {
                                const auto *mask_element =
                                    dynamic_cast<const CoreIrConstantInt *>(lane);
                                return mask_element != nullptr &&
                                       mask_element->get_value() == 0;
                            });
            if (is_lane0_splat_v4i32) {
                if (shuffle_spec->lhs_value.kind == AArch64LlvmImportValueKind::Local) {
                    if (auto it = bindings.find(shuffle_spec->lhs_value.local_name);
                        it != bindings.end() &&
                        ((it->second.lane0_splat_scalar != nullptr &&
                          it->second.lane0_splat_vector_type == lhs_type) ||
                         (it->second.lane0_zero_insert_scalar != nullptr &&
                          it->second.lane0_zero_insert_vector_type == lhs_type))) {
                        CoreIrValue *scalar_value =
                            it->second.lane0_splat_scalar != nullptr
                                ? it->second.lane0_splat_scalar
                                : it->second.lane0_zero_insert_scalar;
                        const CoreIrType *ptr_type = parse_type_text("ptr");
                        if (ptr_type == nullptr) {
                            return false;
                        }
                        ValueBinding binding;
                        binding.lane0_splat_scalar = scalar_value;
                        binding.lane0_splat_vector_type = lhs_type;
                        bindings[result_name] = binding;
                        return true;
                    }
                }
                CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                    lhs_type, shuffle_spec->lhs_value, function, block,
                    bindings, synthetic_index, line_number);
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return false;
                }
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        result_name.empty()
                            ? "ll.vec.splat." + std::to_string(synthetic_index++)
                            : result_name,
                        lhs_type, get_storage_alignment(lhs_type));
                CoreIrValue *result_address = materialize_stack_slot_address(
                    block, result_slot, synthetic_index);
                if (result_address == nullptr) {
                    return false;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        void_type(), std::vector<const CoreIrType *>{ptr_type, ptr_type},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto it = functions_.find(kInlineSplatLane0V4I32);
                    it != functions_.end()) {
                    helper = it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineSplatLane0V4I32, helper_type, false, false);
                    functions_[kInlineSplatLane0V4I32] = helper;
                }
                block.create_instruction<CoreIrCallInst>(
                    void_type(), "", helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{result_address, lhs_address});
                return bind_stack_slot_result(result_name, result_slot, bindings);
            }
            CoreIrValue *lhs_address = ensure_addressable_aggregate_typed_operand(
                lhs_type, shuffle_spec->lhs_value, function, block,
                bindings, synthetic_index, line_number);
            CoreIrValue *rhs_address = ensure_addressable_aggregate_typed_operand(
                rhs_type, shuffle_spec->rhs_value, function, block,
                bindings, synthetic_index, line_number);
            if (lhs_address == nullptr || rhs_address == nullptr) {
                add_error("unsupported LLVM shufflevector operand types: " + line,
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
            for (std::size_t lane = 0; lane < mask_lanes.size(); ++lane) {
                const auto *mask_element =
                    dynamic_cast<const CoreIrConstantInt *>(mask_lanes[lane]);
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
            if (is_i32x4_vector_type(vector_type)) {
                CoreIrValue *vector_value = resolve_typed_value_operand(
                    vector_type, reduce_spec->vector_value, block, bindings,
                    synthetic_index, line_number);
                if (return_type == nullptr || vector_value == nullptr) {
                    return false;
                }
                CoreIrValue *call_value =
                    block.create_instruction<CoreIrVectorReduceAddInst>(
                        return_type, result_name, vector_value);
                return bind_instruction_result(result_name, call_value, bindings);
            }
            const auto *array_type = as_array_type(vector_type);
            if (array_type == nullptr) {
                add_error("unsupported LLVM vector reduce operand: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *vector_address = ensure_addressable_aggregate_typed_operand(
                vector_type, reduce_spec->vector_value, function,
                block, bindings, synthetic_index, line_number);
            if (vector_address == nullptr) {
                add_error("unsupported LLVM vector reduce operand: " + line,
                          line_number, 1);
                return false;
            }
            if (is_i32x4_array_type(vector_type)) {
                const CoreIrType *ptr_type = parse_type_text("ptr");
                if (ptr_type == nullptr) {
                    return false;
                }
                const CoreIrFunctionType *helper_type =
                    context_->create_type<CoreIrFunctionType>(
                        return_type, std::vector<const CoreIrType *>{ptr_type},
                        false);
                CoreIrFunction *helper = nullptr;
                if (auto it = functions_.find(kInlineReduceAddV4I32);
                    it != functions_.end()) {
                    helper = it->second;
                } else {
                    helper = module_->create_function<CoreIrFunction>(
                        kInlineReduceAddV4I32, helper_type, false, false);
                    functions_[kInlineReduceAddV4I32] = helper;
                }
                CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                    return_type, result_name, helper->get_name(), helper_type,
                    std::vector<CoreIrValue *>{vector_address});
                return bind_instruction_result(result_name, call_value, bindings);
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
            if (alloca_spec->element_count.has_value()) {
                const CoreIrType *count_type =
                    lower_import_type(alloca_spec->element_count->type);
                CoreIrValue *count_value = resolve_typed_value_operand(
                    count_type, *alloca_spec->element_count, block, bindings,
                    synthetic_index, line_number);
                if (count_value == nullptr) {
                    add_error("unsupported LLVM alloca count operand: " + line,
                              line_number, 1);
                    return false;
                }
                if (const auto *count_constant =
                        dynamic_cast<CoreIrConstantInt *>(count_value);
                    count_constant != nullptr) {
                    const std::uint64_t element_count = count_constant->get_value();
                    const CoreIrType *stack_type =
                        element_count <= 1
                            ? allocated_type
                            : context_->create_type<CoreIrArrayType>(
                                  allocated_type,
                                  static_cast<std::size_t>(element_count));
                    ValueBinding binding;
                    binding.stack_slot = function.create_stack_slot<CoreIrStackSlot>(
                        result_name, stack_type, alloca_spec->alignment);
                    bindings[result_name] = binding;
                    return true;
                }

                const auto *count_integer_type =
                    dynamic_cast<const CoreIrIntegerType *>(count_type);
                const CoreIrType *i64_type = parse_type_text("i64");
                const CoreIrType *opaque_ptr_type = parse_type_text("ptr");
                if (count_integer_type == nullptr || i64_type == nullptr ||
                    opaque_ptr_type == nullptr) {
                    add_error("unsupported dynamic LLVM alloca count type: " + line,
                              line_number, 1);
                    return false;
                }
                if (count_integer_type->get_bit_width() != 64) {
                    count_value = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::ZeroExtend, i64_type,
                        "ll.alloca.count.zext." + std::to_string(synthetic_index++),
                        count_value);
                }
                const CoreIrConstant *element_size =
                    context_->create_constant<CoreIrConstantInt>(
                        i64_type, get_type_size(allocated_type));
                CoreIrValue *byte_count = block.create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Mul, i64_type,
                    "ll.alloca.bytes." + std::to_string(synthetic_index++),
                    count_value, const_cast<CoreIrConstant *>(element_size));
                const CoreIrFunctionType *malloc_type =
                    context_->create_type<CoreIrFunctionType>(
                        opaque_ptr_type, std::vector<const CoreIrType *>{i64_type},
                        false);
                CoreIrFunction *malloc_function =
                    get_or_create_imported_declaration("malloc", malloc_type);
                CoreIrValue *malloc_ptr = block.create_instruction<CoreIrCallInst>(
                    opaque_ptr_type, result_name, malloc_function->get_name(),
                    malloc_type, std::vector<CoreIrValue *>{byte_count});
                retype_opaque_pointer_value(malloc_ptr, allocated_type);
                if (!bind_instruction_result(result_name, malloc_ptr, bindings)) {
                    return false;
                }
                if (!dynamic_alloca_scopes_.empty()) {
                    dynamic_alloca_scopes_.back().push_back(malloc_ptr);
                }
                return true;
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
            if (load_spec->load_type.kind == AArch64LlvmImportTypeKind::Integer &&
                load_spec->load_type.integer_bit_width > 64 &&
                load_spec->load_type.integer_bit_width != 128) {
                CoreIrValue *source_address =
                    address.stack_slot != nullptr
                        ? materialize_stack_slot_address(block, address.stack_slot,
                                                         synthetic_index)
                        : address.address_value;
                if (source_address == nullptr) {
                    add_error("unsupported LLVM wide integer load: " + line,
                              line_number, 1);
                    return false;
                }
                ValueBinding::WideIntegerValue wide_integer;
                wide_integer.source_address = source_address;
                wide_integer.source_bit_width =
                    load_spec->load_type.integer_bit_width;
                return bind_wide_integer_result(result_name, wide_integer,
                                                bindings);
            }
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
            std::optional<AArch64LlvmImportStoreSpec> store_spec =
                parse_llvm_import_store_spec(instruction);
            AArch64LlvmImportStoreSpec fallback_store_spec;
            if (!store_spec.has_value()) {
                const std::vector<std::string> operands =
                    split_top_level(trim_copy(instruction_text.substr(5)), ',');
                if (operands.size() >= 2) {
                    const auto value = parse_typed_value_text(operands[0]);
                    const auto address = parse_typed_value_text(operands[1]);
                    if (value.has_value() && address.has_value()) {
                        fallback_store_spec.value = *value;
                        fallback_store_spec.address = *address;
                        if (operands.size() > 2) {
                            fallback_store_spec.alignment =
                                parse_optional_alignment_text(operands[2]);
                        }
                        store_spec = fallback_store_spec;
                    }
                }
            }
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
            if (store_spec->address.type.kind !=
                AArch64LlvmImportTypeKind::Pointer) {
                add_error("unsupported LLVM store address operand: " +
                              store_spec->address.type_text,
                          line_number, 1);
                return false;
            }
            ResolvedAddress address = resolve_typed_address_operand(
                store_spec->address, block, bindings, synthetic_index,
                line_number);
            if (store_spec->value.type.kind == AArch64LlvmImportTypeKind::Integer &&
                store_spec->value.type.integer_bit_width > 64 &&
                store_spec->value.type.integer_bit_width != 128) {
                CoreIrValue *destination_address =
                    address.stack_slot != nullptr
                        ? materialize_stack_slot_address(block, address.stack_slot,
                                                         synthetic_index)
                        : address.address_value;
                if (destination_address == nullptr) {
                    add_error("unsupported LLVM wide integer store address: " +
                                  line,
                              line_number, 1);
                    return false;
                }
                const auto wide_integer = resolve_wide_integer_value(
                    store_spec->value, block, bindings, synthetic_index);
                if (wide_integer.has_value()) {
                    if (!materialize_wide_integer_to_address(
                            block, *wide_integer, destination_address,
                            synthetic_index)) {
                        add_error("failed to materialize LLVM wide integer store: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    return true;
                }
            }
            if (as_array_type(value_type) != nullptr) {
                CoreIrValue *destination_address =
                    address.stack_slot != nullptr
                        ? materialize_stack_slot_address(block, address.stack_slot,
                                                         synthetic_index)
                        : address.address_value;
                if (destination_address == nullptr) {
                    return false;
                }
                if (store_spec->value.kind == AArch64LlvmImportValueKind::Constant) {
                    const CoreIrConstant *constant =
                        lower_import_constant(value_type, store_spec->value.constant);
                    if (const auto *aggregate =
                            dynamic_cast<const CoreIrConstantAggregate *>(constant);
                        aggregate != nullptr) {
                        return materialize_constant_aggregate_to_address(
                            aggregate, value_type, destination_address, block,
                            synthetic_index);
                    }
                    if (dynamic_cast<const CoreIrConstantZeroInitializer *>(
                            constant) != nullptr) {
                        return materialize_zero_initializer_to_address(
                            value_type, destination_address, block,
                            synthetic_index);
                    }
                }
                if (try_materialize_special_aggregate_binding_to_address(
                        value_type, store_spec->value, block, bindings,
                        destination_address, line_number)) {
                    return true;
                }
                CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                    value_type, store_spec->value,
                    function, block, bindings, synthetic_index, line_number);
                if (source_address == nullptr ||
                    !copy_aggregate_between_addresses(block, value_type,
                                                     destination_address,
                                                     source_address,
                                                     synthetic_index)) {
                    return false;
                }
                return true;
            }
            CoreIrValue *value = resolve_typed_value_operand(
                value_type, store_spec->value, block, bindings,
                synthetic_index, line_number);
            if (value == nullptr) {
                add_error("unsupported LLVM store value operand: " +
                              describe_typed_operand(store_spec->value),
                          line_number, 1);
                return false;
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
                    const CoreIrType *i64_type = parse_type_text("i64");
                    CoreIrValue *base_int = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::PtrToInt, i64_type,
                        "ll.gep.retype.ptrtoint." + std::to_string(synthetic_index++),
                        base);
                    base = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::IntToPtr, pointer_to(source_type),
                        "ll.gep.retype.inttoptr." + std::to_string(synthetic_index++),
                        base_int);
                } else {
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
            }
            retype_opaque_pointer_value(base, source_type);
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrGetElementPtrInst>(
                    pointer_to(result_pointee), result_name, base, indices),
                bindings);
        }
        if (instruction_kind == AArch64LlvmImportInstructionKind::Call) {
            if (starts_with(instruction_text, "call void asm sideeffect \"\"") ||
                starts_with(instruction_text, "call void asm \"\"")) {
                return true;
            }
            if (instruction_text.find(" asm ") != std::string::npos &&
                instruction_text.find("\"\"") != std::string::npos &&
                instruction_text.find("\"(") != std::string::npos) {
                const std::string payload = trim_copy(instruction_text.substr(5));
                std::size_t return_type_position = 0;
                const std::optional<std::string> return_type_text =
                    consume_type_token(payload, return_type_position);
                const std::size_t constraint_begin =
                    payload.find("\", \"");
                const std::size_t constraint_end = payload.find("\"(", constraint_begin);
                const std::size_t passthrough_end = payload.rfind(')');
                if (!return_type_text.has_value() ||
                    constraint_begin == std::string::npos ||
                    constraint_end == std::string::npos ||
                    passthrough_end == std::string::npos ||
                    passthrough_end < constraint_end + 2) {
                    add_error("unsupported LLVM inline asm passthrough call: " + line,
                              line_number, 1);
                    return false;
                }
                const auto parsed_return_type =
                    parse_llvm_import_type_text(*return_type_text);
                const std::string constraint_text = payload.substr(
                    constraint_begin + 4, constraint_end - (constraint_begin + 4));
                const std::vector<std::string> arguments = split_top_level(
                    payload.substr(constraint_end + 2,
                                   passthrough_end - (constraint_end + 2)),
                    ',');
                const std::vector<std::string> constraint_parts =
                    split_top_level(constraint_text, ',');
                std::size_t output_count = 0;
                while (output_count < constraint_parts.size() &&
                       !constraint_parts[output_count].empty() &&
                       constraint_parts[output_count][0] == '=') {
                    ++output_count;
                }
                std::size_t tied_input_index = static_cast<std::size_t>(-1);
                for (std::size_t index = output_count; index < constraint_parts.size();
                     ++index) {
                    if (constraint_parts[index] == "0") {
                        tied_input_index = index - output_count;
                        break;
                    }
                    if (!constraint_parts[index].empty() &&
                        constraint_parts[index][0] == '~') {
                        break;
                    }
                }
                if (!parsed_return_type.has_value() || arguments.empty() ||
                    tied_input_index == static_cast<std::size_t>(-1) ||
                    tied_input_index >= arguments.size()) {
                    add_error("unsupported LLVM inline asm passthrough call: " + line,
                              line_number, 1);
                    return false;
                }
                const CoreIrType *return_type =
                    lower_import_type(*parsed_return_type);
                const auto passthrough_operand =
                    parse_typed_value_text(arguments[tied_input_index]);
                CoreIrValue *passthrough_value =
                    passthrough_operand.has_value()
                        ? resolve_typed_value_operand(
                              return_type, *passthrough_operand, block,
                              bindings, synthetic_index, line_number)
                        : nullptr;
                if (return_type == nullptr || passthrough_value == nullptr) {
                    add_error("unsupported LLVM inline asm passthrough operand: " +
                                  line,
                              line_number, 1);
                    return false;
                }
                return bind_instruction_result(result_name, passthrough_value,
                                               bindings);
            }
            std::optional<AArch64LlvmImportCallSpec> call_spec =
                parse_llvm_import_call_spec(instruction);
            AArch64LlvmImportCallSpec fallback_call_spec;
            if (!call_spec.has_value()) {
                const std::string payload =
                    strip_leading_modifiers(trim_copy(instruction_text.substr(5)));
                std::size_t return_type_position = 0;
                const std::optional<std::string> return_type_text =
                    consume_type_token(payload, return_type_position);
                if (return_type_text.has_value()) {
                    std::size_t callee_position = return_type_position;
                    while (callee_position < payload.size() &&
                           std::isspace(static_cast<unsigned char>(
                               payload[callee_position])) != 0) {
                        ++callee_position;
                    }
                    if (callee_position < payload.size() &&
                        payload[callee_position] == '(') {
                        int depth = 0;
                        do {
                            if (payload[callee_position] == '(') {
                                ++depth;
                            } else if (payload[callee_position] == ')') {
                                --depth;
                            }
                            ++callee_position;
                        } while (callee_position < payload.size() && depth > 0);
                        while (callee_position < payload.size() &&
                               (std::isspace(static_cast<unsigned char>(
                                    payload[callee_position])) != 0 ||
                                payload[callee_position] == '*')) {
                            ++callee_position;
                        }
                    }
                    const std::size_t open_paren_pos =
                        payload.find('(', callee_position);
                    const std::size_t close_paren_pos = payload.rfind(')');
                    const auto parsed_return_type =
                        parse_llvm_import_type_text(*return_type_text);
                    if (parsed_return_type.has_value() &&
                        open_paren_pos != std::string::npos &&
                        close_paren_pos != std::string::npos &&
                        close_paren_pos >= open_paren_pos) {
                        const std::string callee_text = trim_copy(payload.substr(
                            callee_position, open_paren_pos - callee_position));
                        AArch64LlvmImportTypedValue callee;
                        if (!callee_text.empty() && callee_text.front() == '@') {
                            callee.kind = AArch64LlvmImportValueKind::Global;
                            callee.global_name = callee_text.substr(1);
                            callee.type_text = "ptr";
                            callee.type.kind =
                                AArch64LlvmImportTypeKind::Pointer;
                        } else if (!callee_text.empty() &&
                                   callee_text.front() == '%') {
                            callee.kind = AArch64LlvmImportValueKind::Local;
                            callee.local_name = callee_text.substr(1);
                            callee.type_text = "ptr";
                            callee.type.kind =
                                AArch64LlvmImportTypeKind::Pointer;
                        }
                        if (callee.is_valid()) {
                            fallback_call_spec.return_type_text = *return_type_text;
                            fallback_call_spec.return_type = *parsed_return_type;
                            fallback_call_spec.callee = callee;
                            auto parse_argument_fallback =
                                [&](const std::string &argument_entry)
                                -> std::optional<AArch64LlvmImportTypedValue> {
                                const auto parsed_argument =
                                    parse_typed_value_text(argument_entry);
                                if (parsed_argument.has_value()) {
                                    return parsed_argument;
                                }
                                std::size_t type_position = 0;
                                const std::optional<std::string> argument_type_text =
                                    consume_type_token(argument_entry,
                                                       type_position);
                                if (!argument_type_text.has_value()) {
                                    return std::nullopt;
                                }
                                const auto argument_type =
                                    parse_llvm_import_type_text(
                                        *argument_type_text);
                                if (!argument_type.has_value() ||
                                    argument_type->kind !=
                                        AArch64LlvmImportTypeKind::Pointer) {
                                    return std::nullopt;
                                }
                                const std::string remainder = trim_copy(
                                    argument_entry.substr(type_position));
                                const std::size_t local_pos =
                                    remainder.rfind('%');
                                const std::size_t global_pos =
                                    remainder.rfind('@');
                                const std::size_t ref_pos =
                                    local_pos == std::string::npos
                                        ? global_pos
                                        : (global_pos == std::string::npos
                                               ? local_pos
                                               : std::max(local_pos, global_pos));
                                if (ref_pos == std::string::npos ||
                                    ref_pos + 1 >= remainder.size()) {
                                    return std::nullopt;
                                }
                                const std::string ref_text =
                                    trim_copy(remainder.substr(ref_pos));
                                AArch64LlvmImportTypedValue parsed;
                                parsed.type_text = *argument_type_text;
                                parsed.type = *argument_type;
                                parsed.value_text = ref_text;
                                if (ref_text.front() == '%') {
                                    parsed.kind = AArch64LlvmImportValueKind::Local;
                                    parsed.local_name = ref_text.substr(1);
                                    return parsed;
                                }
                                if (ref_text.front() == '@') {
                                    parsed.kind = AArch64LlvmImportValueKind::Global;
                                    parsed.global_name = ref_text.substr(1);
                                    return parsed;
                                }
                                return std::nullopt;
                            };
                            bool arguments_ok = true;
                            for (const std::string &argument_entry :
                                 split_top_level(payload.substr(
                                                     open_paren_pos + 1,
                                                     close_paren_pos -
                                                         open_paren_pos - 1),
                                                 ',')) {
                                if (argument_entry.empty()) {
                                    continue;
                                }
                                const auto argument =
                                    parse_argument_fallback(argument_entry);
                                if (!argument.has_value()) {
                                    arguments_ok = false;
                                    break;
                                }
                                fallback_call_spec.arguments.push_back(*argument);
                            }
                            if (arguments_ok) {
                                call_spec = fallback_call_spec;
                            }
                        }
                    }
                }
            }
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
            if (call_spec->callee.kind == AArch64LlvmImportValueKind::Global) {
                const std::string &callee_name = call_spec->callee.global_name;
                if (callee_name.rfind("llvm.is.constant.", 0) == 0) {
                    if (call_spec->arguments.size() != 1 ||
                        dynamic_cast<const CoreIrIntegerType *>(return_type) ==
                            nullptr) {
                        add_error("unsupported llvm.is.constant call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    const AArch64LlvmImportTypedValue &argument =
                        call_spec->arguments.front();
                    if (argument.kind == AArch64LlvmImportValueKind::Local) {
                        if (auto it = bindings.find(argument.local_name);
                            it != bindings.end() && it->second.value != nullptr) {
                            prune_dead_constant_probe_chain(block,
                                                            it->second.value);
                        }
                    }
                    const std::uint64_t value =
                        argument.kind == AArch64LlvmImportValueKind::Constant ||
                                argument.kind ==
                                    AArch64LlvmImportValueKind::ConstantExpressionRaw
                            ? 1ULL
                            : 0ULL;
                    const CoreIrConstant *folded =
                        context_->create_constant<CoreIrConstantInt>(return_type,
                                                                     value);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(
                                     result_name,
                                     const_cast<CoreIrConstant *>(folded),
                                     bindings);
                }
                if (callee_name == "llvm.smul.with.overflow.i129") {
                    const CoreIrType *i1_type = parse_type_text("i1");
                    const CoreIrType *i64_type = parse_type_text("i64");
                    const CoreIrType *i128_type =
                        context_->create_type<CoreIrArrayType>(i64_type, 2);
                    auto build_i128_from_i64 =
                        [&](CoreIrValue *value, bool sign_extend,
                            const std::string &tag) -> CoreIrValue * {
                        if (value == nullptr || i64_type == nullptr ||
                            i128_type == nullptr) {
                            return nullptr;
                        }
                        CoreIrStackSlot *slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                tag + std::to_string(synthetic_index++), i128_type,
                                get_storage_alignment(i128_type));
                        CoreIrValue *slot_address = materialize_stack_slot_address(
                            block, slot, synthetic_index);
                        CoreIrValue *low_address = create_element_address(
                            block, slot_address, i128_type, 0, synthetic_index);
                        CoreIrValue *high_address = create_element_address(
                            block, slot_address, i128_type, 1, synthetic_index);
                        if (slot_address == nullptr || low_address == nullptr ||
                            high_address == nullptr) {
                            return nullptr;
                        }
                        CoreIrValue *low_word = value;
                        CoreIrValue *high_word = nullptr;
                        if (sign_extend) {
                            const CoreIrConstant *shift_constant =
                                context_->create_constant<CoreIrConstantInt>(
                                    i64_type, 63);
                            high_word = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::AShr, i64_type,
                                tag + ".high." +
                                    std::to_string(synthetic_index++),
                                value, const_cast<CoreIrConstant *>(shift_constant));
                        } else {
                            high_word = context_->create_constant<CoreIrConstantInt>(
                                i64_type, 0);
                        }
                        block.create_instruction<CoreIrStoreInst>(void_type(),
                                                                  low_word,
                                                                  low_address);
                        block.create_instruction<CoreIrStoreInst>(void_type(),
                                                                  high_word,
                                                                  high_address);
                        return block.create_instruction<CoreIrLoadInst>(
                            i128_type,
                            tag + ".load." + std::to_string(synthetic_index++),
                            slot_address);
                    };
                    auto materialize_small_signed_operand =
                        [&](const AArch64LlvmImportTypedValue &typed_operand,
                            const std::string &tag) -> CoreIrValue * {
                        if (typed_operand.kind == AArch64LlvmImportValueKind::Constant) {
                            AArch64LlvmImportType i128_import_type;
                            i128_import_type.kind =
                                AArch64LlvmImportTypeKind::Integer;
                            i128_import_type.integer_bit_width = 128;
                            AArch64LlvmImportTypedValue i128_typed_operand;
                            i128_typed_operand.type_text = "i128";
                            i128_typed_operand.type = i128_import_type;
                            i128_typed_operand.value_text = typed_operand.value_text;
                            const auto parsed_constant = parse_llvm_import_constant_text(
                                i128_import_type, typed_operand.value_text);
                            if (!parsed_constant.has_value()) {
                                return nullptr;
                            }
                            i128_typed_operand.kind =
                                AArch64LlvmImportValueKind::Constant;
                            i128_typed_operand.constant = *parsed_constant;
                            CoreIrValue *constant_address =
                                ensure_addressable_aggregate_typed_operand(
                                    i128_type, i128_typed_operand, function, block,
                                    bindings, synthetic_index, line_number);
                            return constant_address == nullptr
                                       ? nullptr
                                       : block.create_instruction<CoreIrLoadInst>(
                                             i128_type,
                                             tag + ".const.load." +
                                                 std::to_string(synthetic_index++),
                                             constant_address);
                        }
                        if (typed_operand.type.kind ==
                                AArch64LlvmImportTypeKind::Integer &&
                            typed_operand.type.integer_bit_width <= 64) {
                            const CoreIrType *small_type =
                                lower_import_type(typed_operand.type);
                            CoreIrValue *operand = resolve_typed_value_operand(
                                small_type, typed_operand, block, bindings,
                                synthetic_index, line_number);
                            if (operand == nullptr || i64_type == nullptr) {
                                return nullptr;
                            }
                            CoreIrValue *wide64 = operand;
                            if (typed_operand.type.integer_bit_width < 64) {
                                wide64 = block.create_instruction<CoreIrCastInst>(
                                    CoreIrCastKind::SignExtend, i64_type,
                                    tag + ".sext." +
                                        std::to_string(synthetic_index++),
                                    operand);
                            }
                            return build_i128_from_i64(wide64, true, tag);
                        }
                        const auto wide_operand = resolve_wide_integer_value(
                            typed_operand, block, bindings, synthetic_index);
                        if (!wide_operand.has_value() ||
                            wide_operand->source_bit_width > 64 ||
                            !wide_operand->shift_ops.empty() ||
                            !wide_operand->bitwise_constant_ops.empty()) {
                            return nullptr;
                        }
                        CoreIrValue *wide64 = materialize_truncated_wide_integer_value(
                            block, *wide_operand, i64_type, synthetic_index);
                        if (wide64 == nullptr) {
                            return nullptr;
                        }
                        const bool sign_extend =
                            wide_operand->high_bit_fill ==
                            ValueBinding::WideIntegerValue::HighBitFill::Sign;
                        return build_i128_from_i64(wide64, sign_extend, tag);
                    };
                    if (call_spec->arguments.size() != 2 || i1_type == nullptr ||
                        i64_type == nullptr || i128_type == nullptr) {
                        add_error("unsupported llvm.smul.with.overflow.i129 call shape: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *lhs_i128 = materialize_small_signed_operand(
                        call_spec->arguments[0], "ll.smulov129.lhs.");
                    CoreIrValue *rhs_i128 = materialize_small_signed_operand(
                        call_spec->arguments[1], "ll.smulov129.rhs.");
                    if (lhs_i128 == nullptr || rhs_i128 == nullptr) {
                        add_error("unsupported llvm.smul.with.overflow.i129 operands: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    const CoreIrFunctionType *multi3_type =
                        context_->create_type<CoreIrFunctionType>(
                            i128_type,
                            std::vector<const CoreIrType *>{i128_type, i128_type},
                            false);
                    CoreIrFunction *multi3 =
                        get_or_create_imported_declaration("__multi3", multi3_type);
                    CoreIrValue *product = block.create_instruction<CoreIrCallInst>(
                        i128_type,
                        "ll.smulov129.mul." + std::to_string(synthetic_index++),
                        multi3->get_name(), multi3_type,
                        std::vector<CoreIrValue *>{lhs_i128, rhs_i128});
                    CoreIrStackSlot *product_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            "ll.smulov129.prod." +
                                std::to_string(synthetic_index++),
                            i128_type, get_storage_alignment(i128_type));
                    block.create_instruction<CoreIrStoreInst>(void_type(), product,
                                                              product_slot);
                    CoreIrValue *product_address = materialize_stack_slot_address(
                        block, product_slot, synthetic_index);
                    if (product_address == nullptr) {
                        return false;
                    }
                    ValueBinding::WideOverflowAggregate aggregate_result;
                    aggregate_result.wide_value.source_address = product_address;
                    aggregate_result.wide_value.source_bit_width = 128;
                    aggregate_result.wide_value.high_bit_fill =
                        ValueBinding::WideIntegerValue::HighBitFill::Sign;
                    aggregate_result.overflow_flag =
                        context_->create_constant<CoreIrConstantInt>(i1_type, 0);
                    return bind_wide_overflow_aggregate_result(
                        result_name, aggregate_result, bindings);
                }
            }
            std::vector<CoreIrValue *> arguments;
            std::vector<const CoreIrType *> argument_types;
            std::vector<CoreIrValue *> aggregate_argument_addresses;
            for (const AArch64LlvmImportTypedValue &argument :
                 call_spec->arguments) {
                const CoreIrType *argument_type =
                    lower_import_type(argument.type);
                if (argument_type == nullptr) {
                    add_error("unsupported LLVM call argument: " +
                                  argument.type_text + " " +
                                  describe_typed_operand(argument),
                              line_number, 1);
                    return false;
                }
                CoreIrValue *argument_value = nullptr;
                CoreIrValue *aggregate_argument_address = nullptr;
                if (is_aggregate_type(argument_type)) {
                    aggregate_argument_address =
                        ensure_addressable_aggregate_typed_operand(
                            argument_type, argument, function, block, bindings,
                            synthetic_index, line_number);
                    if (aggregate_argument_address == nullptr) {
                        return false;
                    }
                } else {
                    argument_value = resolve_typed_value_operand(
                        argument_type, argument, block, bindings, synthetic_index,
                        line_number);
                }
                if (!is_aggregate_type(argument_type) && argument_value == nullptr) {
                    add_error("unsupported LLVM call argument: " +
                                  argument.type_text + " " +
                                  describe_typed_operand(argument),
                              line_number, 1);
                    return false;
                }
                arguments.push_back(argument_value);
                argument_types.push_back(argument_type);
                aggregate_argument_addresses.push_back(aggregate_argument_address);
            }
            auto ensure_argument_value = [&](std::size_t index) -> CoreIrValue * {
                if (index >= arguments.size()) {
                    return nullptr;
                }
                if (arguments[index] != nullptr) {
                    return arguments[index];
                }
                if (aggregate_argument_addresses[index] == nullptr ||
                    argument_types[index] == nullptr) {
                    return nullptr;
                }
                arguments[index] = block.create_instruction<CoreIrLoadInst>(
                    argument_types[index],
                    "ll.call.arg.load." + std::to_string(synthetic_index++),
                    aggregate_argument_addresses[index]);
                return arguments[index];
            };
            auto ensure_argument_prefix = [&](std::size_t count) -> bool {
                if (count > arguments.size()) {
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index) {
                    if (ensure_argument_value(index) == nullptr) {
                        return false;
                    }
                }
                return true;
            };
            if (call_spec->callee.kind == AArch64LlvmImportValueKind::Global &&
                call_spec->callee.global_name.rfind("llvm.is.fpclass.", 0) == 0) {
                if (!ensure_argument_prefix(arguments.size())) {
                    add_error("unsupported llvm.is.fpclass call shape: " + line,
                              line_number, 1);
                    return false;
                }
                const CoreIrType *i1_type = parse_type_text("i1");
                const CoreIrType *i32_type = parse_type_text("i32");
                const auto *operand_float =
                    arguments.size() == 2 ? as_float_type(argument_types[0])
                                          : nullptr;
                if (arguments.size() != 2 || argument_types.size() != 2 ||
                    return_type != i1_type || operand_float == nullptr ||
                    argument_types[1] != i32_type) {
                    add_error("unsupported llvm.is.fpclass call shape: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrFunction *helper = get_or_create_is_fpclass_helper(
                    operand_float->get_float_kind());
                if (helper == nullptr || helper->get_function_type() == nullptr) {
                    add_error("failed to create llvm.is.fpclass helper: " + line,
                              line_number, 1);
                    return false;
                }
                CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                    i1_type,
                    result_name.empty()
                        ? "ll.isfpclass.call." +
                              std::to_string(synthetic_index++)
                        : result_name,
                    helper->get_name(), helper->get_function_type(),
                    std::vector<CoreIrValue *>{arguments[0], arguments[1]});
                return result_name.empty()
                           ? true
                           : bind_instruction_result(result_name, call_value,
                                                     bindings);
            }
            auto apply_variadic_even_gpr_pair_hints =
                [&](CoreIrCallInst *call_inst,
                    const CoreIrFunctionType *actual_callee_type) {
                    if (call_inst == nullptr || actual_callee_type == nullptr ||
                        !actual_callee_type->get_is_variadic()) {
                        return;
                    }
                    const std::size_t fixed_parameter_count =
                        actual_callee_type->get_parameter_types().size();
                    for (std::size_t argument_index = fixed_parameter_count;
                         argument_index < call_spec->arguments.size();
                         ++argument_index) {
                        if (is_import_integer_bit_width(
                                call_spec->arguments[argument_index].type, 128)) {
                            call_inst->set_argument_requires_even_gpr_pair(
                                argument_index);
                        }
                    }
                };
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
                    dynamic_alloca_scopes_.push_back({});
                    return bind_instruction_result(
                        result_name,
                        context_->create_constant<CoreIrConstantNull>(return_type),
                        bindings);
                }
                if (callee_name == "llvm.stackrestore.p0") {
                    if (!dynamic_alloca_scopes_.empty()) {
                        const CoreIrType *opaque_ptr_type = parse_type_text("ptr");
                        const CoreIrFunctionType *free_type =
                            context_->create_type<CoreIrFunctionType>(
                                void_type(),
                                std::vector<const CoreIrType *>{opaque_ptr_type},
                                false);
                        CoreIrFunction *free_function =
                            get_or_create_imported_declaration("free", free_type);
                        auto allocated = std::move(dynamic_alloca_scopes_.back());
                        dynamic_alloca_scopes_.pop_back();
                        for (auto it = allocated.rbegin(); it != allocated.rend();
                             ++it) {
                            CoreIrValue *opaque_ptr = cast_pointer_to_opaque_ptr(
                                block, *it, synthetic_index);
                            if (opaque_ptr == nullptr) {
                                add_error("failed to lower llvm.stackrestore cleanup: " +
                                              line,
                                          line_number, 1);
                                return false;
                            }
                            block.create_instruction<CoreIrCallInst>(
                                void_type(), "", free_function->get_name(), free_type,
                                std::vector<CoreIrValue *>{opaque_ptr});
                        }
                    }
                    return true;
                }
                if (callee_name == "llvm.va_end.p0") {
                    return true;
                }
                if (callee_name == "llvm.va_copy.p0") {
                    const CoreIrType *i8_type = parse_type_text("i8");
                    const CoreIrType *va_copy_type =
                        i8_type == nullptr
                            ? nullptr
                            : context_->create_type<CoreIrArrayType>(i8_type, 32);
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        !is_pointer_type(argument_types[0]) ||
                        !is_pointer_type(argument_types[1]) ||
                        va_copy_type == nullptr ||
                        !copy_aggregate_between_addresses(
                            block, va_copy_type, arguments[0], arguments[1],
                            synthetic_index)) {
                        add_error("unsupported llvm.va_copy call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    return true;
                }
                if (callee_name == "llvm.returnaddress") {
                    if (return_type->get_kind() != CoreIrTypeKind::Pointer) {
                        add_error("unsupported llvm.returnaddress return type: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    return bind_instruction_result(
                        result_name,
                        context_->create_constant<CoreIrConstantNull>(return_type),
                        bindings);
                }
                if (callee_name == "alloca") {
                    const CoreIrType *i64_type = parse_type_text("i64");
                    const CoreIrType *opaque_ptr_type = parse_type_text("ptr");
                    if (return_type->get_kind() != CoreIrTypeKind::Pointer ||
                        arguments.size() != 1 || argument_types.size() != 1 ||
                        i64_type == nullptr || opaque_ptr_type == nullptr) {
                        add_error("unsupported alloca call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *size_value = arguments.front();
                    const auto *size_integer_type =
                        dynamic_cast<const CoreIrIntegerType *>(argument_types.front());
                    if (size_integer_type == nullptr) {
                        add_error("unsupported alloca size type: " + line,
                                  line_number, 1);
                        return false;
                    }
                    if (size_integer_type->get_bit_width() != 64) {
                        size_value = block.create_instruction<CoreIrCastInst>(
                            CoreIrCastKind::ZeroExtend, i64_type,
                            "ll.alloca.call.size.zext." +
                                std::to_string(synthetic_index++),
                            size_value);
                    }
                    const CoreIrFunctionType *malloc_type =
                        context_->create_type<CoreIrFunctionType>(
                            opaque_ptr_type, std::vector<const CoreIrType *>{i64_type},
                            false);
                    CoreIrFunction *malloc_function =
                        get_or_create_imported_declaration("malloc", malloc_type);
                    CoreIrValue *malloc_ptr = block.create_instruction<CoreIrCallInst>(
                        opaque_ptr_type, result_name, malloc_function->get_name(),
                        malloc_type, std::vector<CoreIrValue *>{size_value});
                    retype_opaque_pointer_value(malloc_ptr, void_type());
                    return bind_instruction_result(result_name, malloc_ptr, bindings);
                }
                if (callee_name == "llvm.trap") {
                    const CoreIrFunctionType *abort_type =
                        context_->create_type<CoreIrFunctionType>(
                            void_type(), std::vector<const CoreIrType *>{},
                            false);
                    CoreIrFunction *abort_function =
                        get_or_create_imported_declaration("abort", abort_type);
                    block.create_instruction<CoreIrCallInst>(
                        void_type(), "", abort_function->get_name(), abort_type,
                        std::vector<CoreIrValue *>{});
                    return true;
                }
                if (callee_name.rfind("llvm.fmuladd.", 0) == 0) {
                    const auto *result_array_type = as_array_type(return_type);
                    const auto *result_element_float_type =
                        result_array_type == nullptr
                            ? nullptr
                            : as_float_type(result_array_type->get_element_type());
                    const bool is_scalar_fmuladd =
                        arguments.size() == 3 && argument_types.size() == 3 &&
                        is_float_type(return_type) &&
                        argument_types[0] == return_type &&
                        argument_types[1] == return_type &&
                        argument_types[2] == return_type;
                    const bool is_vector_fmuladd =
                        arguments.size() == 3 && argument_types.size() == 3 &&
                        result_array_type != nullptr &&
                        result_element_float_type != nullptr &&
                        argument_types[0] == return_type &&
                        argument_types[1] == return_type &&
                        argument_types[2] == return_type;

                    if (!is_scalar_fmuladd && !is_vector_fmuladd) {
                        add_error("unsupported llvm.fmuladd call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    if (is_vector_fmuladd) {
                        CoreIrValue *lhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[0], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrValue *rhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[1], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrValue *acc_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[2], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                result_name.empty()
                                    ? "ll.fmuladd.vec." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                return_type, get_storage_alignment(return_type));
                        CoreIrValue *result_address =
                            materialize_stack_slot_address(block, result_slot,
                                                           synthetic_index);
                        if (lhs_address == nullptr || rhs_address == nullptr ||
                            acc_address == nullptr || result_address == nullptr) {
                            add_error("unsupported llvm.fmuladd vector operand: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        for (std::size_t lane = 0;
                             lane < result_array_type->get_element_count(); ++lane) {
                            CoreIrValue *lhs_element_address =
                                create_element_address(block, lhs_address,
                                                       return_type,
                                                       static_cast<std::uint64_t>(lane),
                                                       synthetic_index);
                            CoreIrValue *rhs_element_address =
                                create_element_address(block, rhs_address,
                                                       return_type,
                                                       static_cast<std::uint64_t>(lane),
                                                       synthetic_index);
                            CoreIrValue *acc_element_address =
                                create_element_address(block, acc_address,
                                                       return_type,
                                                       static_cast<std::uint64_t>(lane),
                                                       synthetic_index);
                            CoreIrValue *result_element_address =
                                create_element_address(block, result_address,
                                                       return_type,
                                                       static_cast<std::uint64_t>(lane),
                                                       synthetic_index);
                            if (lhs_element_address == nullptr ||
                                rhs_element_address == nullptr ||
                                acc_element_address == nullptr ||
                                result_element_address == nullptr) {
                                return false;
                            }
                            CoreIrValue *lhs_element =
                                block.create_instruction<CoreIrLoadInst>(
                                    result_array_type->get_element_type(),
                                    "ll.fmuladd.vec.lhs." +
                                        std::to_string(synthetic_index++),
                                    lhs_element_address);
                            CoreIrValue *rhs_element =
                                block.create_instruction<CoreIrLoadInst>(
                                    result_array_type->get_element_type(),
                                    "ll.fmuladd.vec.rhs." +
                                        std::to_string(synthetic_index++),
                                    rhs_element_address);
                            CoreIrValue *acc_element =
                                block.create_instruction<CoreIrLoadInst>(
                                    result_array_type->get_element_type(),
                                    "ll.fmuladd.vec.acc." +
                                        std::to_string(synthetic_index++),
                                    acc_element_address);
                            CoreIrValue *mul_value =
                                block.create_instruction<CoreIrBinaryInst>(
                                    CoreIrBinaryOpcode::Mul,
                                    result_array_type->get_element_type(),
                                    "ll.fmuladd.vec.mul." +
                                        std::to_string(synthetic_index++),
                                    lhs_element, rhs_element);
                            CoreIrValue *add_value =
                                block.create_instruction<CoreIrBinaryInst>(
                                    CoreIrBinaryOpcode::Add,
                                    result_array_type->get_element_type(),
                                    "ll.fmuladd.vec.add." +
                                        std::to_string(synthetic_index++),
                                    mul_value, acc_element);
                            block.create_instruction<CoreIrStoreInst>(
                                void_type(), add_value, result_element_address);
                        }
                        return bind_stack_slot_result(result_name, result_slot,
                                                      bindings);
                    }
                    const auto *float_type = as_float_type(return_type);
                    std::string runtime_name;
                    if (float_type != nullptr) {
                        switch (float_type->get_float_kind()) {
                        case CoreIrFloatKind::Float32:
                            runtime_name = "fmaf";
                            break;
                        case CoreIrFloatKind::Float64:
                            runtime_name = "fma";
                            break;
                        case CoreIrFloatKind::Float128:
                            runtime_name = "fmal";
                            break;
                        default:
                            break;
                        }
                    }
                    if (!runtime_name.empty()) {
                        const CoreIrFunctionType *runtime_type =
                            context_->create_type<CoreIrFunctionType>(
                                return_type,
                                std::vector<const CoreIrType *>{return_type,
                                                                return_type,
                                                                return_type},
                                false);
                        CoreIrFunction *callee = nullptr;
                        if (auto callee_it = functions_.find(runtime_name);
                            callee_it != functions_.end()) {
                            callee = callee_it->second;
                        }
                        if (callee == nullptr) {
                            callee = module_->create_function<CoreIrFunction>(
                                runtime_name, runtime_type, false, false);
                            functions_[runtime_name] = callee;
                        }
                        CoreIrValue *call_value =
                            block.create_instruction<CoreIrCallInst>(
                                return_type,
                                result_name.empty()
                                    ? "ll.fmuladd.call." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                callee->get_name(), runtime_type,
                                std::vector<CoreIrValue *>{arguments[0],
                                                           arguments[1],
                                                           arguments[2]});
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(result_name,
                                                             call_value,
                                                             bindings);
                    }
                    CoreIrValue *mul_value = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Mul, return_type,
                        "ll.fmuladd.mul." + std::to_string(synthetic_index++),
                        arguments[0], arguments[1]);
                    CoreIrValue *add_value = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Add, return_type,
                        result_name.empty()
                            ? "ll.fmuladd.add." + std::to_string(synthetic_index++)
                            : result_name,
                        mul_value, arguments[2]);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, add_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.copysign.", 0) == 0) {
                    const auto *float_type =
                        dynamic_cast<const CoreIrFloatType *>(return_type);
                    if (float_type == nullptr || arguments.size() != 2 ||
                        argument_types.size() != 2 || argument_types[0] != return_type ||
                        argument_types[1] != return_type) {
                        add_error("unsupported llvm.copysign call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    std::string runtime_name;
                    switch (float_type->get_float_kind()) {
                    case CoreIrFloatKind::Float32:
                        runtime_name = "copysignf";
                        break;
                    case CoreIrFloatKind::Float64:
                        runtime_name = "copysign";
                        break;
                    case CoreIrFloatKind::Float128:
                        runtime_name = "copysignl";
                        break;
                    default:
                        add_error(
                            "unsupported llvm.copysign floating kind in the "
                            "AArch64 restricted LLVM importer: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    const CoreIrFunctionType *runtime_callee_type =
                        context_->create_type<CoreIrFunctionType>(
                            return_type,
                            std::vector<const CoreIrType *>{return_type, return_type},
                            false);
                    CoreIrFunction *callee = get_or_create_imported_declaration(
                        runtime_name, runtime_callee_type);
                    CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                        return_type, result_name, callee->get_name(),
                        runtime_callee_type, arguments);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, call_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.fabs.", 0) == 0) {
                    const auto *float_type =
                        dynamic_cast<const CoreIrFloatType *>(return_type);
                    if (float_type == nullptr || arguments.size() != 1 ||
                        argument_types.size() != 1 ||
                        argument_types[0] != return_type) {
                        add_error("unsupported llvm.fabs call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    std::string runtime_name;
                    switch (float_type->get_float_kind()) {
                    case CoreIrFloatKind::Float32:
                        runtime_name = "fabsf";
                        break;
                    case CoreIrFloatKind::Float64:
                        runtime_name = "fabs";
                        break;
                    case CoreIrFloatKind::Float128:
                        runtime_name = "fabsl";
                        break;
                    default:
                        add_error(
                            "unsupported llvm.fabs floating kind in the "
                            "AArch64 restricted LLVM importer: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    const CoreIrFunctionType *runtime_callee_type =
                        context_->create_type<CoreIrFunctionType>(
                            return_type,
                            std::vector<const CoreIrType *>{return_type}, false);
                    CoreIrFunction *callee = get_or_create_imported_declaration(
                        runtime_name, runtime_callee_type);
                    CoreIrValue *call_value = block.create_instruction<CoreIrCallInst>(
                        return_type, result_name, callee->get_name(),
                        runtime_callee_type, arguments);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, call_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.prefetch.", 0) == 0) {
                    return true;
                }
                if (callee_name.rfind("llvm.ptrmask.", 0) == 0) {
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        !is_pointer_type(return_type) ||
                        !is_pointer_type(argument_types[0]) ||
                        argument_types[1]->get_kind() != CoreIrTypeKind::Integer) {
                        add_error("unsupported llvm.ptrmask call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *address_int = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::PtrToInt, argument_types[1],
                        "ll.ptrmask.ptrtoint." +
                            std::to_string(synthetic_index++),
                        arguments[0]);
                    CoreIrValue *masked_int = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::And, argument_types[1],
                        "ll.ptrmask.and." + std::to_string(synthetic_index++),
                        address_int, arguments[1]);
                    CoreIrValue *masked_ptr = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::IntToPtr, return_type,
                        result_name.empty()
                            ? "ll.ptrmask.inttoptr." +
                                  std::to_string(synthetic_index++)
                            : result_name,
                        masked_int);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, masked_ptr,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.smul.with.overflow.", 0) == 0 &&
                    callee_name != "llvm.smul.with.overflow.i64") {
                    const auto *result_struct =
                        dynamic_cast<const CoreIrStructType *>(return_type);
                    const auto *value_type = arguments.size() == 2
                                                 ? as_integer_type(argument_types[0])
                                                 : nullptr;
                    const CoreIrType *i64_type = parse_type_text("i64");
                    const CoreIrType *i1_type = parse_type_text("i1");
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        i64_type == nullptr || i1_type == nullptr ||
                        value_type == nullptr || argument_types[1] != value_type ||
                        value_type->get_bit_width() == 0 ||
                        value_type->get_bit_width() >= 64 ||
                        result_struct == nullptr ||
                        result_struct->get_element_types().size() != 2 ||
                        result_struct->get_element_types()[0] != value_type ||
                        result_struct->get_element_types()[1] != i1_type) {
                        add_error(
                            "unsupported llvm.smul.with.overflow call shape: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    CoreIrValue *lhs64 = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::SignExtend, i64_type,
                        "ll.smulov.lhs64." + std::to_string(synthetic_index++),
                        arguments[0]);
                    CoreIrValue *rhs64 = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::SignExtend, i64_type,
                        "ll.smulov.rhs64." + std::to_string(synthetic_index++),
                        arguments[1]);
                    CoreIrValue *product64 = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Mul, i64_type,
                        "ll.smulov.mul64." + std::to_string(synthetic_index++),
                        lhs64, rhs64);
                    CoreIrValue *truncated_value =
                        block.create_instruction<CoreIrCastInst>(
                            CoreIrCastKind::Truncate, value_type,
                            "ll.smulov.trunc." + std::to_string(synthetic_index++),
                            product64);
                    CoreIrValue *reextended_value =
                        block.create_instruction<CoreIrCastInst>(
                            CoreIrCastKind::SignExtend, i64_type,
                            "ll.smulov.reext." + std::to_string(synthetic_index++),
                            truncated_value);
                    CoreIrValue *overflow_flag =
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::NotEqual, i1_type,
                            "ll.smulov.overflow." +
                                std::to_string(synthetic_index++),
                            product64, reextended_value);
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? "ll.smulov.result." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            return_type, get_storage_alignment(return_type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    CoreIrValue *value_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 0, synthetic_index);
                    CoreIrValue *flag_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 1, synthetic_index);
                    if (result_address == nullptr || value_address == nullptr ||
                        flag_address == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              truncated_value,
                                                              value_address);
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              overflow_flag,
                                                              flag_address);
                    return bind_stack_slot_result(result_name, result_slot, bindings);
                }
                if (callee_name.rfind("llvm.uadd.with.overflow.", 0) == 0) {
                    const auto *result_struct =
                        dynamic_cast<const CoreIrStructType *>(return_type);
                    const auto *value_type = arguments.size() == 2
                                                 ? as_integer_type(argument_types[0])
                                                 : nullptr;
                    const CoreIrType *i1_type = parse_type_text("i1");
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        i1_type == nullptr || value_type == nullptr ||
                        argument_types[1] != value_type ||
                        value_type->get_bit_width() == 0 ||
                        value_type->get_bit_width() > 64 ||
                        result_struct == nullptr ||
                        result_struct->get_element_types().size() != 2 ||
                        result_struct->get_element_types()[0] != value_type ||
                        result_struct->get_element_types()[1] != i1_type) {
                        add_error(
                            "unsupported llvm.uadd.with.overflow call shape: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    CoreIrValue *sum_value = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Add, value_type,
                        "ll.uaddov.sum." + std::to_string(synthetic_index++),
                        arguments[0], arguments[1]);
                    CoreIrValue *overflow_flag =
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::UnsignedLess, i1_type,
                            "ll.uaddov.overflow." +
                                std::to_string(synthetic_index++),
                            sum_value, arguments[0]);
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? "ll.uaddov.result." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            return_type, get_storage_alignment(return_type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    CoreIrValue *value_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 0, synthetic_index);
                    CoreIrValue *flag_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 1, synthetic_index);
                    if (result_address == nullptr || value_address == nullptr ||
                        flag_address == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(), sum_value,
                                                              value_address);
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              overflow_flag,
                                                              flag_address);
                    return bind_stack_slot_result(result_name, result_slot, bindings);
                }
                if (callee_name.rfind("llvm.umul.with.overflow.", 0) == 0) {
                    const auto *result_struct =
                        dynamic_cast<const CoreIrStructType *>(return_type);
                    const auto *value_type = arguments.size() == 2
                                                 ? as_integer_type(argument_types[0])
                                                 : nullptr;
                    const CoreIrType *i64_type = parse_type_text("i64");
                    const CoreIrType *i1_type = parse_type_text("i1");
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        i64_type == nullptr || i1_type == nullptr ||
                        value_type == nullptr || argument_types[1] != value_type ||
                        value_type->get_bit_width() == 0 ||
                        value_type->get_bit_width() >= 64 ||
                        result_struct == nullptr ||
                        result_struct->get_element_types().size() != 2 ||
                        result_struct->get_element_types()[0] != value_type ||
                        result_struct->get_element_types()[1] != i1_type) {
                        add_error(
                            "unsupported llvm.umul.with.overflow call shape: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    CoreIrValue *lhs64 = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::ZeroExtend, i64_type,
                        "ll.umulov.lhs64." + std::to_string(synthetic_index++),
                        arguments[0]);
                    CoreIrValue *rhs64 = block.create_instruction<CoreIrCastInst>(
                        CoreIrCastKind::ZeroExtend, i64_type,
                        "ll.umulov.rhs64." + std::to_string(synthetic_index++),
                        arguments[1]);
                    CoreIrValue *product64 = block.create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Mul, i64_type,
                        "ll.umulov.mul64." + std::to_string(synthetic_index++),
                        lhs64, rhs64);
                    CoreIrValue *truncated_value =
                        block.create_instruction<CoreIrCastInst>(
                            CoreIrCastKind::Truncate, value_type,
                            "ll.umulov.trunc." + std::to_string(synthetic_index++),
                            product64);
                    CoreIrValue *reextended_value =
                        block.create_instruction<CoreIrCastInst>(
                            CoreIrCastKind::ZeroExtend, i64_type,
                            "ll.umulov.reext." + std::to_string(synthetic_index++),
                            truncated_value);
                    CoreIrValue *overflow_flag =
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::NotEqual, i1_type,
                            "ll.umulov.overflow." +
                                std::to_string(synthetic_index++),
                            product64, reextended_value);
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? "ll.umulov.result." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            return_type, get_storage_alignment(return_type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    CoreIrValue *value_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 0, synthetic_index);
                    CoreIrValue *flag_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 1, synthetic_index);
                    if (result_address == nullptr || value_address == nullptr ||
                        flag_address == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              truncated_value,
                                                              value_address);
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              overflow_flag,
                                                              flag_address);
                    return bind_stack_slot_result(result_name, result_slot, bindings);
                }
                if (callee_name == "llvm.smul.with.overflow.i64") {
                    const auto *result_struct =
                        dynamic_cast<const CoreIrStructType *>(return_type);
                    const CoreIrType *i64_type = parse_type_text("i64");
                    const CoreIrType *i1_type = parse_type_text("i1");
                    if (arguments.size() != 2 || argument_types.size() != 2 ||
                        i64_type == nullptr || i1_type == nullptr ||
                        argument_types[0] != i64_type ||
                        argument_types[1] != i64_type ||
                        result_struct == nullptr ||
                        result_struct->get_element_types().size() != 2 ||
                        result_struct->get_element_types()[0] != i64_type ||
                        result_struct->get_element_types()[1] != i1_type) {
                        add_error(
                            "unsupported llvm.smul.with.overflow.i64 call shape: " +
                                line,
                            line_number, 1);
                        return false;
                    }
                    const CoreIrType *i128_type =
                        context_->create_type<CoreIrArrayType>(i64_type, 2);
                    auto materialize_signed_i64_as_i128 =
                        [&](CoreIrValue *value, const std::string &tag)
                        -> CoreIrValue * {
                        CoreIrStackSlot *slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                tag + std::to_string(synthetic_index++), i128_type,
                                get_storage_alignment(i128_type));
                        CoreIrValue *slot_address = materialize_stack_slot_address(
                            block, slot, synthetic_index);
                        CoreIrValue *low_address = create_element_address(
                            block, slot_address, i128_type, 0, synthetic_index);
                        CoreIrValue *high_address = create_element_address(
                            block, slot_address, i128_type, 1, synthetic_index);
                        const CoreIrConstant *shift_constant =
                            context_->create_constant<CoreIrConstantInt>(i64_type, 63);
                        CoreIrValue *sign_word = block.create_instruction<
                            CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::AShr, i64_type,
                            "ll.smulov.sign." + std::to_string(synthetic_index++),
                            value, const_cast<CoreIrConstant *>(shift_constant));
                        if (slot_address == nullptr || low_address == nullptr ||
                            high_address == nullptr) {
                            return nullptr;
                        }
                        block.create_instruction<CoreIrStoreInst>(void_type(), value,
                                                                  low_address);
                        block.create_instruction<CoreIrStoreInst>(void_type(), sign_word,
                                                                  high_address);
                        return block.create_instruction<CoreIrLoadInst>(
                            i128_type,
                            "ll.smulov.arg." + std::to_string(synthetic_index++),
                            slot_address);
                    };
                    CoreIrValue *lhs_i128 = materialize_signed_i64_as_i128(
                        arguments[0], "ll.smulov.lhs.");
                    CoreIrValue *rhs_i128 = materialize_signed_i64_as_i128(
                        arguments[1], "ll.smulov.rhs.");
                    if (lhs_i128 == nullptr || rhs_i128 == nullptr) {
                        add_error("failed to materialize llvm.smul.with.overflow.i64",
                                  line_number, 1);
                        return false;
                    }
                    const CoreIrFunctionType *multi3_type =
                        context_->create_type<CoreIrFunctionType>(
                            i128_type,
                            std::vector<const CoreIrType *>{i128_type, i128_type},
                            false);
                    CoreIrFunction *multi3 =
                        get_or_create_imported_declaration("__multi3", multi3_type);
                    CoreIrValue *product = block.create_instruction<CoreIrCallInst>(
                        i128_type,
                        "ll.smulov.mul." + std::to_string(synthetic_index++),
                        multi3->get_name(), multi3_type,
                        std::vector<CoreIrValue *>{lhs_i128, rhs_i128});
                    CoreIrStackSlot *product_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            "ll.smulov.prod." + std::to_string(synthetic_index++),
                            i128_type, get_storage_alignment(i128_type));
                    block.create_instruction<CoreIrStoreInst>(void_type(), product,
                                                              product_slot);
                    CoreIrValue *product_address = materialize_stack_slot_address(
                        block, product_slot, synthetic_index);
                    CoreIrValue *low_address = create_element_address(
                        block, product_address, i128_type, 0, synthetic_index);
                    CoreIrValue *high_address = create_element_address(
                        block, product_address, i128_type, 1, synthetic_index);
                    if (product_address == nullptr || low_address == nullptr ||
                        high_address == nullptr) {
                        return false;
                    }
                    CoreIrValue *low_word = block.create_instruction<CoreIrLoadInst>(
                        i64_type, "ll.smulov.low." + std::to_string(synthetic_index++),
                        low_address);
                    CoreIrValue *high_word = block.create_instruction<CoreIrLoadInst>(
                        i64_type, "ll.smulov.high." + std::to_string(synthetic_index++),
                        high_address);
                    const CoreIrConstant *shift_constant =
                        context_->create_constant<CoreIrConstantInt>(i64_type, 63);
                    CoreIrValue *sign_word = block.create_instruction<
                        CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::AShr, i64_type,
                        "ll.smulov.signcheck." +
                            std::to_string(synthetic_index++),
                        low_word, const_cast<CoreIrConstant *>(shift_constant));
                    CoreIrValue *overflow_flag =
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::NotEqual, i1_type,
                            "ll.smulov.overflow." +
                                std::to_string(synthetic_index++),
                            high_word, sign_word);
                    CoreIrStackSlot *result_slot =
                        function.create_stack_slot<CoreIrStackSlot>(
                            result_name.empty()
                                ? "ll.smulov.result." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            return_type, get_storage_alignment(return_type));
                    CoreIrValue *result_address = materialize_stack_slot_address(
                        block, result_slot, synthetic_index);
                    CoreIrValue *value_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 0, synthetic_index);
                    CoreIrValue *flag_address = create_aggregate_subvalue_address(
                        block, result_address, return_type, 1, synthetic_index);
                    if (result_address == nullptr || value_address == nullptr ||
                        flag_address == nullptr) {
                        return false;
                    }
                    block.create_instruction<CoreIrStoreInst>(void_type(), low_word,
                                                              value_address);
                    block.create_instruction<CoreIrStoreInst>(void_type(),
                                                              overflow_flag,
                                                              flag_address);
                    return bind_stack_slot_result(result_name, result_slot, bindings);
                }
                if (callee_name.rfind("llvm.frameaddress.", 0) == 0) {
                    const auto *index_constant =
                        arguments.size() == 1
                            ? dynamic_cast<const CoreIrConstantInt *>(arguments[0])
                            : nullptr;
                    if (arguments.size() != 1 || index_constant == nullptr ||
                        index_constant->get_value() != 0 ||
                        !is_pointer_type(return_type)) {
                        add_error("unsupported llvm.frameaddress call shape: " +
                                      line,
                                  line_number, 1);
                        return false;
                    }
                    const CoreIrType *i8_type = parse_type_text("i8");
                    if (i8_type == nullptr) {
                        add_error("failed to lower llvm.frameaddress intrinsic",
                                  line_number, 1);
                        return false;
                    }
                    CoreIrStackSlot *frame_anchor =
                        function.create_stack_slot<CoreIrStackSlot>(
                            "ll.frameaddress." +
                                std::to_string(synthetic_index++),
                            i8_type, 16);
                    return bind_stack_slot_result(result_name, frame_anchor,
                                                  bindings);
                }
                auto make_integer_constant =
                    [&](std::uint64_t value) -> CoreIrConstant * {
                    return context_->create_constant<CoreIrConstantInt>(
                        return_type,
                        truncate_integer_to_width(
                            value, integer_type_bit_width(return_type).value_or(64)));
                };
                auto make_binary_value =
                    [&](CoreIrBinaryOpcode opcode, const std::string &tag,
                        CoreIrValue *lhs, CoreIrValue *rhs) -> CoreIrValue * {
                    return block.create_instruction<CoreIrBinaryInst>(
                        opcode, return_type,
                        tag + std::to_string(synthetic_index++), lhs, rhs);
                };
                auto make_equal_zero =
                    [&](CoreIrValue *value,
                        const std::string &tag) -> CoreIrValue * {
                    const CoreIrType *i1_type = parse_type_text("i1");
                    return block.create_instruction<CoreIrCompareInst>(
                        CoreIrComparePredicate::Equal, i1_type,
                        tag + std::to_string(synthetic_index++), value,
                        make_integer_constant(0));
                };
                auto make_select_value =
                    [&](CoreIrValue *condition, CoreIrValue *true_value,
                        CoreIrValue *false_value,
                        const std::string &tag) -> CoreIrValue * {
                    return block.create_instruction<CoreIrSelectInst>(
                        return_type, tag + std::to_string(synthetic_index++),
                        condition, true_value, false_value);
                };
                auto lower_ctpop_value =
                    [&](CoreIrValue *input,
                        const std::string &tag) -> CoreIrValue * {
                    const auto width = integer_type_bit_width(return_type).value_or(64);
                    CoreIrValue *value = input;
                    value = make_binary_value(
                        CoreIrBinaryOpcode::Sub, tag + ".sub.",
                        value,
                        make_binary_value(
                            CoreIrBinaryOpcode::And, tag + ".and1.",
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr1.", value,
                                              make_integer_constant(1)),
                            make_integer_constant(0x5555555555555555ULL)));
                    value = make_binary_value(
                        CoreIrBinaryOpcode::Add, tag + ".add2.",
                        make_binary_value(CoreIrBinaryOpcode::And,
                                          tag + ".and2a.", value,
                                          make_integer_constant(
                                              0x3333333333333333ULL)),
                        make_binary_value(
                            CoreIrBinaryOpcode::And, tag + ".and2b.",
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr2.", value,
                                              make_integer_constant(2)),
                            make_integer_constant(0x3333333333333333ULL)));
                    value = make_binary_value(
                        CoreIrBinaryOpcode::And, tag + ".and4.",
                        make_binary_value(
                            CoreIrBinaryOpcode::Add, tag + ".add4.", value,
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr4.", value,
                                              make_integer_constant(4))),
                        make_integer_constant(0x0F0F0F0F0F0F0F0FULL));
                    if (width > 8) {
                        value = make_binary_value(
                            CoreIrBinaryOpcode::Add, tag + ".add8.", value,
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr8.", value,
                                              make_integer_constant(8)));
                    }
                    if (width > 16) {
                        value = make_binary_value(
                            CoreIrBinaryOpcode::Add, tag + ".add16.", value,
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr16.", value,
                                              make_integer_constant(16)));
                    }
                    if (width > 32) {
                        value = make_binary_value(
                            CoreIrBinaryOpcode::Add, tag + ".add32.", value,
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              tag + ".shr32.", value,
                                              make_integer_constant(32)));
                    }
                    return make_binary_value(
                        CoreIrBinaryOpcode::And, tag + ".mask.", value,
                        make_integer_constant(width > 32 ? 0x7fU : 0x3fU));
                };
                if (callee_name.rfind("llvm.ctpop.", 0) == 0) {
                    const auto width = integer_type_bit_width(return_type);
                    if (!width.has_value() || arguments.size() != 1 ||
                        argument_types.size() != 1 || argument_types[0] != return_type) {
                        add_error("unsupported llvm.ctpop call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *result_value =
                        lower_ctpop_value(arguments[0], "ll.ctpop.");
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, result_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.cttz.", 0) == 0) {
                    const auto width = integer_type_bit_width(return_type);
                    if (!width.has_value() || arguments.size() != 2 ||
                        argument_types.size() != 2 || argument_types[0] != return_type) {
                        add_error("unsupported llvm.cttz call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *negated = make_binary_value(
                        CoreIrBinaryOpcode::Sub, "ll.cttz.neg.",
                        make_integer_constant(0), arguments[0]);
                    CoreIrValue *lowest_bit = make_binary_value(
                        CoreIrBinaryOpcode::And, "ll.cttz.lowbit.", arguments[0],
                        negated);
                    CoreIrValue *minus_one = make_binary_value(
                        CoreIrBinaryOpcode::Sub, "ll.cttz.minus1.", lowest_bit,
                        make_integer_constant(1));
                    CoreIrValue *count_value =
                        lower_ctpop_value(minus_one, "ll.cttz.pop.");
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, count_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.ctlz.", 0) == 0) {
                    const auto width = integer_type_bit_width(return_type);
                    if (!width.has_value() || arguments.size() != 2 ||
                        argument_types.size() != 2 || argument_types[0] != return_type) {
                        add_error("unsupported llvm.ctlz call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrValue *smeared = arguments[0];
                    for (std::size_t shift = 1; shift < *width; shift <<= 1U) {
                        smeared = make_binary_value(
                            CoreIrBinaryOpcode::Or, "ll.ctlz.or.", smeared,
                            make_binary_value(CoreIrBinaryOpcode::LShr,
                                              "ll.ctlz.shr.", smeared,
                                              make_integer_constant(shift)));
                    }
                    CoreIrValue *population =
                        lower_ctpop_value(smeared, "ll.ctlz.pop.");
                    CoreIrValue *count_value = make_binary_value(
                        CoreIrBinaryOpcode::Sub, "ll.ctlz.sub.",
                        make_integer_constant(*width), population);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, count_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.bswap.", 0) == 0) {
                    const auto target_width = integer_type_bit_width(return_type);
                    if (!target_width.has_value() || arguments.size() != 1 ||
                        argument_types.size() != 1 || arguments[0] == nullptr ||
                        argument_types[0] != return_type || *target_width == 0 ||
                        *target_width > 64 || (*target_width % 8) != 0) {
                        add_error("unsupported llvm.bswap call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    const std::size_t byte_count = *target_width / 8U;
                    auto make_shift_constant = [&](std::size_t shift_bits) {
                        return static_cast<CoreIrConstant *>(
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, shift_bits));
                    };
                    auto make_mask_constant = [&](std::uint64_t value) {
                        return static_cast<CoreIrConstant *>(
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, value));
                    };

                    if (const auto *constant_operand =
                            dynamic_cast<const CoreIrConstantInt *>(arguments[0]);
                        constant_operand != nullptr) {
                        std::uint64_t source = constant_operand->get_value();
                        std::uint64_t reversed = 0;
                        for (std::size_t byte_index = 0; byte_index < byte_count;
                             ++byte_index) {
                            const std::uint64_t byte =
                                (source >> (byte_index * 8U)) & 0xffU;
                            reversed |=
                                byte << ((byte_count - 1U - byte_index) * 8U);
                        }
                        const CoreIrConstant *folded =
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, reversed);
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(
                                         result_name,
                                         const_cast<CoreIrConstant *>(folded),
                                         bindings);
                    }

                    CoreIrValue *result_value = nullptr;
                    for (std::size_t byte_index = 0; byte_index < byte_count;
                         ++byte_index) {
                        CoreIrValue *byte_value = arguments[0];
                        if (byte_index != 0) {
                            byte_value = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::LShr, return_type,
                                "ll.bswap.shr." +
                                    std::to_string(synthetic_index++),
                                byte_value, make_shift_constant(byte_index * 8U));
                        }
                        byte_value = block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::And, return_type,
                            "ll.bswap.mask." +
                                std::to_string(synthetic_index++),
                            byte_value, make_mask_constant(0xffU));
                        const std::size_t destination_shift =
                            (byte_count - 1U - byte_index) * 8U;
                        if (destination_shift != 0) {
                            byte_value = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Shl, return_type,
                                "ll.bswap.shl." +
                                    std::to_string(synthetic_index++),
                                byte_value, make_shift_constant(destination_shift));
                        }
                        if (result_value == nullptr) {
                            result_value = byte_value;
                            continue;
                        }
                        result_value = block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::Or, return_type,
                            result_name.empty()
                                ? "ll.bswap.or." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            result_value, byte_value);
                    }
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, result_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.bitreverse.", 0) == 0) {
                    const auto target_width = integer_type_bit_width(return_type);
                    if (!target_width.has_value() || arguments.size() != 1 ||
                        argument_types.size() != 1 || arguments[0] == nullptr ||
                        argument_types[0] != return_type || *target_width == 0 ||
                        *target_width > 64) {
                        add_error("unsupported llvm.bitreverse call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    auto make_shift_constant = [&](std::size_t shift_bits) {
                        return static_cast<CoreIrConstant *>(
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, shift_bits));
                    };
                    auto make_mask_constant = [&](std::uint64_t value) {
                        return static_cast<CoreIrConstant *>(
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, value));
                    };

                    if (const auto *constant_operand =
                            dynamic_cast<const CoreIrConstantInt *>(arguments[0]);
                        constant_operand != nullptr) {
                        std::uint64_t source = constant_operand->get_value();
                        std::uint64_t reversed = 0;
                        for (std::size_t bit_index = 0; bit_index < *target_width;
                             ++bit_index) {
                            const std::uint64_t bit =
                                (source >> bit_index) & 1U;
                            reversed |= bit << ((*target_width - 1U) - bit_index);
                        }
                        const CoreIrConstant *folded =
                            context_->create_constant<CoreIrConstantInt>(
                                return_type, reversed);
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(
                                         result_name,
                                         const_cast<CoreIrConstant *>(folded),
                                         bindings);
                    }

                    CoreIrValue *result_value = nullptr;
                    for (std::size_t bit_index = 0; bit_index < *target_width;
                         ++bit_index) {
                        CoreIrValue *bit_value = arguments[0];
                        if (bit_index != 0) {
                            bit_value = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::LShr, return_type,
                                "ll.bitreverse.shr." +
                                    std::to_string(synthetic_index++),
                                bit_value, make_shift_constant(bit_index));
                        }
                        bit_value = block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::And, return_type,
                            "ll.bitreverse.mask." +
                                std::to_string(synthetic_index++),
                            bit_value, make_mask_constant(1U));
                        const std::size_t destination_shift =
                            (*target_width - 1U) - bit_index;
                        if (destination_shift != 0) {
                            bit_value = block.create_instruction<CoreIrBinaryInst>(
                                CoreIrBinaryOpcode::Shl, return_type,
                                "ll.bitreverse.shl." +
                                    std::to_string(synthetic_index++),
                                bit_value,
                                make_shift_constant(destination_shift));
                        }
                        if (result_value == nullptr) {
                            result_value = bit_value;
                            continue;
                        }
                        result_value = block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::Or, return_type,
                            result_name.empty()
                                ? "ll.bitreverse.or." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            result_value, bit_value);
                    }
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, result_value,
                                                         bindings);
                }
                if (callee_name.rfind("llvm.abs.", 0) == 0) {
                    const auto width = integer_type_bit_width(return_type);
                    const CoreIrType *i1_type = parse_type_text("i1");
                    if (!width.has_value() || i1_type == nullptr ||
                        arguments.size() != 2 || argument_types.size() != 2 ||
                        argument_types[0] != return_type ||
                        argument_types[1] != i1_type) {
                        add_error("unsupported llvm.abs call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
                    CoreIrConstant *zero_constant =
                        context_->create_constant<CoreIrConstantInt>(return_type, 0);
                    CoreIrValue *is_negative =
                        block.create_instruction<CoreIrCompareInst>(
                            CoreIrComparePredicate::SignedLess, i1_type,
                            "ll.abs.cmp." + std::to_string(synthetic_index++),
                            arguments[0], zero_constant);
                    CoreIrValue *negated_value =
                        block.create_instruction<CoreIrBinaryInst>(
                            CoreIrBinaryOpcode::Sub, return_type,
                            "ll.abs.neg." + std::to_string(synthetic_index++),
                            zero_constant, arguments[0]);
                    CoreIrValue *result_value =
                        block.create_instruction<CoreIrSelectInst>(
                            return_type,
                            result_name.empty()
                                ? "ll.abs.sel." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            is_negative, negated_value, arguments[0]);
                    return result_name.empty()
                               ? true
                               : bind_instruction_result(result_name, result_value,
                                                         bindings);
                }
                auto lower_memory_intrinsic_call =
                    [&](const std::string &runtime_name,
                        std::size_t expected_minimum_arguments,
                        std::size_t used_argument_count) -> bool {
                    if (!ensure_argument_prefix(used_argument_count)) {
                        add_error("unsupported LLVM intrinsic call shape: " + line,
                                  line_number, 1);
                        return false;
                    }
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
                auto lower_scalar_extremum =
                    [&](CoreIrComparePredicate predicate) -> bool {
                        if (arguments.size() != 2 || argument_types.size() != 2 ||
                            argument_types[0] != return_type ||
                            argument_types[1] != return_type) {
                            add_error("unsupported LLVM extremum call shape: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        const CoreIrType *i1_type = parse_type_text("i1");
                        if (i1_type == nullptr) {
                            return false;
                        }
                        CoreIrValue *compare = block.create_instruction<
                            CoreIrCompareInst>(
                            predicate, i1_type,
                            "ll.extremum.cmp." + std::to_string(synthetic_index++),
                            arguments[0], arguments[1]);
                        CoreIrValue *value = block.create_instruction<
                            CoreIrSelectInst>(
                            return_type,
                            result_name.empty()
                                ? "ll.extremum.sel." +
                                      std::to_string(synthetic_index++)
                                : result_name,
                            compare, arguments[0], arguments[1]);
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(result_name, value,
                                                             bindings);
                    };
                auto get_or_create_direct_call_decl =
                    [&](const std::string &name,
                        const CoreIrFunctionType *function_type)
                    -> CoreIrFunction * {
                        if (auto callee_it = functions_.find(name);
                            callee_it != functions_.end()) {
                            return callee_it->second;
                        }
                        CoreIrFunction *callee = module_->create_function<CoreIrFunction>(
                            name, function_type, false, false);
                        functions_[name] = callee;
                        return callee;
                    };
                auto lower_inline_v4i32_extremum_helper =
                    [&](const std::string &helper_name) -> bool {
                        if (!is_i32x4_array_type(return_type) ||
                            arguments.size() != 2 || argument_types.size() != 2 ||
                            argument_types[0] != return_type ||
                            argument_types[1] != return_type) {
                            return false;
                        }
                        const CoreIrType *ptr_type = parse_type_text("ptr");
                        if (ptr_type == nullptr) {
                            return false;
                        }
                        CoreIrValue *lhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[0], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrValue *rhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[1], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                result_name.empty()
                                    ? "ll.inline.extremum.vec." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                return_type, get_storage_alignment(return_type));
                        CoreIrValue *result_address =
                            materialize_stack_slot_address(block, result_slot,
                                                           synthetic_index);
                        if (lhs_address == nullptr || rhs_address == nullptr ||
                            result_address == nullptr) {
                            return false;
                        }
                        const CoreIrFunctionType *helper_type =
                            context_->create_type<CoreIrFunctionType>(
                                void_type(),
                                std::vector<const CoreIrType *>{ptr_type, ptr_type,
                                                                ptr_type},
                                false);
                        CoreIrFunction *helper =
                            get_or_create_direct_call_decl(helper_name, helper_type);
                        block.create_instruction<CoreIrCallInst>(
                            void_type(), "", helper->get_name(), helper_type,
                            std::vector<CoreIrValue *>{result_address, lhs_address,
                                                       rhs_address});
                        return bind_stack_slot_result(result_name, result_slot,
                                                      bindings);
                    };
                auto lower_native_v4i32_call =
                    [&](const std::string &helper_name) -> bool {
                        if (!is_i32x4_vector_type(return_type) ||
                            arguments.size() != 2 || argument_types.size() != 2 ||
                            argument_types[0] != return_type ||
                            argument_types[1] != return_type) {
                            return false;
                        }
                        const CoreIrFunctionType *helper_type =
                            context_->create_type<CoreIrFunctionType>(
                                return_type,
                                std::vector<const CoreIrType *>{return_type,
                                                                return_type},
                                false);
                        CoreIrFunction *helper =
                            get_or_create_direct_call_decl(helper_name, helper_type);
                        CoreIrValue *call_value =
                            block.create_instruction<CoreIrCallInst>(
                                return_type, result_name, helper->get_name(),
                                helper_type, arguments);
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(result_name,
                                                             call_value,
                                                             bindings);
                    };
                auto lower_inline_v4i32_binary_helper =
                    [&](const std::string &helper_name) -> bool {
                        if (!is_i32x4_array_type(return_type) ||
                            arguments.size() != 2 || argument_types.size() != 2 ||
                            argument_types[0] != return_type ||
                            argument_types[1] != return_type) {
                            return false;
                        }
                        const CoreIrType *ptr_type = parse_type_text("ptr");
                        if (ptr_type == nullptr) {
                            return false;
                        }
                        CoreIrValue *lhs_splat_scalar =
                            find_lane0_splat_scalar_binding(
                                return_type, call_spec->arguments[0], bindings);
                        CoreIrValue *rhs_splat_scalar =
                            find_lane0_splat_scalar_binding(
                                return_type, call_spec->arguments[1], bindings);
                        if (lhs_splat_scalar != nullptr &&
                            rhs_splat_scalar != nullptr) {
                            lhs_splat_scalar = nullptr;
                            rhs_splat_scalar = nullptr;
                        }
                        CoreIrValue *lhs_address =
                            lhs_splat_scalar == nullptr
                                ? ensure_addressable_aggregate_typed_operand(
                                      return_type, call_spec->arguments[0], function,
                                      block, bindings, synthetic_index, line_number)
                                : nullptr;
                        CoreIrValue *rhs_address =
                            rhs_splat_scalar == nullptr
                                ? ensure_addressable_aggregate_typed_operand(
                                      return_type, call_spec->arguments[1], function,
                                      block, bindings, synthetic_index, line_number)
                                : nullptr;
                        if ((lhs_splat_scalar == nullptr && lhs_address == nullptr) ||
                            (rhs_splat_scalar == nullptr && rhs_address == nullptr)) {
                            return false;
                        }
                        if (!result_name.empty()) {
                            ValueBinding binding;
                            if (helper_name == kInlineVecMulV4I32) {
                                binding.deferred_vector_pair_kind =
                                    ValueBinding::DeferredVectorPairKind::Mul;
                                binding.deferred_vector_lhs_address = lhs_address;
                                binding.deferred_vector_rhs_address = rhs_address;
                            } else {
                                binding.deferred_vector_pair_kind =
                                    ValueBinding::DeferredVectorPairKind::Add;
                            }
                            binding.deferred_vector_lhs_address = lhs_address;
                            binding.deferred_vector_rhs_address = rhs_address;
                            binding.deferred_vector_lhs_splat_scalar =
                                lhs_splat_scalar;
                            binding.deferred_vector_rhs_splat_scalar =
                                rhs_splat_scalar;
                            binding.deferred_vector_pair_type = return_type;
                            bindings[result_name] = binding;
                            return true;
                        }
                        std::vector<const CoreIrType *> helper_argument_types{ptr_type};
                        std::vector<CoreIrValue *> helper_arguments;
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                "ll.inline.binary.vec." +
                                    std::to_string(synthetic_index++),
                                return_type, get_storage_alignment(return_type));
                        CoreIrValue *result_address =
                            materialize_stack_slot_address(block, result_slot,
                                                           synthetic_index);
                        if (result_address == nullptr) {
                            return false;
                        }
                        helper_arguments.push_back(result_address);
                        std::string direct_helper_name = helper_name;
                        if (lhs_splat_scalar != nullptr &&
                            rhs_address != nullptr) {
                            direct_helper_name =
                                helper_name == kInlineVecMulV4I32
                                    ? kInlineVecMulV4I32SplatLhs
                                    : kInlineVecAddV4I32SplatLhs;
                            helper_argument_types.push_back(
                                lhs_splat_scalar->get_type());
                            helper_argument_types.push_back(ptr_type);
                            helper_arguments.push_back(lhs_splat_scalar);
                            helper_arguments.push_back(rhs_address);
                        } else if (rhs_splat_scalar != nullptr &&
                                   lhs_address != nullptr) {
                            direct_helper_name =
                                helper_name == kInlineVecMulV4I32
                                    ? kInlineVecMulV4I32SplatRhs
                                    : kInlineVecAddV4I32SplatRhs;
                            helper_argument_types.push_back(ptr_type);
                            helper_argument_types.push_back(
                                rhs_splat_scalar->get_type());
                            helper_arguments.push_back(lhs_address);
                            helper_arguments.push_back(rhs_splat_scalar);
                        } else {
                            helper_argument_types.push_back(ptr_type);
                            helper_argument_types.push_back(ptr_type);
                            helper_arguments.push_back(lhs_address);
                            helper_arguments.push_back(rhs_address);
                        }
                        const CoreIrFunctionType *helper_type =
                            context_->create_type<CoreIrFunctionType>(
                                void_type(), helper_argument_types, false);
                        CoreIrFunction *helper =
                            get_or_create_direct_call_decl(direct_helper_name,
                                                           helper_type);
                        block.create_instruction<CoreIrCallInst>(
                            void_type(), "", helper->get_name(), helper_type,
                            helper_arguments);
                        return true;
                    };
                auto lower_inline_v4i32_reduce_helper =
                    [&](const std::string &helper_name) -> bool {
                        if (arguments.size() != 1 || argument_types.size() != 1) {
                            return false;
                        }
                        if (is_i32x4_vector_type(argument_types.front())) {
                            const CoreIrFunctionType *helper_type =
                                context_->create_type<CoreIrFunctionType>(
                                    return_type,
                                    std::vector<const CoreIrType *>{
                                        argument_types.front()},
                                    false);
                            CoreIrFunction *helper =
                                get_or_create_direct_call_decl(helper_name, helper_type);
                            CoreIrValue *call_value =
                                block.create_instruction<CoreIrCallInst>(
                                    return_type, result_name, helper->get_name(),
                                    helper_type, arguments);
                            return result_name.empty()
                                       ? true
                                       : bind_instruction_result(result_name,
                                                                 call_value, bindings);
                        }
                        if (!is_i32x4_array_type(argument_types.front())) {
                            return false;
                        }
                        const CoreIrType *ptr_type = parse_type_text("ptr");
                        if (ptr_type == nullptr) {
                            return false;
                        }
                        CoreIrValue *vector_address =
                            ensure_addressable_aggregate_typed_operand(
                                argument_types.front(), call_spec->arguments.front(),
                                function, block, bindings, synthetic_index,
                                line_number);
                        if (vector_address == nullptr) {
                            return false;
                        }
                        const CoreIrFunctionType *helper_type =
                            context_->create_type<CoreIrFunctionType>(
                                return_type,
                                std::vector<const CoreIrType *>{ptr_type}, false);
                        CoreIrFunction *helper =
                            get_or_create_direct_call_decl(helper_name, helper_type);
                        CoreIrValue *call_value = block.create_instruction<
                            CoreIrCallInst>(
                            return_type, result_name, helper->get_name(),
                            helper_type, std::vector<CoreIrValue *>{vector_address});
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(result_name,
                                                             call_value, bindings);
                    };
                auto lower_vector_extremum =
                    [&](CoreIrComparePredicate predicate) -> bool {
                        const auto *array_type = as_array_type(return_type);
                        if (arguments.size() != 2 || argument_types.size() != 2 ||
                            array_type == nullptr || argument_types[0] != return_type ||
                            argument_types[1] != return_type) {
                            add_error("unsupported LLVM vector extremum call shape: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        CoreIrValue *lhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[0], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrValue *rhs_address =
                            ensure_addressable_aggregate_typed_operand(
                                return_type, call_spec->arguments[1], function,
                                block, bindings, synthetic_index, line_number);
                        CoreIrStackSlot *result_slot =
                            function.create_stack_slot<CoreIrStackSlot>(
                                result_name.empty()
                                    ? "ll.extremum.vec." +
                                          std::to_string(synthetic_index++)
                                    : result_name,
                                return_type, get_storage_alignment(return_type));
                        CoreIrValue *result_address =
                            materialize_stack_slot_address(block, result_slot,
                                                           synthetic_index);
                        const CoreIrType *i1_type = parse_type_text("i1");
                        if (lhs_address == nullptr || rhs_address == nullptr ||
                            result_address == nullptr || i1_type == nullptr) {
                            return false;
                        }
                        for (std::size_t lane = 0;
                             lane < array_type->get_element_count(); ++lane) {
                            CoreIrValue *lhs_element_address = create_element_address(
                                block, lhs_address, return_type,
                                static_cast<std::uint64_t>(lane),
                                synthetic_index);
                            CoreIrValue *rhs_element_address = create_element_address(
                                block, rhs_address, return_type,
                                static_cast<std::uint64_t>(lane),
                                synthetic_index);
                            CoreIrValue *result_element_address =
                                create_element_address(
                                    block, result_address, return_type,
                                    static_cast<std::uint64_t>(lane),
                                    synthetic_index);
                            if (lhs_element_address == nullptr ||
                                rhs_element_address == nullptr ||
                                result_element_address == nullptr) {
                                return false;
                            }
                            CoreIrValue *lhs_value =
                                block.create_instruction<CoreIrLoadInst>(
                                    array_type->get_element_type(),
                                    "ll.extremum.lhs." +
                                        std::to_string(synthetic_index++),
                                    lhs_element_address);
                            CoreIrValue *rhs_value =
                                block.create_instruction<CoreIrLoadInst>(
                                    array_type->get_element_type(),
                                    "ll.extremum.rhs." +
                                        std::to_string(synthetic_index++),
                                    rhs_element_address);
                            CoreIrValue *compare =
                                block.create_instruction<CoreIrCompareInst>(
                                    predicate, i1_type,
                                    "ll.extremum.cmp." +
                                        std::to_string(synthetic_index++),
                                    lhs_value, rhs_value);
                            CoreIrValue *selected =
                                block.create_instruction<CoreIrSelectInst>(
                                    array_type->get_element_type(),
                                    "ll.extremum.sel." +
                                        std::to_string(synthetic_index++),
                                    compare, lhs_value, rhs_value);
                            block.create_instruction<CoreIrStoreInst>(
                                void_type(), selected, result_element_address);
                        }
                        return bind_stack_slot_result(result_name, result_slot,
                                                      bindings);
                    };
                auto lower_vector_reduce =
                    [&](CoreIrBinaryOpcode opcode,
                        std::optional<CoreIrComparePredicate> predicate,
                        std::size_t vector_argument_index,
                        std::optional<std::size_t> initial_argument_index)
                    -> bool {
                        if (arguments.size() <= vector_argument_index ||
                            argument_types.size() <= vector_argument_index) {
                            add_error("unsupported LLVM vector reduction call shape: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        const CoreIrType *vector_type =
                            argument_types[vector_argument_index];
                        const auto *array_type = as_array_type(vector_type);
                        if (array_type == nullptr) {
                            add_error("unsupported LLVM vector reduction operand: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        if (!predicate.has_value() &&
                            !initial_argument_index.has_value() &&
                            opcode == CoreIrBinaryOpcode::Add &&
                            is_i32x4_array_type(vector_type)) {
                            const CoreIrType *ptr_type = parse_type_text("ptr");
                            if (ptr_type == nullptr) {
                                return false;
                            }
                            CoreIrValue *vector_address =
                                ensure_addressable_aggregate_typed_operand(
                                    vector_type,
                                    call_spec->arguments[vector_argument_index],
                                    function, block, bindings, synthetic_index,
                                    line_number);
                            if (vector_address == nullptr) {
                                return false;
                            }
                            const CoreIrFunctionType *helper_type =
                                context_->create_type<CoreIrFunctionType>(
                                    return_type,
                                    std::vector<const CoreIrType *>{ptr_type},
                                    false);
                            CoreIrFunction *helper =
                                get_or_create_direct_call_decl(
                                    kInlineReduceAddV4I32, helper_type);
                            CoreIrValue *call_value =
                                block.create_instruction<CoreIrCallInst>(
                                    return_type, result_name,
                                    helper->get_name(), helper_type,
                                    std::vector<CoreIrValue *>{vector_address});
                            return result_name.empty()
                                       ? true
                                       : bind_instruction_result(result_name,
                                                                 call_value,
                                                                 bindings);
                        }
                        CoreIrValue *vector_address =
                            ensure_addressable_aggregate_typed_operand(
                                vector_type,
                                call_spec->arguments[vector_argument_index],
                                function, block, bindings, synthetic_index,
                                line_number);
                        if (vector_address == nullptr) {
                            return false;
                        }
                        CoreIrValue *accumulator = nullptr;
                        if (initial_argument_index.has_value()) {
                            if (arguments.size() <= *initial_argument_index ||
                                argument_types.size() <= *initial_argument_index ||
                                argument_types[*initial_argument_index] !=
                                    return_type) {
                                add_error(
                                    "unsupported LLVM vector reduction initial value: " +
                                        line,
                                    line_number, 1);
                                return false;
                            }
                            accumulator = arguments[*initial_argument_index];
                        }
                        const CoreIrType *i1_type = predicate.has_value()
                                                        ? parse_type_text("i1")
                                                        : nullptr;
                        if (predicate.has_value() && i1_type == nullptr) {
                            return false;
                        }
                        for (std::size_t lane = 0;
                             lane < array_type->get_element_count(); ++lane) {
                            CoreIrValue *element_address = create_element_address(
                                block, vector_address, vector_type,
                                static_cast<std::uint64_t>(lane),
                                synthetic_index);
                            if (element_address == nullptr) {
                                return false;
                            }
                            CoreIrValue *element_value =
                                block.create_instruction<CoreIrLoadInst>(
                                    array_type->get_element_type(),
                                    "ll.vec.reduce.load." +
                                        std::to_string(synthetic_index++),
                                    element_address);
                            if (accumulator == nullptr) {
                                accumulator = element_value;
                                continue;
                            }
                            if (predicate.has_value()) {
                                CoreIrValue *compare =
                                    block.create_instruction<CoreIrCompareInst>(
                                        *predicate, i1_type,
                                        "ll.vec.reduce.cmp." +
                                            std::to_string(synthetic_index++),
                                        accumulator, element_value);
                                accumulator =
                                    block.create_instruction<CoreIrSelectInst>(
                                        return_type,
                                        "ll.vec.reduce.sel." +
                                            std::to_string(synthetic_index++),
                                        compare, accumulator, element_value);
                            } else {
                                accumulator =
                                    block.create_instruction<CoreIrBinaryInst>(
                                        opcode, return_type,
                                        "ll.vec.reduce.bin." +
                                            std::to_string(synthetic_index++),
                                        accumulator, element_value);
                            }
                        }
                        if (accumulator == nullptr) {
                            add_error("unsupported empty LLVM vector reduction: " +
                                          line,
                                      line_number, 1);
                            return false;
                        }
                        return result_name.empty()
                                   ? true
                                   : bind_instruction_result(result_name,
                                                             accumulator,
                                                             bindings);
                    };
                if (callee_name.rfind("llvm.lifetime.start", 0) == 0 ||
                    callee_name.rfind("llvm.lifetime.end", 0) == 0) {
                    return true;
                }
                if (callee_name.rfind("llvm.smax.", 0) == 0) {
                    if (is_i32x4_vector_type(return_type)) {
                        return lower_native_v4i32_call(kInlineVecSmaxV4I32);
                    }
                    return as_array_type(return_type) != nullptr
                               ? (lower_inline_v4i32_extremum_helper(
                                      kInlineVecSmaxV4I32) ||
                                  lower_vector_extremum(
                                      CoreIrComparePredicate::SignedGreater))
                               : lower_scalar_extremum(
                                     CoreIrComparePredicate::SignedGreater);
                }
                if (callee_name.rfind("llvm.smin.", 0) == 0) {
                    if (is_i32x4_vector_type(return_type)) {
                        return lower_native_v4i32_call(kInlineVecSminV4I32);
                    }
                    return as_array_type(return_type) != nullptr
                               ? (lower_inline_v4i32_extremum_helper(
                                      kInlineVecSminV4I32) ||
                                  lower_vector_extremum(
                                      CoreIrComparePredicate::SignedLess))
                               : lower_scalar_extremum(
                                     CoreIrComparePredicate::SignedLess);
                }
                if (callee_name.rfind("llvm.umax.", 0) == 0) {
                    return as_array_type(return_type) != nullptr
                               ? lower_vector_extremum(
                                     CoreIrComparePredicate::UnsignedGreater)
                               : lower_scalar_extremum(
                                     CoreIrComparePredicate::UnsignedGreater);
                }
                if (callee_name.rfind("llvm.umin.", 0) == 0) {
                    return as_array_type(return_type) != nullptr
                               ? lower_vector_extremum(
                                     CoreIrComparePredicate::UnsignedLess)
                               : lower_scalar_extremum(
                                     CoreIrComparePredicate::UnsignedLess);
                }
                if (callee_name.rfind("llvm.vector.reduce.fadd.", 0) == 0) {
                    return lower_vector_reduce(CoreIrBinaryOpcode::Add,
                                               std::nullopt, 1, 0);
                }
                if (callee_name.rfind("llvm.vector.reduce.add.", 0) == 0) {
                    return lower_inline_v4i32_reduce_helper(
                               kInlineReduceAddV4I32) ||
                           lower_vector_reduce(CoreIrBinaryOpcode::Add,
                                               std::nullopt, 0, std::nullopt);
                }
                if (callee_name.rfind("llvm.vector.reduce.smax.", 0) == 0) {
                    return lower_inline_v4i32_reduce_helper(
                               kInlineReduceSmaxV4I32) ||
                           lower_vector_reduce(
                               CoreIrBinaryOpcode::Add,
                               CoreIrComparePredicate::SignedGreater, 0,
                               std::nullopt);
                }
                if (callee_name.rfind("llvm.vector.reduce.smin.", 0) == 0) {
                    return lower_inline_v4i32_reduce_helper(
                               kInlineReduceSminV4I32) ||
                           lower_vector_reduce(
                               CoreIrBinaryOpcode::Add,
                               CoreIrComparePredicate::SignedLess, 0,
                               std::nullopt);
                }
                if (callee_name.rfind("llvm.vector.reduce.umax.", 0) == 0) {
                    return lower_vector_reduce(
                        CoreIrBinaryOpcode::Add,
                        CoreIrComparePredicate::UnsignedGreater, 0,
                        std::nullopt);
                }
                if (callee_name.rfind("llvm.vector.reduce.umin.", 0) == 0) {
                    return lower_vector_reduce(
                        CoreIrBinaryOpcode::Add,
                        CoreIrComparePredicate::UnsignedLess, 0,
                        std::nullopt);
                }
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
                if (!ensure_argument_prefix(arguments.size())) {
                    add_error("unsupported LLVM direct call arguments: " + line,
                              line_number, 1);
                    return false;
                }
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
                const CoreIrFunctionType *actual_callee_type =
                    callee->get_function_type() != nullptr
                        ? callee->get_function_type()
                        : callee_type;
                CoreIrCallInst *call_value = block.create_instruction<CoreIrCallInst>(
                    return_type, result_name, callee->get_name(), actual_callee_type,
                    arguments);
                apply_variadic_even_gpr_pair_hints(call_value, actual_callee_type);
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
            if (!ensure_argument_prefix(arguments.size())) {
                add_error("unsupported LLVM indirect call arguments: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *callee_value = resolve_typed_value_operand(
                pointer_to(callee_type), call_spec->callee, block, bindings,
                synthetic_index, line_number);
            if (callee_value == nullptr) {
                return false;
            }
            CoreIrCallInst *call_value = block.create_instruction<CoreIrCallInst>(
                return_type, result_name, callee_value, callee_type, arguments);
            apply_variadic_even_gpr_pair_hints(call_value, callee_type);
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
            if (is_aggregate_type(type)) {
                CoreIrPhiInst *phi_address =
                    block.create_instruction<CoreIrPhiInst>(
                        pointer_to(type),
                        result_name.empty()
                            ? "ll.agg.phi.addr." +
                                  std::to_string(synthetic_index++)
                            : result_name + ".addr");
                if (!bind_aggregate_address_result(result_name, phi_address,
                                                  bindings)) {
                    return false;
                }
                for (const AArch64LlvmImportPhiIncoming &incoming :
                     phi_spec->incoming_values) {
                    auto block_it = block_map.find(incoming.block_label);
                    if (block_it == block_map.end() &&
                        incoming.block_label == "1") {
                        block_it = block_map.find("0");
                    }
                    if (block_it == block_map.end()) {
                        add_error("unsupported LLVM phi incoming block: " +
                                      incoming.block_label,
                                  line_number, 1);
                        return false;
                    }
                    PendingAggregatePhiIncoming pending_incoming;
                    pending_incoming.phi = phi_address;
                    pending_incoming.incoming_block = block_it->second;
                    pending_incoming.type = type;
                    pending_incoming.value = incoming.value;
                    pending_incoming.line_number = line_number;
                    pending_aggregate_phi_incomings.push_back(
                        std::move(pending_incoming));
                }
                return true;
            }
            CoreIrPhiInst *phi =
                block.create_instruction<CoreIrPhiInst>(type, result_name);
            for (const AArch64LlvmImportPhiIncoming &incoming :
                 phi_spec->incoming_values) {
                auto block_it = block_map.find(incoming.block_label);
                if (block_it == block_map.end() && incoming.block_label == "1") {
                    block_it = block_map.find("0");
                }
                if (block_it == block_map.end()) {
                    add_error("unsupported LLVM phi incoming block: " +
                                  incoming.block_label,
                              line_number, 1);
                    return false;
                }
                if (incoming.value.kind == AArch64LlvmImportValueKind::Local &&
                    bindings.find(incoming.value.local_name) == bindings.end()) {
                    PendingPhiIncoming pending_incoming;
                    pending_incoming.phi = phi;
                    pending_incoming.phi_block = &block;
                    pending_incoming.incoming_block = block_it->second;
                    pending_incoming.type = type;
                    pending_incoming.value = incoming.value;
                    pending_incoming.line_number = line_number;
                    pending_phi_incomings.push_back(std::move(pending_incoming));
                    continue;
                }
                CoreIrValue *incoming_value = resolve_typed_value_operand(
                    type, incoming.value, block, bindings, synthetic_index,
                    line_number);
                if (incoming_value == nullptr) {
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
        if (starts_with(instruction_text, "atomicrmw ")) {
            std::string payload = trim_copy(instruction_text.substr(10));
            payload = strip_leading_modifiers(payload);
            const std::size_t opcode_end = payload.find(' ');
            if (opcode_end == std::string::npos) {
                add_error("unsupported LLVM atomicrmw instruction: " + line,
                          line_number, 1);
                return false;
            }
            const std::string rmw_opcode = payload.substr(0, opcode_end);
            const std::vector<std::string> operands =
                split_top_level(trim_copy(payload.substr(opcode_end + 1)), ',');
            if (operands.size() < 2) {
                add_error("unsupported LLVM atomicrmw instruction: " + line,
                          line_number, 1);
                return false;
            }
            const auto address_operand = parse_typed_value_text(operands[0]);
            const auto value_operand = parse_typed_value_text(
                strip_trailing_atomic_order_tokens(operands[1], 1));
            if (!address_operand.has_value() || !value_operand.has_value()) {
                add_error("unsupported LLVM atomicrmw operand shape: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *value_type = lower_import_type(value_operand->type);
            if (value_type == nullptr) {
                add_error("unsupported LLVM atomicrmw value type: " + line,
                          line_number, 1);
                return false;
            }
            ResolvedAddress address = resolve_typed_address_operand(
                *address_operand, block, bindings, synthetic_index, line_number);
            CoreIrValue *old_value = nullptr;
            if (address.stack_slot != nullptr) {
                old_value = block.create_instruction<CoreIrLoadInst>(
                    value_type,
                    "ll.atomicrmw.old." + std::to_string(synthetic_index++),
                    address.stack_slot);
            } else if (address.address_value != nullptr) {
                old_value = block.create_instruction<CoreIrLoadInst>(
                    value_type,
                    "ll.atomicrmw.old." + std::to_string(synthetic_index++),
                    address.address_value);
            }
            CoreIrValue *operand_value = resolve_typed_value_operand(
                value_type, *value_operand, block, bindings, synthetic_index,
                line_number);
            if (old_value == nullptr || operand_value == nullptr) {
                add_error("unsupported LLVM atomicrmw operand values: " + line,
                          line_number, 1);
                return false;
            }

            CoreIrValue *new_value = nullptr;
            if (rmw_opcode == "xchg") {
                new_value = operand_value;
            } else if (rmw_opcode == "add" || rmw_opcode == "sub") {
                new_value = block.create_instruction<CoreIrBinaryInst>(
                    rmw_opcode == "add" ? CoreIrBinaryOpcode::Add
                                        : CoreIrBinaryOpcode::Sub,
                    value_type,
                    "ll.atomicrmw.new." + std::to_string(synthetic_index++),
                    old_value, operand_value);
            } else {
                add_error("unsupported LLVM atomicrmw opcode: " + line,
                          line_number, 1);
                return false;
            }

            if (address.stack_slot != nullptr) {
                block.create_instruction<CoreIrStoreInst>(void_type(), new_value,
                                                          address.stack_slot);
            } else if (address.address_value != nullptr) {
                block.create_instruction<CoreIrStoreInst>(void_type(), new_value,
                                                          address.address_value);
            } else {
                add_error("unsupported LLVM atomicrmw address: " + line,
                          line_number, 1);
                return false;
            }
            return bind_instruction_result(result_name, old_value, bindings);
        }
        if (starts_with(instruction_text, "cmpxchg ")) {
            std::string payload = trim_copy(instruction_text.substr(8));
            payload = strip_leading_modifiers(payload);
            const std::vector<std::string> operands =
                split_top_level(payload, ',');
            if (operands.size() < 3) {
                add_error("unsupported LLVM cmpxchg instruction: " + line,
                          line_number, 1);
                return false;
            }
            const auto address_operand = parse_typed_value_text(operands[0]);
            const auto expected_operand = parse_typed_value_text(operands[1]);
            const auto desired_operand = parse_typed_value_text(
                strip_trailing_atomic_order_tokens(operands[2], 2));
            if (!address_operand.has_value() || !expected_operand.has_value() ||
                !desired_operand.has_value()) {
                add_error("unsupported LLVM cmpxchg operand shape: " + line,
                          line_number, 1);
                return false;
            }
            const CoreIrType *value_type = lower_import_type(expected_operand->type);
            const CoreIrType *i1_type = parse_type_text("i1");
            const CoreIrType *result_type =
                (value_type == nullptr || i1_type == nullptr)
                    ? nullptr
                    : context_->create_type<CoreIrStructType>(
                          std::vector<const CoreIrType *>{value_type, i1_type});
            if (value_type == nullptr || i1_type == nullptr || result_type == nullptr) {
                add_error("unsupported LLVM cmpxchg value type: " + line,
                          line_number, 1);
                return false;
            }
            ResolvedAddress address = resolve_typed_address_operand(
                *address_operand, block, bindings, synthetic_index, line_number);
            CoreIrValue *old_value = nullptr;
            if (address.stack_slot != nullptr) {
                old_value = block.create_instruction<CoreIrLoadInst>(
                    value_type,
                    "ll.cmpxchg.old." + std::to_string(synthetic_index++),
                    address.stack_slot);
            } else if (address.address_value != nullptr) {
                old_value = block.create_instruction<CoreIrLoadInst>(
                    value_type,
                    "ll.cmpxchg.old." + std::to_string(synthetic_index++),
                    address.address_value);
            }
            CoreIrValue *expected_value = resolve_typed_value_operand(
                value_type, *expected_operand, block, bindings, synthetic_index,
                line_number);
            CoreIrValue *desired_value = resolve_typed_value_operand(
                value_type, *desired_operand, block, bindings, synthetic_index,
                line_number);
            if (old_value == nullptr || expected_value == nullptr ||
                desired_value == nullptr) {
                add_error("unsupported LLVM cmpxchg operand values: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrValue *success = block.create_instruction<CoreIrCompareInst>(
                CoreIrComparePredicate::Equal, i1_type,
                "ll.cmpxchg.success." + std::to_string(synthetic_index++), old_value,
                expected_value);
            CoreIrValue *stored_value = block.create_instruction<CoreIrSelectInst>(
                value_type,
                "ll.cmpxchg.store." + std::to_string(synthetic_index++), success,
                desired_value, old_value);
            if (address.stack_slot != nullptr) {
                block.create_instruction<CoreIrStoreInst>(void_type(), stored_value,
                                                          address.stack_slot);
            } else if (address.address_value != nullptr) {
                block.create_instruction<CoreIrStoreInst>(void_type(), stored_value,
                                                          address.address_value);
            } else {
                add_error("unsupported LLVM cmpxchg address: " + line,
                          line_number, 1);
                return false;
            }
            CoreIrStackSlot *result_slot = function.create_stack_slot<CoreIrStackSlot>(
                result_name.empty()
                    ? "ll.cmpxchg.result." + std::to_string(synthetic_index++)
                    : result_name,
                result_type, get_storage_alignment(result_type));
            CoreIrValue *result_address = materialize_stack_slot_address(
                block, result_slot, synthetic_index);
            CoreIrValue *value_address = create_aggregate_subvalue_address(
                block, result_address, result_type, 0, synthetic_index);
            CoreIrValue *flag_address = create_aggregate_subvalue_address(
                block, result_address, result_type, 1, synthetic_index);
            if (result_address == nullptr || value_address == nullptr ||
                flag_address == nullptr) {
                return false;
            }
            block.create_instruction<CoreIrStoreInst>(void_type(), old_value,
                                                      value_address);
            block.create_instruction<CoreIrStoreInst>(void_type(), success,
                                                      flag_address);
            return bind_stack_slot_result(result_name, result_slot, bindings);
        }
        if (starts_with(instruction_text, "extractvalue ")) {
            const std::vector<std::string> operands =
                split_top_level(trim_copy(instruction_text.substr(13)), ',');
            if (operands.size() < 2) {
                add_error("unsupported LLVM extractvalue instruction: " + line,
                          line_number, 1);
                return false;
            }
            const auto aggregate_operand = parse_typed_value_text(operands.front());
            if (!aggregate_operand.has_value()) {
                add_error("unsupported LLVM extractvalue aggregate operand: " + line,
                          line_number, 1);
                return false;
            }
            if (aggregate_operand->kind == AArch64LlvmImportValueKind::Local) {
                if (auto binding_it = bindings.find(aggregate_operand->local_name);
                    binding_it != bindings.end() &&
                    binding_it->second.wide_overflow_aggregate.has_value()) {
                    const auto element_index =
                        operands.size() == 2
                            ? parse_integer_literal(trim_copy(operands[1]))
                            : std::nullopt;
                    if (!element_index.has_value()) {
                        add_error("unsupported LLVM extractvalue index: " + line,
                                  line_number, 1);
                        return false;
                    }
                    if (*element_index == 0) {
                        return bind_wide_integer_result(
                            result_name,
                            binding_it->second.wide_overflow_aggregate->wide_value,
                            bindings);
                    }
                    if (*element_index == 1 &&
                        binding_it->second.wide_overflow_aggregate->overflow_flag !=
                            nullptr) {
                        return bind_instruction_result(
                            result_name,
                            binding_it->second.wide_overflow_aggregate->overflow_flag,
                            bindings);
                    }
                    add_error("unsupported LLVM extractvalue path: " + line,
                              line_number, 1);
                    return false;
                }
            }
            const CoreIrType *aggregate_type =
                lower_import_type(aggregate_operand->type);
            CoreIrValue *current_address =
                aggregate_type != nullptr
                    ? ensure_addressable_aggregate_typed_operand(
                          aggregate_type, *aggregate_operand, function, block,
                          bindings, synthetic_index, line_number)
                    : nullptr;
            const CoreIrType *current_type = aggregate_type;
            if (current_address == nullptr || current_type == nullptr) {
                add_error("unsupported LLVM extractvalue aggregate operand: " + line,
                          line_number, 1);
                return false;
            }
            for (std::size_t index = 1; index < operands.size(); ++index) {
                const auto element_index =
                    parse_integer_literal(trim_copy(operands[index]));
                if (!element_index.has_value()) {
                    add_error("unsupported LLVM extractvalue index: " + line,
                              line_number, 1);
                    return false;
                }
                const CoreIrType *next_type =
                    aggregate_element_type(current_type, *element_index);
                CoreIrValue *next_address = create_aggregate_subvalue_address(
                    block, current_address, current_type, *element_index,
                    synthetic_index);
                if (next_type == nullptr || next_address == nullptr) {
                    add_error("unsupported LLVM extractvalue path: " + line,
                              line_number, 1);
                    return false;
                }
                current_type = next_type;
                current_address = next_address;
            }
            return bind_instruction_result(
                result_name,
                block.create_instruction<CoreIrLoadInst>(current_type, result_name,
                                                         current_address),
                bindings);
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
            CoreIrValue *return_value = nullptr;
            if (return_type != nullptr && is_aggregate_type(return_type)) {
                CoreIrValue *source_address = ensure_addressable_aggregate_typed_operand(
                    return_type, return_spec->value, function, block, bindings,
                    synthetic_index, line_number);
                CoreIrStackSlot *result_slot =
                    function.create_stack_slot<CoreIrStackSlot>(
                        "ll.ret.agg." + std::to_string(synthetic_index++),
                        return_type, get_storage_alignment(return_type));
                CoreIrValue *result_address = materialize_stack_slot_address(
                    block, result_slot, synthetic_index);
                if (source_address == nullptr || result_address == nullptr ||
                    !copy_aggregate_between_addresses(block, return_type,
                                                     result_address, source_address,
                                                     synthetic_index)) {
                    add_error("unsupported LLVM aggregate return operand",
                              line_number, 1);
                    return false;
                }
                return_value = block.create_instruction<CoreIrLoadInst>(
                    return_type,
                    "ll.ret.agg.load." + std::to_string(synthetic_index++),
                    result_slot);
            } else {
                return_value = resolve_typed_value_operand(
                    return_type, return_spec->value, block, bindings,
                    synthetic_index, line_number);
            }
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
                block_map[std::to_string(function.get_parameters().size())] = block;
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
        std::vector<PendingPhiIncoming> pending_phi_incomings;
        std::vector<PendingAggregatePhiIncoming> pending_aggregate_phi_incomings;
        const auto compute_lowering_block_order =
            [&](const PendingFunctionDefinition &pending_definition) {
                std::vector<const AArch64LlvmImportBasicBlock *> ordered_blocks;
                ordered_blocks.reserve(pending_definition.basic_blocks.size());
                if (pending_definition.basic_blocks.size() <= 1) {
                    for (const AArch64LlvmImportBasicBlock &block :
                         pending_definition.basic_blocks) {
                        ordered_blocks.push_back(&block);
                    }
                    return ordered_blocks;
                }
                if (pending_definition.basic_blocks.size() > 512) {
                    for (const AArch64LlvmImportBasicBlock &block :
                         pending_definition.basic_blocks) {
                        ordered_blocks.push_back(&block);
                    }
                    return ordered_blocks;
                }

                std::unordered_map<std::string, std::size_t> label_to_index;
                label_to_index.reserve(pending_definition.basic_blocks.size());
                for (std::size_t index = 0;
                     index < pending_definition.basic_blocks.size(); ++index) {
                    label_to_index.emplace(
                        pending_definition.basic_blocks[index].label, index);
                }

                std::vector<std::vector<std::size_t>> successors(
                    pending_definition.basic_blocks.size());
                std::vector<std::vector<std::size_t>> predecessors(
                    pending_definition.basic_blocks.size());
                const auto add_edge = [&](std::size_t source_index,
                                          const std::string &target_label) {
                    const auto it = label_to_index.find(target_label);
                    if (it == label_to_index.end()) {
                        return;
                    }
                    successors[source_index].push_back(it->second);
                    predecessors[it->second].push_back(source_index);
                };

                for (std::size_t index = 0;
                     index < pending_definition.basic_blocks.size(); ++index) {
                    const AArch64LlvmImportBasicBlock &block =
                        pending_definition.basic_blocks[index];
                    if (block.instructions.empty()) {
                        continue;
                    }
                    const AArch64LlvmImportInstruction &terminator =
                        block.instructions.back();
                    if (const auto branch =
                            parse_llvm_import_branch_spec(terminator);
                        branch.has_value()) {
                        add_edge(index, branch->true_target_label);
                        if (branch->is_conditional) {
                            add_edge(index, branch->false_target_label);
                        }
                        continue;
                    }
                    if (const auto switch_spec =
                            parse_llvm_import_switch_spec(terminator);
                        switch_spec.has_value()) {
                        add_edge(index, switch_spec->default_target_label);
                        for (const AArch64LlvmImportSwitchCase &switch_case :
                             switch_spec->cases) {
                            add_edge(index, switch_case.target_label);
                        }
                        continue;
                    }
                    if (const auto indirect_branch =
                            parse_llvm_import_indirect_branch_spec(terminator);
                        indirect_branch.has_value()) {
                        for (const std::string &target_label :
                             indirect_branch->target_labels) {
                            add_edge(index, target_label);
                        }
                    }
                }

                const std::size_t block_count =
                    pending_definition.basic_blocks.size();
                std::vector<std::vector<bool>> dominates(
                    block_count, std::vector<bool>(block_count, true));
                for (std::size_t index = 0; index < block_count; ++index) {
                    if (index == 0) {
                        dominates[index].assign(block_count, false);
                        dominates[index][0] = true;
                        continue;
                    }
                    if (predecessors[index].empty()) {
                        dominates[index].assign(block_count, false);
                        dominates[index][index] = true;
                    }
                }

                bool changed = true;
                while (changed) {
                    changed = false;
                    for (std::size_t index = 1; index < block_count; ++index) {
                        if (predecessors[index].empty()) {
                            continue;
                        }
                        std::vector<bool> new_dominators =
                            dominates[predecessors[index].front()];
                        for (std::size_t predecessor_index = 1;
                             predecessor_index < predecessors[index].size();
                             ++predecessor_index) {
                            const std::size_t predecessor =
                                predecessors[index][predecessor_index];
                            for (std::size_t candidate = 0;
                                 candidate < block_count; ++candidate) {
                                new_dominators[candidate] =
                                    new_dominators[candidate] &&
                                    dominates[predecessor][candidate];
                            }
                        }
                        new_dominators[index] = true;
                        if (new_dominators != dominates[index]) {
                            dominates[index] = std::move(new_dominators);
                            changed = true;
                        }
                    }
                }

                std::vector<std::vector<std::size_t>> dom_tree_children(
                    block_count);
                for (std::size_t index = 1; index < block_count; ++index) {
                    if (predecessors[index].empty()) {
                        continue;
                    }
                    std::optional<std::size_t> immediate_dominator;
                    for (std::size_t candidate = 0; candidate < block_count;
                         ++candidate) {
                        if (candidate == index || !dominates[index][candidate]) {
                            continue;
                        }
                        bool is_immediate = true;
                        for (std::size_t other = 0; other < block_count; ++other) {
                            if (other == index || other == candidate ||
                                !dominates[index][other]) {
                                continue;
                            }
                            if (!dominates[candidate][other]) {
                                is_immediate = false;
                                break;
                            }
                        }
                        if (is_immediate) {
                            immediate_dominator = candidate;
                            break;
                        }
                    }
                    if (immediate_dominator.has_value()) {
                        dom_tree_children[*immediate_dominator].push_back(index);
                    }
                }

                for (std::vector<std::size_t> &children : dom_tree_children) {
                    std::sort(children.begin(), children.end());
                }

                std::vector<bool> visited(block_count, false);
                const auto visit = [&](auto &&self, std::size_t index) -> void {
                    if (visited[index]) {
                        return;
                    }
                    visited[index] = true;
                    ordered_blocks.push_back(
                        &pending_definition.basic_blocks[index]);
                    for (const std::size_t child : dom_tree_children[index]) {
                        self(self, child);
                    }
                };

                visit(visit, 0);
                for (std::size_t index = 0; index < block_count; ++index) {
                    if (!visited[index]) {
                        ordered_blocks.push_back(
                            &pending_definition.basic_blocks[index]);
                    }
                }
                return ordered_blocks;
            };

        for (const AArch64LlvmImportBasicBlock *record :
             compute_lowering_block_order(pending)) {
            if (record == nullptr) {
                continue;
            }
            CoreIrBasicBlock *current_block = block_map.at(record->label);
            for (const AArch64LlvmImportInstruction &instruction :
                 record->instructions) {
                if (!parse_instruction(instruction, function, current_block,
                                       block_map, bindings, synthetic_index,
                                       pending_phi_incomings,
                                       pending_aggregate_phi_incomings)) {
                    return false;
                }
            }
        }

        for (const PendingAggregatePhiIncoming &pending_incoming :
             pending_aggregate_phi_incomings) {
            std::unique_ptr<CoreIrInstruction> detached_terminator;
            auto &incoming_instructions =
                pending_incoming.incoming_block->get_instructions();
            if (!incoming_instructions.empty() &&
                incoming_instructions.back() != nullptr &&
                incoming_instructions.back()->get_is_terminator()) {
                detached_terminator = std::move(incoming_instructions.back());
                incoming_instructions.pop_back();
            }
            CoreIrValue *incoming_address =
                ensure_addressable_aggregate_typed_operand(
                    pending_incoming.type, pending_incoming.value, function,
                    *pending_incoming.incoming_block, bindings, synthetic_index,
                    pending_incoming.line_number);
            if (detached_terminator != nullptr) {
                pending_incoming.incoming_block->append_instruction(
                    std::move(detached_terminator));
            }
            if (incoming_address == nullptr) {
                return false;
            }
            pending_incoming.phi->add_incoming(pending_incoming.incoming_block,
                                               incoming_address);
        }

        for (const PendingPhiIncoming &pending_incoming : pending_phi_incomings) {
            std::unique_ptr<CoreIrInstruction> detached_terminator;
            auto &incoming_instructions =
                pending_incoming.incoming_block->get_instructions();
            if (!incoming_instructions.empty() &&
                incoming_instructions.back() != nullptr &&
                incoming_instructions.back()->get_is_terminator()) {
                detached_terminator = std::move(incoming_instructions.back());
                incoming_instructions.pop_back();
            }
            CoreIrValue *incoming_value = resolve_typed_value_operand(
                pending_incoming.type, pending_incoming.value,
                *pending_incoming.incoming_block, bindings, synthetic_index,
                pending_incoming.line_number);
            if (detached_terminator != nullptr) {
                pending_incoming.incoming_block->append_instruction(
                    std::move(detached_terminator));
            }
            if (incoming_value == nullptr) {
                add_error("unknown LLVM local value: %" +
                              pending_incoming.value.local_name,
                          pending_incoming.line_number, 1);
                return false;
            }
            pending_incoming.phi->add_incoming(pending_incoming.incoming_block,
                                               incoming_value);
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

        std::vector<bool> lowered_named_types(parsed_module.named_types.size(), false);
        std::size_t remaining_named_types = parsed_module.named_types.size();
        while (remaining_named_types != 0) {
            bool made_progress = false;
            for (std::size_t index = 0; index < parsed_module.named_types.size(); ++index) {
                if (lowered_named_types[index]) {
                    continue;
                }
                const AArch64LlvmImportNamedType &named_type =
                    parsed_module.named_types[index];
                if (named_type.is_opaque || named_type.body_text == "opaque") {
                    if (!lower_named_type_definition(named_type)) {
                        remaining_named_types = 0;
                        break;
                    }
                    lowered_named_types[index] = true;
                    --remaining_named_types;
                    made_progress = true;
                    continue;
                }
                const CoreIrType *type = lower_import_type(named_type.body_type);
                if (type == nullptr) {
                    continue;
                }
                named_type_cache_[named_type.name] = type;
                lowered_named_types[index] = true;
                --remaining_named_types;
                made_progress = true;
            }
            if (!diagnostics_.empty() || remaining_named_types == 0) {
                break;
            }
            if (!made_progress) {
                for (std::size_t index = 0; index < parsed_module.named_types.size();
                     ++index) {
                    if (!lowered_named_types[index]) {
                        add_error("unsupported LLVM named type body: " +
                                      parsed_module.named_types[index].body_text,
                                  parsed_module.named_types[index].line, 1);
                        break;
                    }
                }
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

        for (const AArch64LlvmImportGlobal &global : parsed_module.globals) {
            if (!declare_global_definition(global)) {
                break;
            }
        }

        for (const AArch64LlvmImportGlobal &global : parsed_module.globals) {
            if (!lower_global_definition(global)) {
                break;
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
