#include "backend/ir/ir_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/detail/aggregate_layout.hpp"
#include "backend/ir/detail/symbol_value_map.hpp"
#include "backend/ir/gnu_function_attribute_lowering_handler.hpp"
#include "backend/ir/ir_backend.hpp"
#include "backend/ir/ir_result.hpp"
#include "common/integer_literal.hpp"
#include "common/diagnostic/diagnostic.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc {

namespace {

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

struct EmissionResult {
    bool success = false;
    bool terminated = false;
};

struct EmissionState {
    detail::SymbolValueMap symbol_value_map;
    std::vector<std::string> loop_continue_labels;
    std::vector<std::string> break_labels;
    std::unordered_map<std::string, std::string> goto_labels;
    const std::unordered_set<std::string> *defined_function_names = nullptr;
};

const SemanticModel *get_semantic_model(const CompilerContext &context) {
    return context.get_semantic_model();
}

const SemanticType *get_node_type(const CompilerContext &context,
                                  const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return nullptr;
    }
    return semantic_model->get_node_type(node);
}

const SemanticSymbol *get_symbol_binding(const CompilerContext &context,
                                         const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return nullptr;
    }
    return semantic_model->get_symbol_binding(node);
}

const VariableSemanticInfo *get_variable_info(const CompilerContext &context,
                                              const SemanticSymbol *symbol) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || symbol == nullptr) {
        return nullptr;
    }
    return semantic_model->get_variable_info(symbol);
}

bool is_builtin_type_named(const SemanticType *type, const char *name) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    return static_cast<const BuiltinSemanticType *>(type)->get_name() == name;
}

bool is_supported_integer_type(const SemanticType *type) {
    return is_builtin_type_named(type, "int") ||
           is_builtin_type_named(type, "ptrdiff_t") ||
           is_builtin_type_named(type, "unsigned int") ||
           is_builtin_type_named(type, "char") ||
           is_builtin_type_named(type, "signed char") ||
           is_builtin_type_named(type, "unsigned char") ||
           is_builtin_type_named(type, "short") ||
           is_builtin_type_named(type, "unsigned short") ||
           is_builtin_type_named(type, "long int") ||
           is_builtin_type_named(type, "unsigned long") ||
           is_builtin_type_named(type, "long long int") ||
           is_builtin_type_named(type, "unsigned long long");
}

bool is_supported_scalar_storage_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    return is_builtin_type_named(type, "int") ||
           is_builtin_type_named(type, "ptrdiff_t") ||
           is_builtin_type_named(type, "unsigned int") ||
           is_builtin_type_named(type, "char") ||
           is_builtin_type_named(type, "signed char") ||
           is_builtin_type_named(type, "unsigned char") ||
           is_builtin_type_named(type, "short") ||
           is_builtin_type_named(type, "unsigned short") ||
           is_builtin_type_named(type, "long int") ||
           is_builtin_type_named(type, "unsigned long") ||
           is_builtin_type_named(type, "long long int") ||
           is_builtin_type_named(type, "unsigned long long") ||
           is_builtin_type_named(type, "float") ||
           is_builtin_type_named(type, "double") ||
           is_builtin_type_named(type, "_Float16") ||
           is_builtin_type_named(type, "long double") ||
               strip_qualifiers(type) != nullptr &&
               strip_qualifiers(type)->get_kind() == SemanticTypeKind::Pointer;
}

bool is_supported_storage_type(const SemanticType *type);

bool is_supported_array_storage_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Array) {
        return false;
    }

    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    if (array_type->get_dimensions().empty()) {
        return false;
    }
    for (int dimension : array_type->get_dimensions()) {
        if (dimension <= 0) {
            return false;
        }
    }

    return is_supported_storage_type(array_type->get_element_type());
}

bool is_supported_extern_incomplete_array_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Array) {
        return false;
    }

    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    const auto &dimensions = array_type->get_dimensions();
    if (dimensions.empty()) {
        return false;
    }

    bool saw_incomplete_dimension = false;
    for (int dimension : dimensions) {
        if (dimension < 0) {
            return false;
        }
        if (dimension == 0) {
            saw_incomplete_dimension = true;
        }
    }

    return saw_incomplete_dimension &&
           is_supported_storage_type(array_type->get_element_type());
}

bool is_supported_aggregate_storage_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        for (const auto &field : struct_type->get_fields()) {
            if (!is_supported_storage_type(field.get_type())) {
                return false;
            }
        }
        return true;
    }
    if (type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        for (const auto &field : union_type->get_fields()) {
            if (!is_supported_storage_type(field.get_type())) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_supported_storage_type(const SemanticType *type) {
    return is_supported_scalar_storage_type(type) ||
           is_supported_array_storage_type(type) ||
           is_supported_aggregate_storage_type(type);
}

bool is_supported_return_type(const SemanticType *type) {
    return is_builtin_type_named(type, "void") ||
           is_supported_storage_type(type);
}

std::optional<long long> get_integer_constant_value(const CompilerContext &context,
                                                    const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return std::nullopt;
    }
    return semantic_model->get_integer_constant_value(node);
}

bool is_supported_arithmetic_operator(const std::string &op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

bool is_supported_comparison_operator(const std::string &op) {
    return op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" ||
           op == "!=";
}

bool is_supported_bitwise_operator(const std::string &op) {
    return op == "&" || op == "|" || op == "^";
}

bool is_supported_shift_operator(const std::string &op) {
    return op == "<<" || op == ">>";
}

bool is_supported_logical_operator(const std::string &op) {
    return op == "&&" || op == "||";
}

bool is_supported_unary_operator(const std::string &op) {
    return op == "&" || op == "*" || op == "+" || op == "-" || op == "~" ||
           op == "!";
}

bool is_supported_compound_assignment_operator(const std::string &op) {
    return op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
           op == "%=" || op == "<<=" || op == ">>=" || op == "&=" ||
           op == "^=" || op == "|=";
}

std::string get_compound_assignment_binary_operator(const std::string &op) {
    if (op == "+=") {
        return "+";
    }
    if (op == "-=") {
        return "-";
    }
    if (op == "*=") {
        return "*";
    }
    if (op == "/=") {
        return "/";
    }
    if (op == "%=") {
        return "%";
    }
    if (op == "<<=") {
        return "<<";
    }
    if (op == ">>=") {
        return ">>";
    }
    if (op == "&=") {
        return "&";
    }
    if (op == "^=") {
        return "^";
    }
    if (op == "|=") {
        return "|";
    }
    return {};
}

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type);

const SemanticType *get_member_owner_type(const SemanticType *type,
                                          const std::string &op);

bool get_member_info(const SemanticType *owner_type,
                     const std::string &member_name, std::size_t &field_index,
                     const SemanticType *&field_type);

const SemanticType *get_usual_arithmetic_conversion_type(
    const CompilerContext &context, const Expr *lhs_expr,
    const SemanticType *lhs_type, const Expr *rhs_expr,
    const SemanticType *rhs_type);

const SemanticType *get_usual_arithmetic_conversion_type(
    const CompilerContext &context, const SemanticType *lhs_type,
    const SemanticType *rhs_type) {
    return get_usual_arithmetic_conversion_type(context, nullptr, lhs_type,
                                                nullptr, rhs_type);
}

std::optional<int> get_bit_field_width_for_expr(const CompilerContext &context,
                                                const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    if (expr->get_kind() == AstKind::MemberExpr) {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        const SemanticType *owner_type =
            get_member_owner_type(get_node_type(context, member_expr->get_base()),
                                  member_expr->get_operator_text());
        std::size_t field_index = 0;
        const SemanticType *field_type = nullptr;
        if (!get_member_info(owner_type, member_expr->get_member_name(),
                             field_index, field_type)) {
            return std::nullopt;
        }

        const auto field_layout =
            detail::get_aggregate_field_layout(owner_type, field_index);
        if (!field_layout.has_value() || !field_layout->is_bit_field) {
            return std::nullopt;
        }
        return static_cast<int>(field_layout->bit_width);
    }

    if (expr->get_kind() == AstKind::BinaryExpr) {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        if (binary_expr->get_operator_text() == ",") {
            return get_bit_field_width_for_expr(context, binary_expr->get_rhs());
        }
        return std::nullopt;
    }

    if (expr->get_kind() == AstKind::AssignExpr) {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        return get_bit_field_width_for_expr(context, assign_expr->get_target());
    }

    if (expr->get_kind() == AstKind::PrefixExpr) {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        return get_bit_field_width_for_expr(context, prefix_expr->get_operand());
    }

    if (expr->get_kind() == AstKind::PostfixExpr) {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        return get_bit_field_width_for_expr(context, postfix_expr->get_operand());
    }

    if (expr->get_kind() == AstKind::ConditionalExpr) {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        const auto true_width =
            get_bit_field_width_for_expr(context, conditional_expr->get_true_expr());
        const auto false_width = get_bit_field_width_for_expr(
            context, conditional_expr->get_false_expr());
        if (true_width.has_value() && false_width.has_value() &&
            *true_width == *false_width) {
            return true_width;
        }
    }

    return std::nullopt;
}

const SemanticType *get_usual_arithmetic_conversion_type(
    const CompilerContext &context, const Expr *lhs_expr,
    const SemanticType *lhs_type, const Expr *rhs_expr,
    const SemanticType *rhs_type) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || lhs_type == nullptr || rhs_type == nullptr) {
        return nullptr;
    }

    detail::ConversionChecker conversion_checker(
        &context.get_dialect_manager().get_semantic_feature_registry(),
        &context.get_dialect_manager().get_builtin_type_semantic_handler_registry());
    if (!conversion_checker.is_arithmetic_type(lhs_type) ||
        !conversion_checker.is_arithmetic_type(rhs_type)) {
        return nullptr;
    }
    detail::IntegerConversionService integer_conversion_service;
    if (const SemanticType *converted =
            integer_conversion_service.get_usual_arithmetic_conversion_type(
                lhs_type, get_bit_field_width_for_expr(context, lhs_expr),
                rhs_type, get_bit_field_width_for_expr(context, rhs_expr),
                const_cast<SemanticModel &>(*semantic_model));
        converted != nullptr) {
        return converted;
    }
    return conversion_checker.get_usual_arithmetic_conversion_type(
        lhs_type, rhs_type, const_cast<SemanticModel &>(*semantic_model));
}

const SemanticType *get_variadic_promotion_type(const CompilerContext &context,
                                                const Expr *expr,
                                                const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return nullptr;
    }

    if (is_builtin_type_named(type, "float")) {
        static BuiltinSemanticType double_type("double");
        return &double_type;
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto type_info = integer_conversion_service.get_integer_type_info(type);
    if (!type_info.has_value()) {
        return type;
    }

    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr) {
        if (type_info->get_rank() < 3) {
            static BuiltinSemanticType int_type("int");
            return &int_type;
        }
        return type;
    }

    if (const SemanticType *promotion_type =
            integer_conversion_service.get_integer_promotion_type(
                type, get_bit_field_width_for_expr(context, expr),
                const_cast<SemanticModel &>(*semantic_model));
        promotion_type != nullptr) {
        return promotion_type;
    }
    return type;
}

std::optional<IRValue> coerce_variadic_argument(IRBackend &backend,
                                                const CompilerContext &context,
                                                const Expr *expr,
                                                const IRValue &value) {
    const SemanticType *promotion_type =
        get_variadic_promotion_type(context, expr, value.type);
    if (promotion_type == nullptr) {
        return std::nullopt;
    }
    if (promotion_type == value.type) {
        return value;
    }
    return coerce_ir_value(backend, value, promotion_type);
}

std::optional<IRValue> coerce_binary_operand(IRBackend &backend,
                                             const IRValue &value,
                                             const SemanticType *target_type) {
    if (target_type == nullptr) {
        return std::nullopt;
    }
    return coerce_ir_value(backend, value, target_type);
}

std::optional<IRValue> build_compound_assignment_result(
    IRBackend &backend, const CompilerContext &context, EmissionState &state,
    const AssignExpr *assign_expr, const SemanticType *target_type,
    const IRValue &current_value, const IRValue &rhs_value) {
    if (assign_expr == nullptr || target_type == nullptr) {
        return std::nullopt;
    }

    const std::string binary_operator =
        get_compound_assignment_binary_operator(assign_expr->get_operator_text());
    if (binary_operator.empty()) {
        return std::nullopt;
    }

    const SemanticType *lhs_type =
        strip_qualifiers(get_node_type(context, assign_expr->get_target()));
    const SemanticType *rhs_type =
        strip_qualifiers(get_node_type(context, assign_expr->get_value()));
    if (lhs_type == nullptr || rhs_type == nullptr) {
        return std::nullopt;
    }

    if (binary_operator == "+" &&
        lhs_type->get_kind() == SemanticTypeKind::Pointer &&
        rhs_type->get_kind() == SemanticTypeKind::Builtin) {
        const SemanticType *pointee_type =
            static_cast<const PointerSemanticType *>(lhs_type)->get_pointee_type();
        const std::string address =
            backend.emit_element_address(current_value.text, pointee_type, rhs_value);
        if (address.empty()) {
            return std::nullopt;
        }
        return IRValue{address, target_type};
    }

    if (binary_operator == "-" &&
        lhs_type->get_kind() == SemanticTypeKind::Pointer &&
        rhs_type->get_kind() == SemanticTypeKind::Builtin) {
        static BuiltinSemanticType int_type("int");
        IRValue zero = backend.emit_integer_literal(0);
        IRValue negated_index =
            backend.emit_binary("-", zero, rhs_value, &int_type);
        if (negated_index.text.empty()) {
            return std::nullopt;
        }
        const SemanticType *pointee_type =
            static_cast<const PointerSemanticType *>(lhs_type)->get_pointee_type();
        const std::string address = backend.emit_element_address(
            current_value.text, pointee_type, negated_index);
        if (address.empty()) {
            return std::nullopt;
        }
        return IRValue{address, target_type};
    }

    const SemanticType *operation_type = target_type;
    if (binary_operator == "+" || binary_operator == "-" ||
        binary_operator == "*" || binary_operator == "/") {
        const SemanticType *arithmetic_type =
            get_usual_arithmetic_conversion_type(
                context, assign_expr->get_target(), lhs_type,
                assign_expr->get_value(), rhs_type);
        if (arithmetic_type != nullptr) {
            operation_type = arithmetic_type;
        }
    }

    std::optional<IRValue> coerced_lhs =
        coerce_binary_operand(backend, current_value, operation_type);
    std::optional<IRValue> coerced_rhs =
        coerce_binary_operand(backend, rhs_value, operation_type);
    if (!coerced_lhs.has_value() || !coerced_rhs.has_value()) {
        return std::nullopt;
    }

    IRValue result =
        backend.emit_binary(binary_operator, *coerced_lhs, *coerced_rhs,
                            operation_type);
    if (result.text.empty()) {
        return std::nullopt;
    }
    return coerce_ir_value(backend, result, target_type);
}

const SemanticType *get_function_return_type(const CompilerContext &context,
                                             const FunctionDecl *function_decl) {
    const SemanticType *function_type = get_node_type(context, function_decl);
    if (function_type != nullptr &&
        strip_qualifiers(function_type) != nullptr &&
        strip_qualifiers(function_type)->get_kind() == SemanticTypeKind::Function) {
        return static_cast<const FunctionSemanticType *>(
                   strip_qualifiers(function_type))
            ->get_return_type();
    }

    const TypeNode *return_type_node = function_decl->get_return_type();
    if (return_type_node != nullptr &&
        return_type_node->get_kind() == AstKind::BuiltinType) {
        const auto *builtin_type =
            static_cast<const BuiltinTypeNode *>(return_type_node);
        if (builtin_type->get_name() == "int") {
            static BuiltinSemanticType int_type("int");
            return &int_type;
        }
        if (builtin_type->get_name() == "void") {
            static BuiltinSemanticType void_type("void");
            return &void_type;
        }
        if (builtin_type->get_name() == "float") {
            static BuiltinSemanticType float_type("float");
            return &float_type;
        }
        if (builtin_type->get_name() == "double") {
            static BuiltinSemanticType double_type("double");
            return &double_type;
        }
        if (builtin_type->get_name() == "_Float16") {
            static BuiltinSemanticType float16_type("_Float16");
            return &float16_type;
        }
        if (builtin_type->get_name() == "long double") {
            static BuiltinSemanticType long_double_type("long double");
            return &long_double_type;
        }
    }

    return nullptr;
}

bool is_supported_type_for_storage(const CompilerContext &context,
                                   const AstNode *node) {
    return is_supported_storage_type(get_node_type(context, node));
}

bool is_system_header_path(const CompilerContext &context,
                           const std::string &file_path) {
    if (file_path.empty()) {
        return false;
    }

    const std::filesystem::path normalized_file_path =
        std::filesystem::path(file_path).lexically_normal();
    const std::string normalized_file_string = normalized_file_path.string();
    if (normalized_file_string.rfind("/usr/include/", 0) == 0 ||
        normalized_file_string == "/usr/include" ||
        normalized_file_string.rfind(
            "/Library/Developer/CommandLineTools/", 0) == 0 ||
        normalized_file_string.rfind(
            "/Applications/Xcode.app/Contents/Developer/", 0) == 0) {
        return true;
    }

    for (const std::string &system_directory :
         context.get_system_include_directories()) {
        const std::filesystem::path normalized_system_directory =
            std::filesystem::path(system_directory).lexically_normal();
        if (normalized_file_path == normalized_system_directory) {
            return true;
        }

        const std::string directory_with_separator =
            normalized_system_directory.string() +
            std::filesystem::path::preferred_separator;
        if (normalized_file_string.rfind(directory_with_separator, 0) == 0) {
            return true;
        }
    }

    return false;
}

bool is_system_header_decl(const CompilerContext &context, const Decl *decl) {
    if (decl == nullptr) {
        return false;
    }
    const SourceFile *source_file = decl->get_source_span().get_file();
    if (source_file == nullptr) {
        return false;
    }
    return is_system_header_path(context, source_file->get_path());
}

bool is_supported_expr(const CompilerContext &context, const Expr *expr);
std::string describe_unsupported_expr(const CompilerContext &context,
                                      const Expr *expr);
std::string describe_unsupported_decl(const CompilerContext &context,
                                      const Decl *decl);
std::string describe_unsupported_stmt(const CompilerContext &context,
                                      const Stmt *stmt);

bool is_supported_lvalue_expr(const Expr *expr) {
    if (expr == nullptr) {
        return false;
    }
    return expr->get_kind() == AstKind::IdentifierExpr ||
           expr->get_kind() == AstKind::MemberExpr ||
           expr->get_kind() == AstKind::IndexExpr ||
           expr->get_kind() == AstKind::UnaryExpr;
}

const SemanticType *get_array_decay_pointer_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Array) {
        return nullptr;
    }

    static std::unordered_map<const SemanticType *,
                              std::unique_ptr<PointerSemanticType>>
        decay_type_cache;

    auto it = decay_type_cache.find(type);
    if (it != decay_type_cache.end()) {
        return it->second.get();
    }

    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    auto owned_pointer_type =
        std::make_unique<PointerSemanticType>(array_type->get_element_type());
    const SemanticType *pointer_type = owned_pointer_type.get();
    decay_type_cache.emplace(type, std::move(owned_pointer_type));
    return pointer_type;
}

const SemanticType *get_member_owner_type(const SemanticType *type,
                                          const std::string &operator_text) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return nullptr;
    }
    if (operator_text == ".") {
        return type;
    }
    if (operator_text == "->" &&
        type->get_kind() == SemanticTypeKind::Pointer) {
        return strip_qualifiers(
            static_cast<const PointerSemanticType *>(type)->get_pointee_type());
    }
    return nullptr;
}

bool get_member_info(const SemanticType *owner_type,
                     const std::string &member_name, std::size_t &field_index,
                     const SemanticType *&field_type) {
    owner_type = strip_qualifiers(owner_type);
    if (owner_type == nullptr) {
        return false;
    }
    if (owner_type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type =
            static_cast<const StructSemanticType *>(owner_type);
        for (std::size_t index = 0; index < struct_type->get_fields().size();
             ++index) {
            if (struct_type->get_fields()[index].get_name() == member_name) {
                field_index = index;
                field_type = struct_type->get_fields()[index].get_type();
                return true;
            }
        }
    }
    if (owner_type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type =
            static_cast<const UnionSemanticType *>(owner_type);
        for (std::size_t index = 0; index < union_type->get_fields().size();
             ++index) {
            if (union_type->get_fields()[index].get_name() == member_name) {
                field_index = index;
                field_type = union_type->get_fields()[index].get_type();
                return true;
            }
        }
    }
    return false;
}

struct LValueAddress {
    std::string address;
    const SemanticType *type = nullptr;
    std::optional<detail::AggregateFieldLayout> bit_field_layout;
};

struct GlobalEmissionInfo {
    const SemanticSymbol *symbol = nullptr;
    const SemanticType *type = nullptr;
    bool is_internal_linkage = false;
    const VarDecl *extern_decl = nullptr;
    const VarDecl *tentative_definition = nullptr;
    const VarDecl *initialized_definition = nullptr;
};

struct UnionFirstNamedFieldInfo {
    std::size_t field_index = 0;
    const SemanticFieldInfo *field = nullptr;
};

std::optional<IRValue> build_expr(IRBackend &backend,
                                  const CompilerContext &context,
                                  EmissionState &state, const Expr *expr);
std::optional<IRValue> build_zero_ir_value(IRBackend &backend,
                                           const SemanticType *type);
std::optional<IRValue> build_all_ones_ir_value(IRBackend &backend,
                                               const SemanticType *type);
bool is_supported_initializer_for_type(const CompilerContext &context,
                                       const Expr *initializer,
                                       const SemanticType *target_type);
std::string describe_unsupported_initializer_for_type(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type);

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type);

std::optional<LValueAddress> build_lvalue_address(
    IRBackend &backend, const CompilerContext &context, EmissionState &state,
    const Expr *expr);

std::optional<std::string> build_global_initializer_text(
    const CompilerContext &context, const VarDecl *var_decl,
    const SemanticType *target_type);

std::optional<std::string> build_global_initializer_text_for_expr(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type);
std::optional<std::string> build_global_backing_initializer_text_for_expr(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type);
std::string build_padded_storage_initializer_text(
    const std::string &base_type_name, const std::string &initializer_text,
    std::size_t base_size, std::size_t total_size);
std::optional<UnionFirstNamedFieldInfo>
get_first_named_union_field(const SemanticType *target_type);
std::string get_symbol_ir_name(const SemanticSymbol *symbol);

std::optional<IRValue> load_lvalue_value(IRBackend &backend,
                                         const CompilerContext &context,
                                         const LValueAddress &lvalue);
std::optional<IRValue> decay_array_lvalue_to_pointer(IRBackend &backend,
                                                     const LValueAddress &lvalue);
bool emit_zero_initialize_lvalue(IRBackend &backend,
                                 const CompilerContext &context,
                                 EmissionState &state,
                                 const LValueAddress &lvalue);
bool emit_local_initializer_to_lvalue(IRBackend &backend,
                                      const CompilerContext &context,
                                      EmissionState &state,
                                      const LValueAddress &lvalue,
                                      const Expr *initializer);

std::optional<IRValue> store_lvalue_value(IRBackend &backend,
                                          const CompilerContext &context,
                                          const LValueAddress &lvalue,
                                          const IRValue &value);

std::uint64_t get_low_bit_mask(std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    if (bit_width >= 64) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << bit_width) - 1;
}

IRValue build_integer_constant(const SemanticType *type, std::uint64_t value) {
    return {std::to_string(value), type};
}

std::optional<IRValue> load_lvalue_value(IRBackend &backend,
                                         const CompilerContext &context,
                                         const LValueAddress &lvalue) {
    if (!lvalue.bit_field_layout.has_value() ||
        !lvalue.bit_field_layout->is_bit_field) {
        return backend.emit_load(lvalue.address, lvalue.type);
    }

    const auto &field_layout = *lvalue.bit_field_layout;
    if (field_layout.storage_type == nullptr || lvalue.type == nullptr ||
        field_layout.bit_width == 0) {
        return std::nullopt;
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto storage_info =
        integer_conversion_service.get_integer_type_info(field_layout.storage_type);
    if (!storage_info.has_value()) {
        return std::nullopt;
    }

    IRValue storage_value =
        backend.emit_load(lvalue.address, field_layout.storage_type);
    IRValue extracted_value = storage_value;
    if (field_layout.bit_offset > 0) {
        extracted_value = backend.emit_binary(
            ">>", extracted_value,
            build_integer_constant(field_layout.storage_type,
                                   field_layout.bit_offset),
            field_layout.storage_type);
        if (extracted_value.text.empty()) {
            return std::nullopt;
        }
    }

    const std::size_t storage_bits =
        static_cast<std::size_t>(storage_info->get_bit_width());
    const std::uint64_t field_mask = get_low_bit_mask(field_layout.bit_width);
    if (field_layout.bit_width < storage_bits) {
        extracted_value = backend.emit_binary(
            "&", extracted_value,
            build_integer_constant(field_layout.storage_type, field_mask),
            field_layout.storage_type);
        if (extracted_value.text.empty()) {
            return std::nullopt;
        }
    }

    if (storage_info->get_is_signed() && field_layout.bit_width < storage_bits) {
        const std::size_t sign_shift = storage_bits - field_layout.bit_width;
        extracted_value = backend.emit_binary(
            "<<", extracted_value,
            build_integer_constant(field_layout.storage_type, sign_shift),
            field_layout.storage_type);
        if (extracted_value.text.empty()) {
            return std::nullopt;
        }
        extracted_value = backend.emit_binary(
            ">>", extracted_value,
            build_integer_constant(field_layout.storage_type, sign_shift),
            field_layout.storage_type);
        if (extracted_value.text.empty()) {
            return std::nullopt;
        }
    }

    return coerce_ir_value(backend, extracted_value, lvalue.type);
}

std::optional<IRValue> decay_array_lvalue_to_pointer(IRBackend &backend,
                                                     const LValueAddress &lvalue) {
    const SemanticType *array_type = strip_qualifiers(lvalue.type);
    if (array_type == nullptr || array_type->get_kind() != SemanticTypeKind::Array) {
        return std::nullopt;
    }

    const auto *pointer_type = get_array_decay_pointer_type(array_type);
    if (pointer_type == nullptr) {
        return std::nullopt;
    }

    const auto *semantic_array_type = static_cast<const ArraySemanticType *>(array_type);
    IRValue zero = backend.emit_integer_literal(0);
    const std::string element_address = backend.emit_element_address(
        lvalue.address, semantic_array_type->get_element_type(), zero);
    if (element_address.empty()) {
        return std::nullopt;
    }

    return IRValue{element_address, pointer_type};
}

std::optional<IRValue> store_lvalue_value(IRBackend &backend,
                                          const CompilerContext &context,
                                          const LValueAddress &lvalue,
                                          const IRValue &value) {
    if (!lvalue.bit_field_layout.has_value() ||
        !lvalue.bit_field_layout->is_bit_field) {
        std::optional<IRValue> coerced_value;
        if (value.type != nullptr && lvalue.type != nullptr &&
            detail::get_llvm_type_name(value.type) ==
                detail::get_llvm_type_name(lvalue.type)) {
            coerced_value = value;
        } else {
            coerced_value = coerce_ir_value(backend, value, lvalue.type);
        }
        if (!coerced_value.has_value()) {
            return std::nullopt;
        }
        backend.emit_store(lvalue.address, *coerced_value);
        return coerced_value;
    }

    const auto &field_layout = *lvalue.bit_field_layout;
    if (field_layout.storage_type == nullptr || lvalue.type == nullptr ||
        field_layout.bit_width == 0) {
        return std::nullopt;
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto storage_info =
        integer_conversion_service.get_integer_type_info(field_layout.storage_type);
    if (!storage_info.has_value()) {
        return std::nullopt;
    }

    std::optional<IRValue> coerced_field_value =
        coerce_ir_value(backend, value, lvalue.type);
    if (!coerced_field_value.has_value()) {
        return std::nullopt;
    }

    std::optional<IRValue> storage_value =
        coerce_ir_value(backend, *coerced_field_value, field_layout.storage_type);
    if (!storage_value.has_value()) {
        return std::nullopt;
    }

    IRValue packed_field_value = *storage_value;
    const std::size_t storage_bits =
        static_cast<std::size_t>(storage_info->get_bit_width());
    const std::uint64_t field_mask = get_low_bit_mask(field_layout.bit_width);
    if (field_layout.bit_width < storage_bits) {
        packed_field_value = backend.emit_binary(
            "&", packed_field_value,
            build_integer_constant(field_layout.storage_type, field_mask),
            field_layout.storage_type);
        if (packed_field_value.text.empty()) {
            return std::nullopt;
        }
    }
    if (field_layout.bit_offset > 0) {
        packed_field_value = backend.emit_binary(
            "<<", packed_field_value,
            build_integer_constant(field_layout.storage_type,
                                   field_layout.bit_offset),
            field_layout.storage_type);
        if (packed_field_value.text.empty()) {
            return std::nullopt;
        }
    }

    IRValue current_storage =
        backend.emit_load(lvalue.address, field_layout.storage_type);
    const std::uint64_t shifted_field_mask =
        field_mask << field_layout.bit_offset;
    IRValue preserved_bits = backend.emit_binary(
        "&", current_storage,
        build_integer_constant(field_layout.storage_type, ~shifted_field_mask),
        field_layout.storage_type);
    if (preserved_bits.text.empty()) {
        return std::nullopt;
    }

    IRValue merged_storage = backend.emit_binary(
        "|", preserved_bits, packed_field_value, field_layout.storage_type);
    if (merged_storage.text.empty()) {
        return std::nullopt;
    }

    backend.emit_store(lvalue.address, merged_storage);
    return load_lvalue_value(backend, context, lvalue);
}

std::optional<IRValue> build_increment_result(IRBackend &backend,
                                              const SemanticType *operand_type,
                                              const IRValue &current_value,
                                              bool is_increment) {
    operand_type = strip_qualifiers(operand_type);
    if (operand_type == nullptr) {
        return std::nullopt;
    }

    if (operand_type->get_kind() == SemanticTypeKind::Pointer) {
        const auto *pointer_type =
            static_cast<const PointerSemanticType *>(operand_type);
        IRValue step = backend.emit_integer_literal(is_increment ? 1 : -1);
        const std::string updated_address = backend.emit_element_address(
            current_value.text, pointer_type->get_pointee_type(), step);
        if (updated_address.empty()) {
            return std::nullopt;
        }
        return IRValue{updated_address, operand_type};
    }

    if (!is_supported_scalar_storage_type(operand_type)) {
        return std::nullopt;
    }

    std::optional<IRValue> one =
        coerce_ir_value(backend, backend.emit_integer_literal(1), operand_type);
    if (!one.has_value()) {
        return std::nullopt;
    }

    IRValue updated_value =
        backend.emit_binary(is_increment ? "+" : "-", current_value, *one,
                            operand_type);
    if (updated_value.text.empty()) {
        return std::nullopt;
    }
    return updated_value;
}

std::optional<IRValue> emit_increment_expr(IRBackend &backend,
                                           const CompilerContext &context,
                                           EmissionState &state,
                                           const Expr *operand,
                                           bool is_increment,
                                           bool returns_updated_value) {
    std::optional<LValueAddress> target_address =
        build_lvalue_address(backend, context, state, operand);
    if (!target_address.has_value() || target_address->type == nullptr) {
        return std::nullopt;
    }

    std::optional<IRValue> current_value =
        load_lvalue_value(backend, context, *target_address);
    if (!current_value.has_value()) {
        return std::nullopt;
    }

    std::optional<IRValue> updated_value = build_increment_result(
        backend, target_address->type, *current_value, is_increment);
    if (!updated_value.has_value()) {
        return std::nullopt;
    }

    std::optional<IRValue> stored_value =
        store_lvalue_value(backend, context, *target_address, *updated_value);
    if (!stored_value.has_value()) {
        return std::nullopt;
    }

    return returns_updated_value ? stored_value : current_value;
}

std::string format_floating_initializer(long double value) {
    std::ostringstream stream;
    stream << std::scientific << std::setprecision(16)
           << static_cast<double>(value);
    return stream.str();
}

std::optional<long double>
parse_floating_initializer_text(const std::string &value_text) {
    std::string sanitized = value_text;
    while (!sanitized.empty() &&
           (sanitized.back() == 'f' || sanitized.back() == 'F' ||
            sanitized.back() == 'l' || sanitized.back() == 'L')) {
        sanitized.pop_back();
    }
    if (sanitized.empty()) {
        return std::nullopt;
    }
    try {
        return std::stold(sanitized);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> build_global_initializer_text(
    const CompilerContext &context, const VarDecl *var_decl,
    const SemanticType *target_type) {
    if (var_decl == nullptr || target_type == nullptr) {
        return std::nullopt;
    }

    return build_global_initializer_text_for_expr(
        context, var_decl->get_initializer(), target_type);
}

std::optional<std::string> build_global_struct_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    const auto *struct_type =
        static_cast<const StructSemanticType *>(strip_qualifiers(target_type));
    if (struct_type == nullptr) {
        return std::nullopt;
    }

    const detail::AggregateLayoutInfo layout =
        detail::compute_aggregate_layout(target_type);
    std::vector<std::string> element_initializers(layout.elements.size());
    std::vector<bool> element_initialized(layout.elements.size(), false);
    std::vector<std::uint64_t> bit_field_storage_values(layout.elements.size(), 0);
    std::vector<bool> bit_field_storage_initialized(layout.elements.size(), false);

    std::size_t initializer_index = 0;
    for (std::size_t field_index = 0; field_index < struct_type->get_fields().size();
         ++field_index) {
        const auto &field = struct_type->get_fields()[field_index];
        const auto &field_layout = layout.field_layouts[field_index];
        if (!field_layout.has_value()) {
            continue;
        }

        const Expr *field_initializer = nullptr;
        if (!field.get_name().empty()) {
            if (initializer_index < initializer->get_elements().size()) {
                field_initializer =
                    initializer->get_elements()[initializer_index].get();
            }
            ++initializer_index;
        }

        if (field.get_is_bit_field()) {
            const auto integer_constant = field_initializer == nullptr
                                              ? std::optional<long long>(0)
                                              : get_integer_constant_value(
                                                    context, field_initializer);
            if (!integer_constant.has_value()) {
                return std::nullopt;
            }

            const std::uint64_t field_bits =
                static_cast<std::uint64_t>(*integer_constant) &
                get_low_bit_mask(field_layout->bit_width);
            bit_field_storage_values[field_layout->llvm_element_index] |=
                field_bits << field_layout->bit_offset;
            bit_field_storage_initialized[field_layout->llvm_element_index] = true;
            continue;
        }

        if (field_layout->llvm_element_index >= element_initializers.size()) {
            return std::nullopt;
        }
        std::optional<std::string> lowered_initializer =
            build_global_initializer_text_for_expr(context, field_initializer,
                                                   field.get_type());
        if (!lowered_initializer.has_value()) {
            return std::nullopt;
        }
        element_initializers[field_layout->llvm_element_index] =
            *lowered_initializer;
        element_initialized[field_layout->llvm_element_index] = true;
    }

    if (initializer_index < initializer->get_elements().size()) {
        return std::nullopt;
    }

    std::string result = "{ ";
    for (std::size_t index = 0; index < layout.elements.size(); ++index) {
        if (index > 0) {
            result += ", ";
        }

        const auto &element = layout.elements[index];
        if (element.kind == detail::AggregateLayoutElementKind::Padding) {
            result += "[" + std::to_string(element.padding_size) + " x i8]";
            result += " zeroinitializer";
            continue;
        }

        const SemanticType *element_type = element.type;
        result += detail::get_llvm_type_name(element_type);
        result += " ";
        if (bit_field_storage_initialized[index]) {
            result += std::to_string(bit_field_storage_values[index]);
            continue;
        }
        if (element_initialized[index]) {
            result += element_initializers[index];
            continue;
        }
        result += "zeroinitializer";
    }
    result += " }";
    return result;
}

bool is_null_pointer_constant_initializer(const CompilerContext &context,
                                          const Expr *initializer) {
    if (initializer == nullptr) {
        return false;
    }

    const auto integer_constant = get_integer_constant_value(context, initializer);
    if (integer_constant.has_value() && *integer_constant == 0) {
        return true;
    }

    if (initializer->get_kind() != AstKind::CastExpr) {
        return false;
    }

    const auto *cast_expr = static_cast<const CastExpr *>(initializer);
    return is_null_pointer_constant_initializer(context, cast_expr->get_operand());
}

std::string get_integer_storage_llvm_type_name(std::size_t bit_width) {
    return "i" + std::to_string(bit_width);
}

std::optional<std::string> build_global_union_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr ||
        target_type->get_kind() != SemanticTypeKind::Union ||
        initializer == nullptr) {
        return std::nullopt;
    }
    const auto *union_type = static_cast<const UnionSemanticType *>(target_type);

    if (initializer->get_elements().empty()) {
        return std::string("zeroinitializer");
    }
    if (initializer->get_elements().size() > 1) {
        return std::nullopt;
    }

    const auto first_named_field = get_first_named_union_field(target_type);
    if (!first_named_field.has_value() || first_named_field->field == nullptr) {
        return std::nullopt;
    }

    const SemanticFieldInfo &field = *first_named_field->field;
    const SemanticType *field_type = field.get_type();
    std::string field_initializer;
    if (field.get_is_bit_field()) {
        const auto integer_constant =
            get_integer_constant_value(context, initializer->get_elements().front().get());
        if (!integer_constant.has_value()) {
            return std::nullopt;
        }

        const std::size_t bit_width =
            static_cast<std::size_t>(field.get_bit_width().value_or(0));
        const std::uint64_t masked_value =
            static_cast<std::uint64_t>(*integer_constant) &
            get_low_bit_mask(bit_width);
        field_initializer = std::to_string(masked_value);
    } else {
        const std::optional<std::string> lowered_initializer =
            build_global_initializer_text_for_expr(
                context, initializer->get_elements().front().get(), field_type);
        if (!lowered_initializer.has_value()) {
            return std::nullopt;
        }
        field_initializer = *lowered_initializer;
    }

    return build_padded_storage_initializer_text(
        detail::get_llvm_type_name(field_type), field_initializer,
        detail::get_type_size(field_type), detail::get_type_size(target_type));
}

std::optional<UnionFirstNamedFieldInfo>
get_first_named_union_field(const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr ||
        target_type->get_kind() != SemanticTypeKind::Union) {
        return std::nullopt;
    }
    const auto *union_type = static_cast<const UnionSemanticType *>(target_type);

    for (std::size_t index = 0; index < union_type->get_fields().size(); ++index) {
        const auto &field = union_type->get_fields()[index];
        if (!field.get_name().empty()) {
            return UnionFirstNamedFieldInfo{index, &field};
        }
    }

    return std::nullopt;
}

std::string get_padded_storage_llvm_type_name(
    const std::string &base_type_name, std::size_t base_size,
    std::size_t total_size) {
    if (base_size >= total_size) {
        return base_type_name;
    }
    return "{ " + base_type_name + ", [" +
           std::to_string(total_size - base_size) + " x i8] }";
}

std::string build_global_backing_storage_llvm_type_name(
    const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr) {
        return "void";
    }

    if (target_type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(target_type);
        const auto &dimensions = array_type->get_dimensions();
        std::string element_type_name = build_global_backing_storage_llvm_type_name(
            array_type->get_element_type());
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            element_type_name = "[" + std::to_string(*it) + " x " +
                                element_type_name + "]";
        }
        return element_type_name;
    }

    if (target_type->get_kind() == SemanticTypeKind::Struct) {
        const detail::AggregateLayoutInfo layout =
            detail::compute_aggregate_layout(target_type);
        std::string result = "{ ";
        for (std::size_t index = 0; index < layout.elements.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            const auto &element = layout.elements[index];
            if (element.kind == detail::AggregateLayoutElementKind::Padding) {
                result += "[" + std::to_string(element.padding_size) + " x i8]";
                continue;
            }
            result += build_global_backing_storage_llvm_type_name(element.type);
        }
        result += " }";
        return result;
    }

    if (target_type->get_kind() == SemanticTypeKind::Union) {
        const auto first_named_field = get_first_named_union_field(target_type);
        if (!first_named_field.has_value() || first_named_field->field == nullptr) {
            return detail::get_llvm_type_name(target_type);
        }
        const SemanticType *field_type = first_named_field->field->get_type();
        return get_padded_storage_llvm_type_name(
            build_global_backing_storage_llvm_type_name(field_type),
            detail::get_type_size(field_type),
            detail::get_type_size(target_type));
    }

    return detail::get_llvm_type_name(target_type);
}

std::string build_global_backing_storage_name(const std::string &symbol_name) {
    return "__sysycc.storage." + symbol_name;
}

std::string build_padded_storage_initializer_text(
    const std::string &base_type_name, const std::string &initializer_text,
    std::size_t base_size, std::size_t total_size) {
    if (base_size >= total_size) {
        return initializer_text;
    }

    return "{ " + base_type_name + " " + initializer_text + ", [" +
           std::to_string(total_size - base_size) +
           " x i8] zeroinitializer }";
}

bool uses_global_storage_alias(const CompilerContext &context,
                               const VarDecl *var_decl,
                               const SemanticType *type) {
    if (var_decl == nullptr || type == nullptr || var_decl->get_initializer() == nullptr) {
        return false;
    }

    if (build_global_initializer_text(context, var_decl, type).has_value()) {
        return false;
    }

    if (!build_global_backing_initializer_text_for_expr(
             context, var_decl->get_initializer(), type)
             .has_value()) {
        return false;
    }

    return build_global_backing_storage_llvm_type_name(type) !=
           detail::get_llvm_type_name(type);
}

std::optional<std::string> build_global_array_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    const auto *array_type =
        static_cast<const ArraySemanticType *>(strip_qualifiers(target_type));
    if (array_type == nullptr || array_type->get_dimensions().empty()) {
        return std::nullopt;
    }

    const int element_count = array_type->get_dimensions().front();
    if (element_count < 0) {
        return std::nullopt;
    }

    const SemanticType *element_type = array_type->get_element_type();
    std::vector<std::string> element_initializers(
        static_cast<std::size_t>(element_count), "zeroinitializer");

    const auto &elements = initializer->get_elements();
    if (elements.size() > static_cast<std::size_t>(element_count)) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < elements.size(); ++index) {
        std::optional<std::string> lowered_initializer =
            build_global_initializer_text_for_expr(context, elements[index].get(),
                                                   element_type);
        if (!lowered_initializer.has_value()) {
            return std::nullopt;
        }
        element_initializers[index] = *lowered_initializer;
    }

    std::string result = "[";
    for (int index = 0; index < element_count; ++index) {
        if (index > 0) {
            result += ", ";
        }
        result += detail::get_llvm_type_name(element_type);
        result += " ";
        result += element_initializers[static_cast<std::size_t>(index)];
    }
    result += "]";
    return result;
}

std::optional<std::string> build_global_backing_struct_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    const auto *struct_type =
        static_cast<const StructSemanticType *>(strip_qualifiers(target_type));
    if (struct_type == nullptr) {
        return std::nullopt;
    }

    const detail::AggregateLayoutInfo layout =
        detail::compute_aggregate_layout(target_type);
    std::vector<std::string> element_initializers(layout.elements.size());
    std::vector<bool> element_initialized(layout.elements.size(), false);
    std::vector<std::uint64_t> bit_field_storage_values(layout.elements.size(), 0);
    std::vector<bool> bit_field_storage_initialized(layout.elements.size(), false);

    std::size_t initializer_index = 0;
    for (std::size_t field_index = 0; field_index < struct_type->get_fields().size();
         ++field_index) {
        const auto &field = struct_type->get_fields()[field_index];
        const auto &field_layout = layout.field_layouts[field_index];
        if (!field_layout.has_value()) {
            continue;
        }

        const Expr *field_initializer = nullptr;
        if (!field.get_name().empty()) {
            if (initializer_index < initializer->get_elements().size()) {
                field_initializer =
                    initializer->get_elements()[initializer_index].get();
            }
            ++initializer_index;
        }

        if (field.get_is_bit_field()) {
            const auto integer_constant = field_initializer == nullptr
                                              ? std::optional<long long>(0)
                                              : get_integer_constant_value(
                                                    context, field_initializer);
            if (!integer_constant.has_value()) {
                return std::nullopt;
            }

            const std::uint64_t field_bits =
                static_cast<std::uint64_t>(*integer_constant) &
                get_low_bit_mask(field_layout->bit_width);
            bit_field_storage_values[field_layout->llvm_element_index] |=
                field_bits << field_layout->bit_offset;
            bit_field_storage_initialized[field_layout->llvm_element_index] = true;
            continue;
        }

        if (field_layout->llvm_element_index >= element_initializers.size()) {
            return std::nullopt;
        }

        const std::optional<std::string> lowered_initializer =
            build_global_backing_initializer_text_for_expr(context,
                                                           field_initializer,
                                                           field.get_type());
        if (!lowered_initializer.has_value()) {
            return std::nullopt;
        }

        element_initializers[field_layout->llvm_element_index] =
            *lowered_initializer;
        element_initialized[field_layout->llvm_element_index] = true;
    }

    if (initializer_index < initializer->get_elements().size()) {
        return std::nullopt;
    }

    std::string result = "{ ";
    for (std::size_t index = 0; index < layout.elements.size(); ++index) {
        if (index > 0) {
            result += ", ";
        }

        const auto &element = layout.elements[index];
        if (element.kind == detail::AggregateLayoutElementKind::Padding) {
            result += "[" + std::to_string(element.padding_size) + " x i8]";
            result += " zeroinitializer";
            continue;
        }

        result += build_global_backing_storage_llvm_type_name(element.type);
        result += " ";
        if (bit_field_storage_initialized[index]) {
            result += std::to_string(bit_field_storage_values[index]);
            continue;
        }
        if (element_initialized[index]) {
            result += element_initializers[index];
            continue;
        }
        result += "zeroinitializer";
    }
    result += " }";
    return result;
}

std::optional<std::string> build_global_backing_union_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr ||
        target_type->get_kind() != SemanticTypeKind::Union ||
        initializer == nullptr) {
        return std::nullopt;
    }

    if (initializer->get_elements().empty()) {
        return std::string("zeroinitializer");
    }
    if (initializer->get_elements().size() > 1) {
        return std::nullopt;
    }

    const auto first_named_field = get_first_named_union_field(target_type);
    if (!first_named_field.has_value() || first_named_field->field == nullptr) {
        return std::nullopt;
    }

    const SemanticType *field_type = first_named_field->field->get_type();
    const std::optional<std::string> field_initializer =
        build_global_backing_initializer_text_for_expr(
            context, initializer->get_elements().front().get(), field_type);
    if (!field_initializer.has_value()) {
        return std::nullopt;
    }

    return build_padded_storage_initializer_text(
        build_global_backing_storage_llvm_type_name(field_type),
        *field_initializer, detail::get_type_size(field_type),
        detail::get_type_size(target_type));
}

std::optional<std::string> build_global_backing_array_initializer_text(
    const CompilerContext &context, const InitListExpr *initializer,
    const SemanticType *target_type) {
    const auto *array_type =
        static_cast<const ArraySemanticType *>(strip_qualifiers(target_type));
    if (array_type == nullptr || array_type->get_dimensions().empty()) {
        return std::nullopt;
    }

    const int element_count = array_type->get_dimensions().front();
    if (element_count < 0) {
        return std::nullopt;
    }

    const SemanticType *element_type = array_type->get_element_type();
    std::vector<std::string> element_initializers(
        static_cast<std::size_t>(element_count), "zeroinitializer");

    const auto &elements = initializer->get_elements();
    if (elements.size() > static_cast<std::size_t>(element_count)) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < elements.size(); ++index) {
        const std::optional<std::string> lowered_initializer =
            build_global_backing_initializer_text_for_expr(
                context, elements[index].get(), element_type);
        if (!lowered_initializer.has_value()) {
            return std::nullopt;
        }
        element_initializers[index] = *lowered_initializer;
    }

    std::string result = "[";
    for (int index = 0; index < element_count; ++index) {
        if (index > 0) {
            result += ", ";
        }
        result += build_global_backing_storage_llvm_type_name(element_type);
        result += " ";
        result += element_initializers[static_cast<std::size_t>(index)];
    }
    result += "]";
    return result;
}

std::optional<std::string> build_global_backing_initializer_text_for_expr(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type) {
    if (target_type == nullptr) {
        return std::nullopt;
    }

    if (initializer == nullptr) {
        return std::string("zeroinitializer");
    }

    const SemanticType *unqualified_target = strip_qualifiers(target_type);
    if (unqualified_target == nullptr) {
        return std::nullopt;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Array) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_backing_array_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Struct) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_backing_struct_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Union) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_backing_union_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    return build_global_initializer_text_for_expr(context, initializer, target_type);
}

std::optional<std::string> build_global_lvalue_address_initializer_text(
    const CompilerContext &context, const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return std::nullopt;
        }
        if (symbol->get_kind() == SymbolKind::Function) {
            return "@" + get_symbol_ir_name(symbol);
        }

        const VariableSemanticInfo *variable_info =
            get_variable_info(context, symbol);
        if (variable_info == nullptr || !variable_info->get_is_global_storage()) {
            return std::nullopt;
        }
        return "@" + get_symbol_ir_name(symbol);
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        std::optional<std::string> base_address =
            build_global_lvalue_address_initializer_text(context,
                                                         index_expr->get_base());
        if (!base_address.has_value()) {
            return std::nullopt;
        }

        const auto constant_index =
            get_integer_constant_value(context, index_expr->get_index());
        const SemanticType *element_type = get_node_type(context, expr);
        if (!constant_index.has_value() || element_type == nullptr) {
            return std::nullopt;
        }

        return "getelementptr inbounds (" +
               detail::get_llvm_type_name(element_type) + ", ptr " +
               *base_address + ", i32 " + std::to_string(*constant_index) + ")";
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        if (member_expr->get_operator_text() != ".") {
            return std::nullopt;
        }

        const SemanticType *owner_type =
            get_member_owner_type(get_node_type(context, member_expr->get_base()),
                                  member_expr->get_operator_text());
        std::size_t field_index = 0;
        const SemanticType *field_type = nullptr;
        if (!get_member_info(owner_type, member_expr->get_member_name(),
                             field_index, field_type)) {
            return std::nullopt;
        }

        const auto field_layout =
            detail::get_aggregate_field_layout(owner_type, field_index);
        if (!field_layout.has_value() || field_layout->is_bit_field) {
            return std::nullopt;
        }

        std::optional<std::string> base_address =
            build_global_lvalue_address_initializer_text(context,
                                                         member_expr->get_base());
        if (!base_address.has_value()) {
            return std::nullopt;
        }

        const SemanticType *unqualified_owner_type = strip_qualifiers(owner_type);
        if (unqualified_owner_type != nullptr &&
            unqualified_owner_type->get_kind() == SemanticTypeKind::Union) {
            return *base_address;
        }

        return "getelementptr inbounds (" +
               detail::get_llvm_type_name(owner_type) + ", ptr " + *base_address +
               ", i32 0, i32 " +
               std::to_string(field_layout->llvm_element_index) + ")";
    }
    default:
        return std::nullopt;
    }
}

std::optional<std::string> build_global_address_initializer_text(
    const CompilerContext &context, const Expr *initializer) {
    if (initializer == nullptr || initializer->get_kind() != AstKind::UnaryExpr) {
        return std::nullopt;
    }

    const auto *unary_expr = static_cast<const UnaryExpr *>(initializer);
    if (unary_expr->get_operator_text() != "&") {
        return std::nullopt;
    }
    return build_global_lvalue_address_initializer_text(context,
                                                        unary_expr->get_operand());
}

std::optional<std::string> build_global_initializer_text_for_expr(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type) {
    if (target_type == nullptr) {
        return std::nullopt;
    }

    if (initializer == nullptr) {
        return std::string("zeroinitializer");
    }

    const SemanticType *unqualified_target = strip_qualifiers(target_type);
    if (unqualified_target == nullptr) {
        return std::nullopt;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Pointer) {
        if (is_null_pointer_constant_initializer(context, initializer)) {
            return std::string("null");
        }
        std::optional<std::string> address_initializer =
            build_global_address_initializer_text(context, initializer);
        if (address_initializer.has_value()) {
            return address_initializer;
        }
        return std::nullopt;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Array) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_array_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Struct) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_struct_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Union) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return std::nullopt;
        }
        return build_global_union_initializer_text(
            context, static_cast<const InitListExpr *>(initializer), target_type);
    }

    if (unqualified_target->get_kind() != SemanticTypeKind::Builtin) {
        return std::nullopt;
    }

    const auto &builtin_name =
        static_cast<const BuiltinSemanticType *>(unqualified_target)->get_name();
    const auto integer_constant = get_integer_constant_value(context, initializer);
    if (integer_constant.has_value()) {
        return std::to_string(*integer_constant);
    }

    if (initializer->get_kind() == AstKind::FloatLiteralExpr &&
        (builtin_name == "float" || builtin_name == "double" ||
         builtin_name == "_Float16" || builtin_name == "long double")) {
        const auto *float_literal = static_cast<const FloatLiteralExpr *>(initializer);
        const auto parsed_value =
            parse_floating_initializer_text(float_literal->get_value_text());
        if (!parsed_value.has_value()) {
            return std::nullopt;
        }
        return format_floating_initializer(*parsed_value);
    }

    return std::nullopt;
}

bool is_supported_initializer_for_type(const CompilerContext &context,
                                       const Expr *initializer,
                                       const SemanticType *target_type) {
    if (initializer == nullptr || target_type == nullptr) {
        return initializer == nullptr;
    }

    const SemanticType *unqualified_target = strip_qualifiers(target_type);
    if (unqualified_target == nullptr) {
        return false;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Array) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *array_type = static_cast<const ArraySemanticType *>(unqualified_target);
        if (array_type->get_dimensions().empty()) {
            return false;
        }
        const int element_count = array_type->get_dimensions().front();
        if (element_count < 0) {
            return false;
        }
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (init_list->get_elements().size() >
            static_cast<std::size_t>(element_count)) {
            return false;
        }
        for (const auto &element : init_list->get_elements()) {
            if (!is_supported_initializer_for_type(context, element.get(),
                                                   array_type->get_element_type())) {
                return false;
            }
        }
        return true;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Struct) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *struct_type =
            static_cast<const StructSemanticType *>(unqualified_target);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        std::size_t initializer_index = 0;
        for (const auto &field : struct_type->get_fields()) {
            if (field.get_name().empty()) {
                continue;
            }
            if (initializer_index >= init_list->get_elements().size()) {
                break;
            }
            if (!is_supported_initializer_for_type(
                    context, init_list->get_elements()[initializer_index].get(),
                    field.get_type())) {
                return false;
            }
            ++initializer_index;
        }
        return initializer_index == init_list->get_elements().size();
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Union) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *union_type =
            static_cast<const UnionSemanticType *>(unqualified_target);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (init_list->get_elements().size() > 1) {
            return false;
        }
        if (init_list->get_elements().empty()) {
            return true;
        }
        const auto named_field_it =
            std::find_if(union_type->get_fields().begin(),
                         union_type->get_fields().end(),
                         [](const SemanticFieldInfo &field) {
                             return !field.get_name().empty();
                         });
        return named_field_it != union_type->get_fields().end() &&
               is_supported_initializer_for_type(
                   context, init_list->get_elements().front().get(),
                   named_field_it->get_type());
    }

    return is_supported_expr(context, initializer);
}

std::string describe_unsupported_initializer_for_type(
    const CompilerContext &context, const Expr *initializer,
    const SemanticType *target_type) {
    if (initializer == nullptr) {
        return {};
    }
    if (target_type == nullptr) {
        return "initializer target type is missing";
    }

    const SemanticType *unqualified_target = strip_qualifiers(target_type);
    if (unqualified_target == nullptr) {
        return "initializer target type is unsupported";
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Array) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return "array initializer must be an initializer list";
        }
        const auto *array_type = static_cast<const ArraySemanticType *>(unqualified_target);
        if (array_type->get_dimensions().empty()) {
            return "array type is incomplete";
        }
        const int element_count = array_type->get_dimensions().front();
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (element_count >= 0 &&
            init_list->get_elements().size() > static_cast<std::size_t>(element_count)) {
            return "array initializer has too many elements";
        }
        for (std::size_t index = 0; index < init_list->get_elements().size();
             ++index) {
            const std::string element_reason =
                describe_unsupported_initializer_for_type(
                    context, init_list->get_elements()[index].get(),
                    array_type->get_element_type());
            if (!element_reason.empty()) {
                return "array initializer element " + std::to_string(index) +
                       ": " + element_reason;
            }
        }
        return {};
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Struct) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return "struct initializer must be an initializer list";
        }
        const auto *struct_type =
            static_cast<const StructSemanticType *>(unqualified_target);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        std::size_t initializer_index = 0;
        for (const auto &field : struct_type->get_fields()) {
            if (field.get_name().empty()) {
                continue;
            }
            if (initializer_index >= init_list->get_elements().size()) {
                return {};
            }
            const std::string field_reason =
                describe_unsupported_initializer_for_type(
                    context, init_list->get_elements()[initializer_index].get(),
                    field.get_type());
            if (!field_reason.empty()) {
                return "field '" + field.get_name() + "': " + field_reason;
            }
            ++initializer_index;
        }
        return initializer_index == init_list->get_elements().size()
                   ? std::string()
                   : "struct initializer has too many elements";
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Union) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return "union initializer must be an initializer list";
        }
        const auto *union_type =
            static_cast<const UnionSemanticType *>(unqualified_target);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (init_list->get_elements().size() > 1) {
            return "union initializer has too many elements";
        }
        if (init_list->get_elements().empty()) {
            return {};
        }
        const auto named_field_it =
            std::find_if(union_type->get_fields().begin(),
                         union_type->get_fields().end(),
                         [](const SemanticFieldInfo &field) {
                             return !field.get_name().empty();
                         });
        if (named_field_it == union_type->get_fields().end()) {
            return "union initializer has no named field to initialize";
        }
        const std::string field_reason =
            describe_unsupported_initializer_for_type(
                context, init_list->get_elements().front().get(),
                named_field_it->get_type());
        return field_reason.empty()
                   ? std::string()
                   : "union first field '" + named_field_it->get_name() +
                         "': " + field_reason;
    }

    const std::string expr_reason =
        describe_unsupported_expr(context, initializer);
    if (expr_reason.empty()) {
        return {};
    }
    return "target " + detail::get_llvm_type_name(target_type) + ": " +
           expr_reason;
}

bool is_supported_decl(const CompilerContext &context, const Decl *decl) {
    if (decl == nullptr) {
        return false;
    }

    if (decl->get_kind() == AstKind::VarDecl) {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        if (!is_supported_type_for_storage(context, var_decl)) {
            return false;
        }
        const Expr *initializer = var_decl->get_initializer();
        return is_supported_initializer_for_type(
            context, initializer, get_node_type(context, var_decl));
    }

    if (decl->get_kind() == AstKind::ParamDecl) {
        const auto *param_decl = static_cast<const ParamDecl *>(decl);
        return is_supported_type_for_storage(context, param_decl);
    }

    if (decl->get_kind() == AstKind::StructDecl ||
        decl->get_kind() == AstKind::UnionDecl ||
        decl->get_kind() == AstKind::EnumDecl) {
        return true;
    }

    return false;
}

bool is_supported_stmt(const CompilerContext &context, const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            if (!is_supported_stmt(context, child.get())) {
                return false;
            }
        }
        return true;
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (!is_supported_decl(context, decl.get())) {
                return false;
            }
        }
        return true;
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        return expr_stmt->get_expression() != nullptr &&
               is_supported_expr(context, expr_stmt->get_expression());
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        return if_stmt->get_condition() != nullptr &&
               is_supported_expr(context, if_stmt->get_condition()) &&
               is_supported_stmt(context, if_stmt->get_then_branch()) &&
               (if_stmt->get_else_branch() == nullptr ||
                is_supported_stmt(context, if_stmt->get_else_branch()));
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        return while_stmt->get_condition() != nullptr &&
               is_supported_expr(context, while_stmt->get_condition()) &&
               is_supported_stmt(context, while_stmt->get_body());
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        return do_while_stmt->get_condition() != nullptr &&
               is_supported_expr(context, do_while_stmt->get_condition()) &&
               is_supported_stmt(context, do_while_stmt->get_body());
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        return (for_stmt->get_init() == nullptr ||
                is_supported_expr(context, for_stmt->get_init())) &&
               (for_stmt->get_condition() == nullptr ||
                is_supported_expr(context, for_stmt->get_condition())) &&
               (for_stmt->get_step() == nullptr ||
                is_supported_expr(context, for_stmt->get_step())) &&
               is_supported_stmt(context, for_stmt->get_body());
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        if (switch_stmt->get_condition() == nullptr ||
            !is_supported_expr(context, switch_stmt->get_condition()) ||
            switch_stmt->get_body() == nullptr ||
            switch_stmt->get_body()->get_kind() != AstKind::BlockStmt) {
            return false;
        }

        const auto *body = static_cast<const BlockStmt *>(switch_stmt->get_body());
        for (const auto &child : body->get_statements()) {
            if (child == nullptr) {
                return false;
            }
            if (child->get_kind() == AstKind::CaseStmt) {
                const auto *case_stmt = static_cast<const CaseStmt *>(child.get());
                if (case_stmt->get_value() == nullptr ||
                    !is_supported_expr(context, case_stmt->get_value()) ||
                    !get_integer_constant_value(context, case_stmt->get_value())
                         .has_value() ||
                    !is_supported_stmt(context, case_stmt->get_body())) {
                    return false;
                }
                continue;
            }
            if (child->get_kind() == AstKind::DefaultStmt) {
                const auto *default_stmt =
                    static_cast<const DefaultStmt *>(child.get());
                if (!is_supported_stmt(context, default_stmt->get_body())) {
                    return false;
                }
                continue;
            }
            return false;
        }
        return true;
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(stmt);
        return case_stmt->get_value() != nullptr &&
               is_supported_expr(context, case_stmt->get_value()) &&
               get_integer_constant_value(context, case_stmt->get_value()).has_value() &&
               is_supported_stmt(context, case_stmt->get_body());
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(stmt);
        return is_supported_stmt(context, default_stmt->get_body());
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(stmt);
        return is_supported_stmt(context, label_stmt->get_body());
    }
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
    case AstKind::GotoStmt:
        return true;
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        const Expr *return_value = return_stmt->get_value();
        return return_value == nullptr || is_supported_expr(context, return_value);
    }
    default:
        return false;
    }
}

bool is_supported_expr(const CompilerContext &context, const Expr *expr) {
    if (expr == nullptr) {
        return false;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return true;
    case AstKind::FloatLiteralExpr:
        return is_supported_scalar_storage_type(get_node_type(context, expr));
    case AstKind::StringLiteralExpr:
        return is_supported_scalar_storage_type(get_node_type(context, expr));
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return false;
        }
        if (symbol->get_kind() == SymbolKind::Function) {
            const SemanticType *type = symbol->get_type();
            return type != nullptr && type->get_kind() == SemanticTypeKind::Function;
        }
        return symbol->get_decl_node() != nullptr &&
               is_supported_storage_type(symbol->get_type());
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        if (binary_expr->get_operator_text() == ",") {
            return is_supported_expr(context, binary_expr->get_lhs()) &&
                   is_supported_expr(context, binary_expr->get_rhs());
        }
        return (is_supported_arithmetic_operator(binary_expr->get_operator_text()) ||
                is_supported_bitwise_operator(binary_expr->get_operator_text()) ||
                is_supported_shift_operator(binary_expr->get_operator_text()) ||
                is_supported_comparison_operator(binary_expr->get_operator_text()) ||
                is_supported_logical_operator(binary_expr->get_operator_text())) &&
               is_supported_scalar_storage_type(get_node_type(context, expr)) &&
               is_supported_expr(context, binary_expr->get_lhs()) &&
               is_supported_expr(context, binary_expr->get_rhs());
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        return is_supported_scalar_storage_type(get_node_type(context, expr)) &&
               is_supported_expr(context, cast_expr->get_operand()) &&
               is_supported_scalar_storage_type(
                   get_node_type(context, cast_expr->get_operand()));
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        return is_supported_scalar_storage_type(get_node_type(context, expr)) &&
               is_supported_expr(context, conditional_expr->get_condition()) &&
               is_supported_expr(context, conditional_expr->get_true_expr()) &&
               is_supported_expr(context, conditional_expr->get_false_expr());
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        if (assign_expr->get_target() == nullptr) {
            return false;
        }
        if (!is_supported_lvalue_expr(assign_expr->get_target())) {
            return false;
        }
        return is_supported_storage_type(get_node_type(context, assign_expr)) &&
               is_supported_expr(context, assign_expr->get_value());
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        const SemanticType *owner_type =
            get_member_owner_type(get_node_type(context, member_expr->get_base()),
                                  member_expr->get_operator_text());
        std::size_t field_index = 0;
        const SemanticType *field_type = nullptr;
        if (!get_member_info(owner_type, member_expr->get_member_name(),
                             field_index, field_type)) {
            return false;
        }
        if (!is_supported_storage_type(field_type)) {
            return false;
        }
        if (member_expr->get_operator_text() == "->") {
            return is_supported_expr(context, member_expr->get_base());
        }
        return member_expr->get_base() != nullptr &&
               is_supported_lvalue_expr(member_expr->get_base());
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&") {
            return unary_expr->get_operand() != nullptr &&
                   is_supported_lvalue_expr(unary_expr->get_operand());
        }
        if (unary_expr->get_operator_text() == "*") {
            return is_supported_expr(context, unary_expr->get_operand()) &&
                   is_supported_storage_type(get_node_type(context, expr));
        }
        return is_supported_unary_operator(unary_expr->get_operator_text()) &&
               is_supported_expr(context, unary_expr->get_operand()) &&
               is_supported_scalar_storage_type(get_node_type(context, expr));
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        return (prefix_expr->get_operator_text() == "++" ||
                prefix_expr->get_operator_text() == "--") &&
               is_supported_lvalue_expr(prefix_expr->get_operand()) &&
               is_supported_scalar_storage_type(get_node_type(context, expr));
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        return (postfix_expr->get_operator_text() == "++" ||
                postfix_expr->get_operator_text() == "--") &&
               is_supported_lvalue_expr(postfix_expr->get_operand()) &&
               is_supported_scalar_storage_type(get_node_type(context, expr));
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        return is_supported_expr(context, index_expr->get_base()) &&
               is_supported_expr(context, index_expr->get_index()) &&
               is_supported_storage_type(get_node_type(context, expr));
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        if (call_expr->get_callee() == nullptr ||
            call_expr->get_callee()->get_kind() != AstKind::IdentifierExpr) {
            return false;
        }

        const SemanticType *callee_type =
            get_node_type(context, call_expr->get_callee());
        if (callee_type == nullptr ||
            callee_type->get_kind() != SemanticTypeKind::Function) {
            return false;
        }

        const auto *function_type =
            static_cast<const FunctionSemanticType *>(callee_type);
        if (!is_supported_return_type(function_type->get_return_type())) {
            return false;
        }

        const auto &parameter_types = function_type->get_parameter_types();
        if ((!function_type->get_is_variadic() &&
             parameter_types.size() != call_expr->get_arguments().size()) ||
            (function_type->get_is_variadic() &&
             call_expr->get_arguments().size() < parameter_types.size())) {
            return false;
        }

        for (std::size_t index = 0; index < call_expr->get_arguments().size();
             ++index) {
            if (!is_supported_expr(context, call_expr->get_arguments()[index].get())) {
                return false;
            }
            if (index < parameter_types.size() &&
                !is_supported_storage_type(parameter_types[index])) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

bool is_supported_function(const CompilerContext &context,
                           const FunctionDecl *function_decl) {
    if (function_decl == nullptr) {
        return false;
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    if (!is_supported_return_type(return_type)) {
        return false;
    }

    for (const auto &parameter : function_decl->get_parameters()) {
        if (!is_supported_decl(context, parameter.get())) {
            return false;
        }
    }

    return function_decl->get_body() != nullptr &&
           is_supported_stmt(context, function_decl->get_body());
}

std::string describe_unsupported_decl(const CompilerContext &context,
                                      const Decl *decl) {
    if (decl == nullptr) {
        return "null declaration";
    }

    switch (decl->get_kind()) {
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        if (!is_supported_type_for_storage(context, var_decl)) {
            return "variable declaration '" + var_decl->get_name() +
                   "' has unsupported storage type";
        }
        if (var_decl->get_initializer() != nullptr) {
            const std::string initializer_reason =
                describe_unsupported_initializer_for_type(
                    context, var_decl->get_initializer(),
                    get_node_type(context, var_decl));
            if (!initializer_reason.empty()) {
                return "initializer for variable '" + var_decl->get_name() +
                       "': " + initializer_reason;
            }
        }
        return {};
    }
    case AstKind::ParamDecl: {
        const auto *param_decl = static_cast<const ParamDecl *>(decl);
        if (!is_supported_type_for_storage(context, param_decl)) {
            return "parameter '" + param_decl->get_name() +
                   "' has unsupported storage type";
        }
        return {};
    }
    case AstKind::StructDecl:
    case AstKind::UnionDecl:
    case AstKind::EnumDecl:
        return {};
    default:
        return "declaration kind is unsupported";
    }
}

std::string describe_unsupported_stmt(const CompilerContext &context,
                                      const Stmt *stmt) {
    if (stmt == nullptr) {
        return "null statement";
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            const std::string child_reason =
                describe_unsupported_stmt(context, child.get());
            if (!child_reason.empty()) {
                return child_reason;
            }
        }
        return {};
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            const std::string decl_reason =
                describe_unsupported_decl(context, decl.get());
            if (!decl_reason.empty()) {
                return decl_reason;
            }
        }
        return {};
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        if (expr_stmt->get_expression() == nullptr) {
            return "empty expression statement";
        }
        return describe_unsupported_expr(context, expr_stmt->get_expression());
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        if (const std::string condition_reason =
                describe_unsupported_expr(context, if_stmt->get_condition());
            !condition_reason.empty()) {
            return "if condition: " + condition_reason;
        }
        if (const std::string then_reason =
                describe_unsupported_stmt(context, if_stmt->get_then_branch());
            !then_reason.empty()) {
            return then_reason;
        }
        if (const std::string else_reason =
                describe_unsupported_stmt(context, if_stmt->get_else_branch());
            !else_reason.empty()) {
            return else_reason;
        }
        return {};
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        if (const std::string condition_reason =
                describe_unsupported_expr(context, while_stmt->get_condition());
            !condition_reason.empty()) {
            return "while condition: " + condition_reason;
        }
        return describe_unsupported_stmt(context, while_stmt->get_body());
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        if (const std::string body_reason =
                describe_unsupported_stmt(context, do_while_stmt->get_body());
            !body_reason.empty()) {
            return body_reason;
        }
        if (const std::string condition_reason =
                describe_unsupported_expr(context, do_while_stmt->get_condition());
            !condition_reason.empty()) {
            return "do-while condition: " + condition_reason;
        }
        return {};
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        if (const std::string init_reason =
                describe_unsupported_expr(context, for_stmt->get_init());
            !init_reason.empty()) {
            return "for init: " + init_reason;
        }
        if (const std::string condition_reason =
                describe_unsupported_expr(context, for_stmt->get_condition());
            !condition_reason.empty()) {
            return "for condition: " + condition_reason;
        }
        if (const std::string step_reason =
                describe_unsupported_expr(context, for_stmt->get_step());
            !step_reason.empty()) {
            return "for step: " + step_reason;
        }
        return describe_unsupported_stmt(context, for_stmt->get_body());
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        if (const std::string condition_reason =
                describe_unsupported_expr(context, switch_stmt->get_condition());
            !condition_reason.empty()) {
            return "switch condition: " + condition_reason;
        }
        return describe_unsupported_stmt(context, switch_stmt->get_body());
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(stmt);
        if (case_stmt->get_value() == nullptr) {
            return "case statement without value";
        }
        if (!get_integer_constant_value(context, case_stmt->get_value()).has_value()) {
            return "case value is not an integer constant";
        }
        if (const std::string value_reason =
                describe_unsupported_expr(context, case_stmt->get_value());
            !value_reason.empty()) {
            return "case value: " + value_reason;
        }
        return describe_unsupported_stmt(context, case_stmt->get_body());
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(stmt);
        return describe_unsupported_stmt(context, default_stmt->get_body());
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(stmt);
        return describe_unsupported_stmt(context, label_stmt->get_body());
    }
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        return describe_unsupported_expr(context, return_stmt->get_value());
    }
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
    case AstKind::GotoStmt:
        return {};
    default:
        return "statement kind is unsupported";
    }
}

std::string describe_unsupported_expr(const CompilerContext &context,
                                      const Expr *expr) {
    if (expr == nullptr) {
        return {};
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return {};
    case AstKind::FloatLiteralExpr:
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "floating literal has unsupported result type";
    case AstKind::StringLiteralExpr:
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "string literal has unsupported result type";
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return "identifier is not bound";
        }
        if (symbol->get_kind() == SymbolKind::Function) {
            const SemanticType *type = symbol->get_type();
            return type != nullptr && type->get_kind() == SemanticTypeKind::Function
                       ? std::string()
                       : "identifier refers to unsupported function type";
        }
        if (symbol->get_decl_node() == nullptr) {
            return "identifier has no declaration node";
        }
        return is_supported_storage_type(symbol->get_type())
                   ? std::string()
                   : "identifier has unsupported storage type";
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        if (const std::string lhs_reason =
                describe_unsupported_expr(context, binary_expr->get_lhs());
            !lhs_reason.empty()) {
            return lhs_reason;
        }
        if (const std::string rhs_reason =
                describe_unsupported_expr(context, binary_expr->get_rhs());
            !rhs_reason.empty()) {
            return rhs_reason;
        }
        const std::string &op = binary_expr->get_operator_text();
        if (op == ",") {
            return {};
        }
        if (!(is_supported_arithmetic_operator(op) ||
              is_supported_bitwise_operator(op) ||
              is_supported_shift_operator(op) ||
              is_supported_comparison_operator(op) ||
              is_supported_logical_operator(op))) {
            return "binary operator '" + op + "' is unsupported";
        }
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "binary operator '" + op + "' has unsupported result type";
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        if (const std::string operand_reason =
                describe_unsupported_expr(context, cast_expr->get_operand());
            !operand_reason.empty()) {
            return operand_reason;
        }
        if (!is_supported_scalar_storage_type(get_node_type(context, expr))) {
            return "cast has unsupported target type";
        }
        return is_supported_scalar_storage_type(
                   get_node_type(context, cast_expr->get_operand()))
                   ? std::string()
                   : "cast operand has unsupported type";
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        if (const std::string condition_reason = describe_unsupported_expr(
                context, conditional_expr->get_condition());
            !condition_reason.empty()) {
            return "conditional condition: " + condition_reason;
        }
        if (const std::string true_reason =
                describe_unsupported_expr(context, conditional_expr->get_true_expr());
            !true_reason.empty()) {
            return true_reason;
        }
        if (const std::string false_reason = describe_unsupported_expr(
                context, conditional_expr->get_false_expr());
            !false_reason.empty()) {
            return false_reason;
        }
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "conditional expression has unsupported result type";
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        if (assign_expr->get_target() == nullptr) {
            return "assignment without target";
        }
        if (!is_supported_lvalue_expr(assign_expr->get_target())) {
            return "assignment target kind is unsupported";
        }
        if (const std::string value_reason =
                describe_unsupported_expr(context, assign_expr->get_value());
            !value_reason.empty()) {
            return value_reason;
        }
        return is_supported_storage_type(get_node_type(context, assign_expr))
                   ? std::string()
                   : "assignment result type is unsupported";
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        const SemanticType *owner_type =
            get_member_owner_type(get_node_type(context, member_expr->get_base()),
                                  member_expr->get_operator_text());
        std::size_t field_index = 0;
        const SemanticType *field_type = nullptr;
        if (!get_member_info(owner_type, member_expr->get_member_name(),
                             field_index, field_type)) {
            return "member access '" + member_expr->get_member_name() +
                   "' cannot be resolved";
        }
        if (!is_supported_storage_type(field_type)) {
            return "member access '" + member_expr->get_member_name() +
                   "' has unsupported field type";
        }
        if (member_expr->get_operator_text() == "->") {
            return describe_unsupported_expr(context, member_expr->get_base());
        }
        if (member_expr->get_base() == nullptr) {
            return "member access without base";
        }
        return is_supported_lvalue_expr(member_expr->get_base())
                   ? std::string()
                   : "dot-member base expression kind is unsupported";
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&") {
            if (unary_expr->get_operand() == nullptr) {
                return "address-of without operand";
            }
            return is_supported_lvalue_expr(unary_expr->get_operand())
                       ? std::string()
                       : "address-of operand kind is unsupported";
        }
        if (unary_expr->get_operator_text() == "*") {
            if (const std::string operand_reason =
                    describe_unsupported_expr(context, unary_expr->get_operand());
                !operand_reason.empty()) {
                return operand_reason;
            }
            return is_supported_storage_type(get_node_type(context, expr))
                       ? std::string()
                       : "dereference result type is unsupported";
        }
        if (!is_supported_unary_operator(unary_expr->get_operator_text())) {
            return "unary operator '" + unary_expr->get_operator_text() +
                   "' is unsupported";
        }
        if (const std::string operand_reason =
                describe_unsupported_expr(context, unary_expr->get_operand());
            !operand_reason.empty()) {
            return operand_reason;
        }
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "unary operator '" + unary_expr->get_operator_text() +
                         "' has unsupported result type";
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        if (prefix_expr->get_operator_text() != "++" &&
            prefix_expr->get_operator_text() != "--") {
            return "prefix operator '" + prefix_expr->get_operator_text() +
                   "' is unsupported";
        }
        if (!is_supported_lvalue_expr(prefix_expr->get_operand())) {
            return "prefix operand kind is unsupported";
        }
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "prefix expression result type is unsupported";
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        if (postfix_expr->get_operator_text() != "++" &&
            postfix_expr->get_operator_text() != "--") {
            return "postfix operator '" + postfix_expr->get_operator_text() +
                   "' is unsupported";
        }
        if (!is_supported_lvalue_expr(postfix_expr->get_operand())) {
            return "postfix operand kind is unsupported";
        }
        return is_supported_scalar_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "postfix expression result type is unsupported";
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        if (const std::string base_reason =
                describe_unsupported_expr(context, index_expr->get_base());
            !base_reason.empty()) {
            return base_reason;
        }
        if (const std::string index_reason =
                describe_unsupported_expr(context, index_expr->get_index());
            !index_reason.empty()) {
            return index_reason;
        }
        return is_supported_storage_type(get_node_type(context, expr))
                   ? std::string()
                   : "index expression result type is unsupported";
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        if (call_expr->get_callee() == nullptr ||
            call_expr->get_callee()->get_kind() != AstKind::IdentifierExpr) {
            return "call callee kind is unsupported";
        }
        const SemanticType *callee_type = get_node_type(context, call_expr->get_callee());
        if (callee_type == nullptr ||
            callee_type->get_kind() != SemanticTypeKind::Function) {
            return "call callee does not resolve to a function";
        }
        const auto *function_type =
            static_cast<const FunctionSemanticType *>(callee_type);
        if (!is_supported_return_type(function_type->get_return_type())) {
            return "call return type is unsupported";
        }
        const auto &parameter_types = function_type->get_parameter_types();
        if ((!function_type->get_is_variadic() &&
             parameter_types.size() != call_expr->get_arguments().size()) ||
            (function_type->get_is_variadic() &&
             call_expr->get_arguments().size() < parameter_types.size())) {
            return "call argument count is unsupported";
        }
        for (std::size_t index = 0; index < call_expr->get_arguments().size();
             ++index) {
            if (const std::string argument_reason = describe_unsupported_expr(
                    context, call_expr->get_arguments()[index].get());
                !argument_reason.empty()) {
                return "call argument " + std::to_string(index) + ": " +
                       argument_reason;
            }
            if (index < parameter_types.size() &&
                !is_supported_storage_type(parameter_types[index])) {
                return "call parameter " + std::to_string(index) +
                       " has unsupported type";
            }
        }
        return {};
    }
    default:
        return "expression kind is unsupported";
    }
}

std::vector<IRFunctionAttribute>
collect_ir_function_attributes(const CompilerContext &context,
                               const FunctionDecl *function_decl) {
    std::vector<IRFunctionAttribute> attributes;
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || function_decl == nullptr) {
        return attributes;
    }

    const std::vector<SemanticFunctionAttribute> *function_attributes =
        semantic_model->get_function_attributes(function_decl);
    if (function_attributes == nullptr) {
        return attributes;
    }

    const auto &lowering_registry =
        context.get_dialect_manager().get_ir_extension_lowering_registry();
    const auto &ir_feature_registry =
        context.get_dialect_manager().get_ir_feature_registry();
    if (!ir_feature_registry.has_feature(IrFeature::FunctionAttributes)) {
        return attributes;
    }
    if (!lowering_registry.has_handler(
            IrExtensionLoweringHandlerKind::GnuFunctionAttributes)) {
        return attributes;
    }

    GnuFunctionAttributeLoweringHandler lowering_handler;
    return lowering_handler.lower_function_attributes(*function_attributes);
}

std::string get_function_ir_name(const FunctionDecl *function_decl) {
    if (function_decl == nullptr || function_decl->get_asm_label().empty()) {
        return function_decl == nullptr ? "" : function_decl->get_name();
    }
    return function_decl->get_asm_label();
}

bool get_function_is_internal_linkage(const FunctionDecl *function_decl) {
    return function_decl != nullptr && function_decl->get_is_static();
}

std::string get_symbol_ir_name(const SemanticSymbol *symbol) {
    if (symbol == nullptr) {
        return "";
    }
    const AstNode *decl_node = symbol->get_decl_node();
    if (decl_node != nullptr && decl_node->get_kind() == AstKind::FunctionDecl) {
        return get_function_ir_name(static_cast<const FunctionDecl *>(decl_node));
    }
    return symbol->get_name();
}

std::optional<LValueAddress> build_lvalue_address(
    IRBackend &backend, const CompilerContext &context, EmissionState &state,
    const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr || symbol->get_decl_node() == nullptr) {
            const VariableSemanticInfo *variable_info =
                get_variable_info(context, symbol);
            if (symbol != nullptr && variable_info != nullptr &&
                variable_info->get_is_global_storage()) {
                backend.declare_global(symbol->get_name(), symbol->get_type(),
                                       variable_info->get_has_internal_linkage());
                return LValueAddress{"@" + symbol->get_name(), symbol->get_type()};
            }
            return std::nullopt;
        }
        const std::string *address =
            state.symbol_value_map.get_value(symbol->get_decl_node());
        if (address == nullptr) {
            const VariableSemanticInfo *variable_info =
                get_variable_info(context, symbol);
            if (variable_info != nullptr && variable_info->get_is_global_storage()) {
                backend.declare_global(symbol->get_name(), symbol->get_type(),
                                       variable_info->get_has_internal_linkage());
                return LValueAddress{"@" + symbol->get_name(), symbol->get_type()};
            }
            return std::nullopt;
        }
        return LValueAddress{*address, symbol->get_type()};
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        const SemanticType *owner_type =
            get_member_owner_type(get_node_type(context, member_expr->get_base()),
                                  member_expr->get_operator_text());
        std::size_t field_index = 0;
        const SemanticType *field_type = nullptr;
        if (!get_member_info(owner_type, member_expr->get_member_name(),
                             field_index, field_type)) {
            return std::nullopt;
        }

        std::string base_address;
        if (member_expr->get_operator_text() == "->") {
            std::optional<IRValue> base_pointer =
                build_expr(backend, context, state, member_expr->get_base());
            if (!base_pointer.has_value()) {
                return std::nullopt;
            }
            base_address = base_pointer->text;
        } else {
            std::optional<LValueAddress> base_lvalue =
                build_lvalue_address(backend, context, state,
                                     member_expr->get_base());
            if (!base_lvalue.has_value()) {
                return std::nullopt;
            }
            base_address = base_lvalue->address;
        }

        const std::string member_address = backend.emit_member_address(
            base_address, owner_type, field_index, field_type);
        if (member_address.empty()) {
            return std::nullopt;
        }
        LValueAddress member_lvalue{member_address, field_type};
        const auto field_layout =
            detail::get_aggregate_field_layout(owner_type, field_index);
        if (field_layout.has_value() && field_layout->is_bit_field) {
            member_lvalue.bit_field_layout = field_layout;
        }
        return member_lvalue;
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() != "*") {
            return std::nullopt;
        }
        std::optional<IRValue> pointer_value =
            build_expr(backend, context, state, unary_expr->get_operand());
        if (!pointer_value.has_value()) {
            return std::nullopt;
        }
        return LValueAddress{pointer_value->text, get_node_type(context, expr)};
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        std::optional<IRValue> base =
            build_expr(backend, context, state, index_expr->get_base());
        std::optional<IRValue> index =
            build_expr(backend, context, state, index_expr->get_index());
        const SemanticType *element_type = get_node_type(context, expr);
        if (!base.has_value() || !index.has_value() || element_type == nullptr) {
            return std::nullopt;
        }
        const std::string address =
            backend.emit_element_address(base->text, element_type, *index);
        if (address.empty()) {
            return std::nullopt;
        }
        return LValueAddress{address, element_type};
    }
    default:
        return std::nullopt;
    }
}

std::optional<IRValue>
build_expr(IRBackend &backend, const CompilerContext &context,
           EmissionState &state, const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    const SemanticType *expr_type = strip_qualifiers(get_node_type(context, expr));
    if (expr_type != nullptr && expr_type->get_kind() == SemanticTypeKind::Array &&
        is_supported_lvalue_expr(expr)) {
        std::optional<LValueAddress> array_address =
            build_lvalue_address(backend, context, state, expr);
        if (!array_address.has_value()) {
            return std::nullopt;
        }
        return decay_array_lvalue_to_pointer(backend, *array_address);
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr: {
        const auto *integer_literal =
            static_cast<const IntegerLiteralExpr *>(expr);
        const auto parsed_literal =
            parse_integer_literal_info(integer_literal->get_value_text());
        const SemanticType *literal_type = get_node_type(context, expr);
        if (!parsed_literal.has_value() || literal_type == nullptr) {
            return std::nullopt;
        }
        return IRValue{std::to_string(parsed_literal->magnitude), literal_type};
    }
    case AstKind::FloatLiteralExpr: {
        const auto *float_literal = static_cast<const FloatLiteralExpr *>(expr);
        const SemanticType *type = get_node_type(context, expr);
        if (type == nullptr) {
            return std::nullopt;
        }
        if (is_builtin_type_named(type, "long double")) {
            static BuiltinSemanticType double_type("double");
            const IRValue double_literal =
                backend.emit_floating_literal(float_literal->get_value_text(),
                                              &double_type);
            return coerce_ir_value(backend, double_literal, type);
        }
        return backend.emit_floating_literal(float_literal->get_value_text(),
                                             type);
    }
    case AstKind::StringLiteralExpr: {
        const auto *string_literal = static_cast<const StringLiteralExpr *>(expr);
        const SemanticType *type = get_node_type(context, expr);
        if (type == nullptr) {
            return std::nullopt;
        }
        return backend.emit_string_literal(string_literal->get_value_text(), type);
    }
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return std::nullopt;
        }

        if (symbol->get_kind() == SymbolKind::Function) {
            return IRValue{"@" + get_symbol_ir_name(symbol), symbol->get_type()};
        }

        const AstNode *decl_node = symbol->get_decl_node();
        if (decl_node == nullptr) {
            const VariableSemanticInfo *variable_info =
                get_variable_info(context, symbol);
            if (variable_info != nullptr && variable_info->get_is_global_storage()) {
                backend.declare_global(symbol->get_name(), symbol->get_type(),
                                       variable_info->get_has_internal_linkage());
                return backend.emit_load("@" + symbol->get_name(),
                                         symbol->get_type());
            }
            return std::nullopt;
        }

        const std::string *address = state.symbol_value_map.get_value(decl_node);
        if (address == nullptr) {
            const VariableSemanticInfo *variable_info =
                get_variable_info(context, symbol);
            if (variable_info != nullptr && variable_info->get_is_global_storage()) {
                backend.declare_global(symbol->get_name(), symbol->get_type(),
                                       variable_info->get_has_internal_linkage());
                return backend.emit_load("@" + symbol->get_name(),
                                         symbol->get_type());
            }
            return std::nullopt;
        }
        return backend.emit_load(*address, symbol->get_type());
    }
    case AstKind::MemberExpr: {
        std::optional<LValueAddress> member_address =
            build_lvalue_address(backend, context, state, expr);
        if (!member_address.has_value() || member_address->type == nullptr) {
            return std::nullopt;
        }
        return load_lvalue_value(backend, context, *member_address);
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&") {
            std::optional<LValueAddress> address =
                build_lvalue_address(backend, context, state,
                                     unary_expr->get_operand());
            if (!address.has_value()) {
                return std::nullopt;
            }
            return IRValue{address->address, get_node_type(context, expr)};
        }
        if (unary_expr->get_operator_text() == "*") {
            std::optional<LValueAddress> address =
                build_lvalue_address(backend, context, state, expr);
            if (!address.has_value() || address->type == nullptr) {
                return std::nullopt;
            }
            return load_lvalue_value(backend, context, *address);
        }

        const SemanticType *result_type = get_node_type(context, expr);
        std::optional<IRValue> operand =
            build_expr(backend, context, state, unary_expr->get_operand());
        if (!operand.has_value() || result_type == nullptr) {
            return std::nullopt;
        }
        std::optional<IRValue> coerced_operand =
            coerce_ir_value(backend, *operand, result_type);
        if (unary_expr->get_operator_text() == "+") {
            return coerced_operand;
        }
        if (unary_expr->get_operator_text() == "-") {
            if (!coerced_operand.has_value()) {
                return std::nullopt;
            }
            std::optional<IRValue> zero = build_zero_ir_value(backend, result_type);
            if (!zero.has_value()) {
                return std::nullopt;
            }
            return backend.emit_binary("-", *zero, *coerced_operand, result_type);
        }
        if (unary_expr->get_operator_text() == "~") {
            if (!coerced_operand.has_value()) {
                return std::nullopt;
            }
            std::optional<IRValue> mask =
                build_all_ones_ir_value(backend, result_type);
            if (!mask.has_value()) {
                return std::nullopt;
            }
            return backend.emit_binary("^", *coerced_operand, *mask, result_type);
        }
        if (unary_expr->get_operator_text() == "!") {
            std::optional<IRValue> zero = build_zero_ir_value(backend, operand->type);
            if (!zero.has_value()) {
                return std::nullopt;
            }
            return backend.emit_binary("==", *operand, *zero, result_type);
        }
        return std::nullopt;
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        if (prefix_expr->get_operator_text() != "++" &&
            prefix_expr->get_operator_text() != "--") {
            return std::nullopt;
        }
        return emit_increment_expr(backend, context, state,
                                   prefix_expr->get_operand(),
                                   prefix_expr->get_operator_text() == "++",
                                   true);
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        if (postfix_expr->get_operator_text() != "++" &&
            postfix_expr->get_operator_text() != "--") {
            return std::nullopt;
        }
        return emit_increment_expr(backend, context, state,
                                   postfix_expr->get_operand(),
                                   postfix_expr->get_operator_text() == "++",
                                   false);
    }
    case AstKind::IndexExpr: {
        std::optional<LValueAddress> element_address =
            build_lvalue_address(backend, context, state, expr);
        if (!element_address.has_value() || element_address->type == nullptr) {
            return std::nullopt;
        }
        return load_lvalue_value(backend, context, *element_address);
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const SemanticType *result_type = get_node_type(context, expr);
        if (result_type == nullptr) {
            return std::nullopt;
        }
        if (binary_expr->get_operator_text() == ",") {
            if (!build_expr(backend, context, state, binary_expr->get_lhs())
                     .has_value()) {
                return std::nullopt;
            }
            return build_expr(backend, context, state, binary_expr->get_rhs());
        }
        const SemanticType *lhs_type =
            strip_qualifiers(get_node_type(context, binary_expr->get_lhs()));
        const SemanticType *rhs_type =
            strip_qualifiers(get_node_type(context, binary_expr->get_rhs()));

        if (binary_expr->get_operator_text() == "+" &&
            lhs_type != nullptr &&
            lhs_type->get_kind() == SemanticTypeKind::Pointer &&
            rhs_type != nullptr && rhs_type->get_kind() == SemanticTypeKind::Builtin) {
            std::optional<IRValue> base =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> index =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!base.has_value() || !index.has_value()) {
                return std::nullopt;
            }
            const SemanticType *pointee_type =
                static_cast<const PointerSemanticType *>(lhs_type)->get_pointee_type();
            const std::string address =
                backend.emit_element_address(base->text, pointee_type, *index);
            if (address.empty()) {
                return std::nullopt;
            }
            return IRValue{address, result_type};
        }

        if (binary_expr->get_operator_text() == "+" &&
            rhs_type != nullptr &&
            rhs_type->get_kind() == SemanticTypeKind::Pointer &&
            lhs_type != nullptr && lhs_type->get_kind() == SemanticTypeKind::Builtin) {
            std::optional<IRValue> index =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> base =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!base.has_value() || !index.has_value()) {
                return std::nullopt;
            }
            const SemanticType *pointee_type =
                static_cast<const PointerSemanticType *>(rhs_type)->get_pointee_type();
            const std::string address =
                backend.emit_element_address(base->text, pointee_type, *index);
            if (address.empty()) {
                return std::nullopt;
            }
            return IRValue{address, result_type};
        }

        if (binary_expr->get_operator_text() == "-" &&
            lhs_type != nullptr &&
            lhs_type->get_kind() == SemanticTypeKind::Pointer &&
            rhs_type != nullptr && rhs_type->get_kind() == SemanticTypeKind::Builtin) {
            std::optional<IRValue> base =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> index =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!base.has_value() || !index.has_value()) {
                return std::nullopt;
            }
            static BuiltinSemanticType int_type("int");
            IRValue zero = backend.emit_integer_literal(0);
            IRValue negated_index =
                backend.emit_binary("-", zero, *index, &int_type);
            if (negated_index.text.empty()) {
                return std::nullopt;
            }
            const SemanticType *pointee_type =
                static_cast<const PointerSemanticType *>(lhs_type)->get_pointee_type();
            const std::string address = backend.emit_element_address(
                base->text, pointee_type, negated_index);
            if (address.empty()) {
                return std::nullopt;
            }
            return IRValue{address, result_type};
        }

        if (binary_expr->get_operator_text() == "-" &&
            lhs_type != nullptr && rhs_type != nullptr &&
            lhs_type->get_kind() == SemanticTypeKind::Pointer &&
            rhs_type->get_kind() == SemanticTypeKind::Pointer) {
            std::optional<IRValue> lhs =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> rhs =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!lhs.has_value() || !rhs.has_value()) {
                return std::nullopt;
            }
            const SemanticType *pointee_type =
                static_cast<const PointerSemanticType *>(lhs_type)->get_pointee_type();
            IRValue result = backend.emit_pointer_difference(
                *lhs, *rhs, pointee_type, result_type);
            if (result.text.empty()) {
                return std::nullopt;
            }
            return result;
        }

        if (is_supported_logical_operator(binary_expr->get_operator_text())) {
            std::optional<IRValue> lhs =
                build_expr(backend, context, state, binary_expr->get_lhs());
            if (!lhs.has_value()) {
                return std::nullopt;
            }

            const std::string result_address =
                backend.emit_alloca("", result_type);
            backend.emit_store(result_address, backend.emit_integer_literal(0));

            const std::string rhs_label = backend.create_label("logic.rhs");
            const std::string true_label = backend.create_label("logic.true");
            const std::string end_label = backend.create_label("logic.end");

            if (binary_expr->get_operator_text() == "&&") {
                backend.emit_cond_branch(*lhs, rhs_label, end_label);
            } else {
                backend.emit_cond_branch(*lhs, true_label, rhs_label);
            }

            backend.emit_label(rhs_label);
            std::optional<IRValue> rhs =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            backend.emit_cond_branch(*rhs, true_label, end_label);

            backend.emit_label(true_label);
            backend.emit_store(result_address, backend.emit_integer_literal(1));
            backend.emit_branch(end_label);

            backend.emit_label(end_label);
            return backend.emit_load(result_address, result_type);
        }

        const bool is_arithmetic_binary =
            is_supported_arithmetic_operator(binary_expr->get_operator_text());
        const bool is_comparison_binary =
            is_supported_comparison_operator(binary_expr->get_operator_text());
        if (is_arithmetic_binary || is_comparison_binary) {
            std::optional<IRValue> lhs =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> rhs =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!lhs.has_value() || !rhs.has_value() || result_type == nullptr) {
                return std::nullopt;
            }

            const SemanticType *operand_target_type = result_type;
            bool should_coerce_operands = true;
            if (is_comparison_binary) {
                const SemanticType *arithmetic_type =
                    get_usual_arithmetic_conversion_type(
                        context, binary_expr->get_lhs(), lhs_type,
                        binary_expr->get_rhs(), rhs_type);
                if (arithmetic_type != nullptr) {
                    operand_target_type = arithmetic_type;
                } else {
                    should_coerce_operands = false;
                }
            }

            if (should_coerce_operands && operand_target_type != nullptr) {
                std::optional<IRValue> coerced_lhs =
                    coerce_binary_operand(backend, *lhs, operand_target_type);
                std::optional<IRValue> coerced_rhs =
                    coerce_binary_operand(backend, *rhs, operand_target_type);
                if (!coerced_lhs.has_value() || !coerced_rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = std::move(coerced_lhs);
                rhs = std::move(coerced_rhs);
            }

            IRValue result = backend.emit_binary(binary_expr->get_operator_text(),
                                                 *lhs, *rhs, result_type);
            if (result.text.empty()) {
                return std::nullopt;
            }
            return result;
        }

        if (binary_expr->get_operator_text() == "%" ||
            binary_expr->get_operator_text() == "<<" ||
            binary_expr->get_operator_text() == ">>" ||
            binary_expr->get_operator_text() == "&" ||
            binary_expr->get_operator_text() == "|" ||
            binary_expr->get_operator_text() == "^") {
            std::optional<IRValue> lhs =
                build_expr(backend, context, state, binary_expr->get_lhs());
            std::optional<IRValue> rhs =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!lhs.has_value() || !rhs.has_value() || result_type == nullptr) {
                return std::nullopt;
            }

            std::optional<IRValue> coerced_lhs =
                coerce_binary_operand(backend, *lhs, result_type);
            std::optional<IRValue> coerced_rhs =
                coerce_binary_operand(backend, *rhs, result_type);
            if (!coerced_lhs.has_value() || !coerced_rhs.has_value()) {
                return std::nullopt;
            }

            IRValue result = backend.emit_binary(binary_expr->get_operator_text(),
                                                 *coerced_lhs, *coerced_rhs,
                                                 result_type);
            if (result.text.empty()) {
                return std::nullopt;
            }
            return result;
        }

        return std::nullopt;
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        const SemanticType *target_type = get_node_type(context, expr);
        if (target_type == nullptr) {
            return std::nullopt;
        }
        std::optional<IRValue> operand =
            build_expr(backend, context, state, cast_expr->get_operand());
        if (!operand.has_value()) {
            return std::nullopt;
        }
        IRValue result = backend.emit_cast(*operand, target_type);
        if (result.text.empty()) {
            return std::nullopt;
        }
        return result;
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        const SemanticType *result_type = get_node_type(context, expr);
        if (result_type == nullptr) {
            return std::nullopt;
        }

        std::optional<IRValue> condition =
            build_expr(backend, context, state, conditional_expr->get_condition());
        if (!condition.has_value()) {
            return std::nullopt;
        }

        const std::string result_address = backend.emit_alloca("", result_type);
        const std::string true_label = backend.create_label("cond.true");
        const std::string false_label = backend.create_label("cond.false");
        const std::string end_label = backend.create_label("cond.end");

        backend.emit_cond_branch(*condition, true_label, false_label);

        backend.emit_label(true_label);
        std::optional<IRValue> true_value =
            build_expr(backend, context, state, conditional_expr->get_true_expr());
        if (!true_value.has_value()) {
            return std::nullopt;
        }
        backend.emit_store(result_address, *true_value);
        backend.emit_branch(end_label);

        backend.emit_label(false_label);
        std::optional<IRValue> false_value = build_expr(
            backend, context, state, conditional_expr->get_false_expr());
        if (!false_value.has_value()) {
            return std::nullopt;
        }
        backend.emit_store(result_address, *false_value);
        backend.emit_branch(end_label);

        backend.emit_label(end_label);
        return backend.emit_load(result_address, result_type);
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        std::optional<LValueAddress> target_address =
            build_lvalue_address(backend, context, state,
                                 assign_expr->get_target());
        if (!target_address.has_value()) {
            return std::nullopt;
        }

        std::optional<IRValue> value =
            build_expr(backend, context, state, assign_expr->get_value());
        if (!value.has_value()) {
            return std::nullopt;
        }

        std::optional<IRValue> coerced_value;
        if (assign_expr->get_operator_text() == "=") {
            coerced_value = coerce_ir_value(backend, *value, target_address->type);
        } else if (is_supported_compound_assignment_operator(
                       assign_expr->get_operator_text())) {
            std::optional<IRValue> current_value =
                load_lvalue_value(backend, context, *target_address);
            if (!current_value.has_value()) {
                return std::nullopt;
            }
            coerced_value = build_compound_assignment_result(
                backend, context, state, assign_expr, target_address->type,
                *current_value, *value);
        }
        if (!coerced_value.has_value()) {
            return std::nullopt;
        }

        return store_lvalue_value(backend, context, *target_address,
                                  *coerced_value);
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        const auto *callee_identifier = static_cast<const IdentifierExpr *>(
            call_expr->get_callee());
        const SemanticSymbol *callee_symbol =
            get_symbol_binding(context, callee_identifier);
        std::optional<IRValue> callee =
            build_expr(backend, context, state, call_expr->get_callee());
        const SemanticType *return_type = get_node_type(context, expr);
        if (!callee.has_value() || return_type == nullptr ||
            callee_symbol == nullptr) {
            return std::nullopt;
        }

        const auto *function_type =
            static_cast<const FunctionSemanticType *>(callee_symbol->get_type());
        const AstNode *callee_decl = callee_symbol->get_decl_node();
        bool needs_function_declaration = callee_decl == nullptr;
        if (callee_decl != nullptr &&
            callee_decl->get_kind() == AstKind::FunctionDecl) {
            needs_function_declaration =
                static_cast<const FunctionDecl *>(callee_decl)->get_body() == nullptr;
        }
        if (needs_function_declaration && state.defined_function_names != nullptr &&
            state.defined_function_names->find(get_symbol_ir_name(callee_symbol)) !=
                state.defined_function_names->end()) {
            needs_function_declaration = false;
        }
        if (needs_function_declaration) {
            backend.declare_function(get_symbol_ir_name(callee_symbol),
                                     function_type->get_return_type(),
                                     function_type->get_parameter_types(),
                                     function_type->get_is_variadic(), false);
        }

        std::vector<IRValue> arguments;
        arguments.reserve(call_expr->get_arguments().size());
        for (std::size_t index = 0; index < call_expr->get_arguments().size();
             ++index) {
            const auto &argument = call_expr->get_arguments()[index];
            std::optional<IRValue> lowered_argument =
                build_expr(backend, context, state, argument.get());
            if (!lowered_argument.has_value()) {
                return std::nullopt;
            }
            std::optional<IRValue> coerced_argument;
            if (index < function_type->get_parameter_types().size()) {
                coerced_argument = coerce_ir_value(
                    backend, *lowered_argument,
                    function_type->get_parameter_types()[index]);
            } else {
                coerced_argument = coerce_variadic_argument(
                    backend, context, argument.get(), *lowered_argument);
            }
            if (!coerced_argument.has_value()) {
                return std::nullopt;
            }
            arguments.push_back(*coerced_argument);
        }

        return backend.emit_call(callee->text, arguments, return_type,
                                 function_type->get_parameter_types(),
                                 function_type->get_is_variadic());
    }
    default:
        return std::nullopt;
    }
}

bool emit_decl(IRBackend &backend, const CompilerContext &context,
               EmissionState &state, const Decl *decl) {
    if (decl == nullptr) {
        return false;
    }

    if (decl->get_kind() == AstKind::StructDecl ||
        decl->get_kind() == AstKind::UnionDecl ||
        decl->get_kind() == AstKind::EnumDecl) {
        return true;
    }

    if (decl->get_kind() != AstKind::VarDecl) {
        return false;
    }

    const auto *var_decl = static_cast<const VarDecl *>(decl);
    if (var_decl->get_is_extern()) {
        return true;
    }
    const SemanticType *declared_type = get_node_type(context, var_decl);
    if (!is_supported_storage_type(declared_type)) {
        return false;
    }

    const std::string address =
        backend.emit_alloca(var_decl->get_name(), declared_type);
    state.symbol_value_map.bind_value(var_decl, address);

    if (var_decl->get_initializer() != nullptr) {
        if (!emit_local_initializer_to_lvalue(
                backend, context, state,
                LValueAddress{address, declared_type},
                var_decl->get_initializer())) {
            return false;
        }
    }

    return true;
}

const std::string &get_or_create_goto_label(IRBackend &backend,
                                            EmissionState &state,
                                            const std::string &label_name) {
    auto it = state.goto_labels.find(label_name);
    if (it != state.goto_labels.end()) {
        return it->second;
    }
    const std::string created_label =
        backend.create_label("goto." + label_name);
    auto inserted =
        state.goto_labels.emplace(label_name, std::move(created_label));
    return inserted.first->second;
}

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type) {
    if (target_type == nullptr || value.type == nullptr) {
        return std::nullopt;
    }
    if (detail::get_llvm_type_name(value.type) ==
        detail::get_llvm_type_name(target_type)) {
        return IRValue{value.text, target_type};
    }
    detail::IntegerConversionService integer_conversion_service;
    const auto integer_conversion_plan =
        integer_conversion_service.get_integer_conversion_plan(value.type,
                                                               target_type);
    if (integer_conversion_plan.get_kind() !=
        detail::IntegerConversionKind::Unsupported) {
        IRValue converted = backend.emit_integer_conversion(
            value, integer_conversion_plan.get_kind(), target_type);
        if (converted.text.empty()) {
            return std::nullopt;
        }
        return converted;
    }
    IRValue coerced = backend.emit_cast(value, target_type);
    if (coerced.text.empty()) {
        return std::nullopt;
    }
    return coerced;
}

std::optional<IRValue> build_zero_ir_value(IRBackend &backend,
                                           const SemanticType *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    const SemanticType *unqualified_type = strip_qualifiers(type);
    if (unqualified_type != nullptr &&
        (unqualified_type->get_kind() == SemanticTypeKind::Array ||
         unqualified_type->get_kind() == SemanticTypeKind::Struct ||
         unqualified_type->get_kind() == SemanticTypeKind::Union)) {
        return IRValue{"zeroinitializer", type};
    }
    if (is_builtin_type_named(type, "float") || is_builtin_type_named(type, "double") ||
        is_builtin_type_named(type, "_Float16") ||
        is_builtin_type_named(type, "long double")) {
        return backend.emit_floating_literal("0.0", type);
    }

    IRValue zero = backend.emit_integer_literal(0);
    return coerce_ir_value(backend, zero, type);
}

std::optional<IRValue> build_all_ones_ir_value(IRBackend &backend,
                                               const SemanticType *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    IRValue all_ones = backend.emit_integer_literal(-1);
    return coerce_ir_value(backend, all_ones, type);
}

bool emit_zero_initialize_lvalue(IRBackend &backend,
                                 const CompilerContext &context,
                                 EmissionState &state,
                                 const LValueAddress &lvalue) {
    const SemanticType *target_type = strip_qualifiers(lvalue.type);
    if (target_type == nullptr) {
        return false;
    }

    if (target_type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(target_type);
        if (array_type->get_dimensions().empty()) {
            return false;
        }
        std::optional<IRValue> base_pointer =
            decay_array_lvalue_to_pointer(backend, lvalue);
        if (!base_pointer.has_value()) {
            return false;
        }
        const SemanticType *element_type = array_type->get_element_type();
        const int element_count = array_type->get_dimensions().front();
        for (int index = 0; index < element_count; ++index) {
            const std::string element_address = backend.emit_element_address(
                base_pointer->text, element_type, backend.emit_integer_literal(index));
            if (element_address.empty()) {
                return false;
            }
            if (!emit_zero_initialize_lvalue(
                    backend, context, state,
                    LValueAddress{element_address, element_type})) {
                return false;
            }
        }
        return true;
    }

    if (target_type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type =
            static_cast<const StructSemanticType *>(target_type);
        for (std::size_t field_index = 0; field_index < struct_type->get_fields().size();
             ++field_index) {
            const auto &field = struct_type->get_fields()[field_index];
            const auto field_layout =
                detail::get_aggregate_field_layout(target_type, field_index);
            if (!field_layout.has_value()) {
                continue;
            }
            const std::string field_address = backend.emit_member_address(
                lvalue.address, target_type, field_index, field.get_type());
            if (field_address.empty()) {
                return false;
            }
            LValueAddress field_lvalue{field_address, field.get_type()};
            if (field_layout->is_bit_field) {
                field_lvalue.bit_field_layout = field_layout;
            }
            if (!emit_zero_initialize_lvalue(
                    backend, context, state, field_lvalue)) {
                return false;
            }
        }
        return true;
    }

    if (target_type->get_kind() == SemanticTypeKind::Union) {
        std::optional<IRValue> zero_union = build_zero_ir_value(backend, lvalue.type);
        if (!zero_union.has_value()) {
            return false;
        }
        return store_lvalue_value(backend, context, lvalue, *zero_union).has_value();
    }

    std::optional<IRValue> zero = build_zero_ir_value(backend, lvalue.type);
    if (!zero.has_value()) {
        return false;
    }
    return store_lvalue_value(backend, context, lvalue, *zero).has_value();
}

bool emit_local_initializer_to_lvalue(IRBackend &backend,
                                      const CompilerContext &context,
                                      EmissionState &state,
                                      const LValueAddress &lvalue,
                                      const Expr *initializer) {
    if (initializer == nullptr) {
        return emit_zero_initialize_lvalue(backend, context, state, lvalue);
    }

    const SemanticType *target_type = strip_qualifiers(lvalue.type);
    if (target_type == nullptr) {
        return false;
    }

    if (target_type->get_kind() == SemanticTypeKind::Array) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *array_type = static_cast<const ArraySemanticType *>(target_type);
        if (array_type->get_dimensions().empty()) {
            return false;
        }
        std::optional<IRValue> base_pointer =
            decay_array_lvalue_to_pointer(backend, lvalue);
        if (!base_pointer.has_value()) {
            return false;
        }

        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        const SemanticType *element_type = array_type->get_element_type();
        const int element_count = array_type->get_dimensions().front();
        std::size_t provided_count = init_list->get_elements().size();
        if (provided_count > static_cast<std::size_t>(element_count)) {
            return false;
        }

        for (int index = 0; index < element_count; ++index) {
            const std::string element_address = backend.emit_element_address(
                base_pointer->text, element_type, backend.emit_integer_literal(index));
            if (element_address.empty()) {
                return false;
            }
            LValueAddress element_lvalue{element_address, element_type};
            const Expr *element_initializer =
                index < static_cast<int>(provided_count)
                    ? init_list->get_elements()[static_cast<std::size_t>(index)].get()
                    : nullptr;
            if (!emit_local_initializer_to_lvalue(
                    backend, context, state, element_lvalue, element_initializer)) {
                return false;
            }
        }
        return true;
    }

    if (target_type->get_kind() == SemanticTypeKind::Struct) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *struct_type =
            static_cast<const StructSemanticType *>(target_type);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        std::size_t initializer_index = 0;
        for (std::size_t field_index = 0; field_index < struct_type->get_fields().size();
             ++field_index) {
            const auto &field = struct_type->get_fields()[field_index];
            const auto field_layout =
                detail::get_aggregate_field_layout(target_type, field_index);
            if (!field_layout.has_value()) {
                continue;
            }
            const std::string field_address = backend.emit_member_address(
                lvalue.address, target_type, field_index, field.get_type());
            if (field_address.empty()) {
                return false;
            }
            LValueAddress field_lvalue{field_address, field.get_type()};
            if (field_layout->is_bit_field) {
                field_lvalue.bit_field_layout = field_layout;
            }

            const Expr *field_initializer = nullptr;
            if (!field.get_name().empty()) {
                if (initializer_index < init_list->get_elements().size()) {
                    field_initializer =
                        init_list->get_elements()[initializer_index].get();
                }
                ++initializer_index;
            }

            if (!emit_local_initializer_to_lvalue(
                    backend, context, state, field_lvalue, field_initializer)) {
                return false;
            }
        }
        return initializer_index == init_list->get_elements().size();
    }

    if (target_type->get_kind() == SemanticTypeKind::Union) {
        if (initializer->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *union_type = static_cast<const UnionSemanticType *>(target_type);
        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (init_list->get_elements().size() > 1) {
            return false;
        }
        if (!emit_zero_initialize_lvalue(backend, context, state, lvalue)) {
            return false;
        }
        if (init_list->get_elements().empty()) {
            return true;
        }

        std::size_t field_index = 0;
        while (field_index < union_type->get_fields().size() &&
               union_type->get_fields()[field_index].get_name().empty()) {
            ++field_index;
        }
        if (field_index >= union_type->get_fields().size()) {
            return false;
        }

        const auto &field = union_type->get_fields()[field_index];
        const auto field_layout =
            detail::get_aggregate_field_layout(target_type, field_index);
        if (!field_layout.has_value()) {
            return false;
        }

        const std::string field_address = backend.emit_member_address(
            lvalue.address, target_type, field_index, field.get_type());
        if (field_address.empty()) {
            return false;
        }
        LValueAddress field_lvalue{field_address, field.get_type()};
        if (field_layout->is_bit_field) {
            field_lvalue.bit_field_layout = field_layout;
        }
        return emit_local_initializer_to_lvalue(
            backend, context, state, field_lvalue,
            init_list->get_elements().front().get());
    }

    std::optional<IRValue> value =
        build_expr(backend, context, state, initializer);
    if (!value.has_value()) {
        return false;
    }
    std::optional<IRValue> coerced_value =
        coerce_ir_value(backend, *value, lvalue.type);
    if (!coerced_value.has_value()) {
        return false;
    }
    return store_lvalue_value(backend, context, lvalue, *coerced_value)
        .has_value();
}

EmissionResult emit_stmt(IRBackend &backend, const CompilerContext &context,
                         EmissionState &state, const Stmt *stmt,
                         const SemanticType *function_return_type) {
    if (stmt == nullptr) {
        return {};
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        bool block_terminated = false;
        for (const auto &child : block_stmt->get_statements()) {
            if (child != nullptr && child->get_kind() == AstKind::LabelStmt &&
                !block_terminated) {
                const auto *label_stmt = static_cast<const LabelStmt *>(child.get());
                const std::string &label_name = get_or_create_goto_label(
                    backend, state, label_stmt->get_label_name());
                backend.emit_branch(label_name);
                block_terminated = true;
            }
            if (block_terminated &&
                (child == nullptr || child->get_kind() != AstKind::LabelStmt)) {
                continue;
            }
            EmissionResult child_result =
                emit_stmt(backend, context, state, child.get(),
                          function_return_type);
            if (!child_result.success) {
                return {};
            }
            block_terminated = child_result.terminated;
        }
        return {true, block_terminated};
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (!emit_decl(backend, context, state, decl.get())) {
                return {};
            }
        }
        return {true, false};
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(stmt);
        const std::string &label_name =
            get_or_create_goto_label(backend, state, label_stmt->get_label_name());
        backend.emit_label(label_name);
        return emit_stmt(backend, context, state, label_stmt->get_body(),
                         function_return_type);
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        return {build_expr(backend, context, state,
                           expr_stmt->get_expression())
                        .has_value(),
                false};
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, if_stmt->get_condition());
        if (!condition.has_value()) {
            return {};
        }

        const std::string then_label = backend.create_label("if.then");
        const std::string end_label = backend.create_label("if.end");
        const std::string else_label =
            if_stmt->get_else_branch() != nullptr ? backend.create_label("if.else")
                                                  : end_label;

        backend.emit_cond_branch(*condition, then_label, else_label);

        backend.emit_label(then_label);
        EmissionResult then_result =
            emit_stmt(backend, context, state, if_stmt->get_then_branch(),
                      function_return_type);
        if (!then_result.success) {
            return {};
        }
        if (!then_result.terminated) {
            backend.emit_branch(end_label);
        }

        bool else_terminated = false;
        if (if_stmt->get_else_branch() != nullptr) {
            backend.emit_label(else_label);
            EmissionResult else_result =
                emit_stmt(backend, context, state, if_stmt->get_else_branch(),
                          function_return_type);
            if (!else_result.success) {
                return {};
            }
            else_terminated = else_result.terminated;
            if (!else_result.terminated) {
                backend.emit_branch(end_label);
            }
        }

        if (!then_result.terminated || !else_terminated ||
            if_stmt->get_else_branch() == nullptr) {
            backend.emit_label(end_label);
        }
        return {true, then_result.terminated &&
                          if_stmt->get_else_branch() != nullptr && else_terminated};
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        const std::string cond_label = backend.create_label("while.cond");
        const std::string body_label = backend.create_label("while.body");
        const std::string end_label = backend.create_label("while.end");

        backend.emit_branch(cond_label);
        backend.emit_label(cond_label);
        state.loop_continue_labels.push_back(cond_label);
        state.break_labels.push_back(end_label);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, while_stmt->get_condition());
        if (!condition.has_value()) {
            state.loop_continue_labels.pop_back();
            state.break_labels.pop_back();
            return {};
        }
        backend.emit_cond_branch(*condition, body_label, end_label);

        backend.emit_label(body_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, while_stmt->get_body(),
                      function_return_type);
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(cond_label);
        }

        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        const std::string body_label = backend.create_label("dowhile.body");
        const std::string cond_label = backend.create_label("dowhile.cond");
        const std::string end_label = backend.create_label("dowhile.end");

        backend.emit_branch(body_label);
        backend.emit_label(body_label);
        state.loop_continue_labels.push_back(cond_label);
        state.break_labels.push_back(end_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, do_while_stmt->get_body(),
                      function_return_type);
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(cond_label);
        }

        backend.emit_label(cond_label);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, do_while_stmt->get_condition());
        if (!condition.has_value()) {
            return {};
        }
        backend.emit_cond_branch(*condition, body_label, end_label);
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        if (for_stmt->get_init() != nullptr &&
            !build_expr(backend, context, state, for_stmt->get_init()).has_value()) {
            return {};
        }

        const std::string cond_label = backend.create_label("for.cond");
        const std::string body_label = backend.create_label("for.body");
        const std::string step_label = backend.create_label("for.step");
        const std::string end_label = backend.create_label("for.end");

        backend.emit_branch(cond_label);
        backend.emit_label(cond_label);
        if (for_stmt->get_condition() != nullptr) {
            std::optional<IRValue> condition =
                build_expr(backend, context, state, for_stmt->get_condition());
            if (!condition.has_value()) {
                return {};
            }
            backend.emit_cond_branch(*condition, body_label, end_label);
        } else {
            backend.emit_branch(body_label);
        }

        backend.emit_label(body_label);
        state.loop_continue_labels.push_back(step_label);
        state.break_labels.push_back(end_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, for_stmt->get_body(),
                      function_return_type);
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(step_label);
        }

        backend.emit_label(step_label);
        if (for_stmt->get_step() != nullptr &&
            !build_expr(backend, context, state, for_stmt->get_step()).has_value()) {
            return {};
        }
        backend.emit_branch(cond_label);
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        const auto *body = static_cast<const BlockStmt *>(switch_stmt->get_body());

        std::optional<IRValue> switch_value =
            build_expr(backend, context, state, switch_stmt->get_condition());
        if (!switch_value.has_value()) {
            return {};
        }

        struct SwitchEntry {
            const Stmt *stmt = nullptr;
            std::string label;
        };

        std::vector<SwitchEntry> entries;
        entries.reserve(body->get_statements().size());
        std::vector<const CaseStmt *> case_entries;
        std::vector<std::string> case_labels;
        case_entries.reserve(body->get_statements().size());
        case_labels.reserve(body->get_statements().size());
        const DefaultStmt *default_entry = nullptr;
        std::string default_label;
        for (const auto &child : body->get_statements()) {
            if (child->get_kind() == AstKind::CaseStmt) {
                const std::string case_label = backend.create_label("switch.case");
                entries.push_back({child.get(), case_label});
                case_entries.push_back(static_cast<const CaseStmt *>(child.get()));
                case_labels.push_back(case_label);
            } else {
                default_label = backend.create_label("switch.default");
                entries.push_back({child.get(), default_label});
                default_entry = static_cast<const DefaultStmt *>(child.get());
            }
        }

        const std::string end_label = backend.create_label("switch.end");
        if (!case_entries.empty()) {
            std::vector<std::string> test_labels;
            test_labels.reserve(case_entries.size());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                test_labels.push_back(backend.create_label("switch.test"));
            }

            backend.emit_branch(test_labels.front());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                backend.emit_label(test_labels[index]);

                const auto *case_stmt = case_entries[index];
                std::optional<long long> case_value =
                    get_integer_constant_value(context, case_stmt->get_value());
                if (!case_value.has_value()) {
                    return {};
                }

                IRValue case_ir_value =
                    backend.emit_integer_literal(static_cast<int>(*case_value));
                const SemanticType *comparison_type =
                    get_node_type(context, switch_stmt->get_condition());
                if (comparison_type == nullptr) {
                    return {};
                }

                IRValue comparison = backend.emit_binary(
                    "==", *switch_value, case_ir_value, comparison_type);
                const std::string false_label =
                    index + 1 < case_entries.size()
                        ? test_labels[index + 1]
                        : (default_entry != nullptr ? default_label : end_label);
                backend.emit_cond_branch(comparison, case_labels[index],
                                         false_label);
            }
        } else {
            backend.emit_branch(default_entry != nullptr ? default_label
                                                         : end_label);
        }

        state.break_labels.push_back(end_label);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            backend.emit_label(entries[index].label);
            const Stmt *body_stmt = nullptr;
            if (entries[index].stmt->get_kind() == AstKind::CaseStmt) {
                body_stmt =
                    static_cast<const CaseStmt *>(entries[index].stmt)->get_body();
            } else {
                body_stmt =
                    static_cast<const DefaultStmt *>(entries[index].stmt)->get_body();
            }

            EmissionResult entry_result =
                emit_stmt(backend, context, state, body_stmt,
                          function_return_type);
            if (!entry_result.success) {
                state.break_labels.pop_back();
                return {};
            }
            if (!entry_result.terminated) {
                const std::string fallthrough_label =
                    index + 1 < entries.size() ? entries[index + 1].label : end_label;
                backend.emit_branch(fallthrough_label);
            }
        }
        state.break_labels.pop_back();
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::BreakStmt:
        if (state.break_labels.empty()) {
            return {};
        }
        backend.emit_branch(state.break_labels.back());
        return {true, true};
    case AstKind::ContinueStmt:
        if (state.loop_continue_labels.empty()) {
            return {};
        }
        backend.emit_branch(state.loop_continue_labels.back());
        return {true, true};
    case AstKind::GotoStmt: {
        const auto *goto_stmt = static_cast<const GotoStmt *>(stmt);
        backend.emit_branch(
            get_or_create_goto_label(backend, state, goto_stmt->get_target_label()));
        return {true, true};
    }
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        if (return_stmt->get_value() == nullptr) {
            backend.emit_return_void();
            return {true, true};
        }

        std::optional<IRValue> return_value =
            build_expr(backend, context, state, return_stmt->get_value());
        if (!return_value.has_value()) {
            return {};
        }
        std::optional<IRValue> coerced_return =
            coerce_ir_value(backend, *return_value, function_return_type);
        if (!coerced_return.has_value()) {
            return {};
        }
        backend.emit_return(*coerced_return);
        return {true, true};
    }
    default:
        return {};
    }
}

bool emit_supported_function(IRBackend &backend, const CompilerContext &context,
                             const FunctionDecl *function_decl,
                             const std::unordered_set<std::string>
                                 &defined_function_names) {
    if (!is_supported_function(context, function_decl)) {
        return false;
    }

    const auto *function_type = dynamic_cast<const FunctionSemanticType *>(
        strip_qualifiers(get_node_type(context, function_decl)));
    std::vector<IRFunctionParameter> parameters;
    parameters.reserve(function_decl->get_parameters().size());
    for (std::size_t index = 0; index < function_decl->get_parameters().size();
         ++index) {
        const auto &parameter_decl = function_decl->get_parameters()[index];
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        const SemanticType *parameter_type = get_node_type(context, param_decl);
        if (parameter_type == nullptr && function_type != nullptr &&
            index < function_type->get_parameter_types().size()) {
            parameter_type = function_type->get_parameter_types()[index];
        }
        parameters.push_back(
            IRFunctionParameter{param_decl->get_name(), parameter_type});
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    backend.begin_function(get_function_ir_name(function_decl), return_type,
                           parameters, function_decl->get_is_variadic(),
                           collect_ir_function_attributes(context, function_decl),
                           get_function_is_internal_linkage(function_decl));

    EmissionState state;
    state.defined_function_names = &defined_function_names;
    for (std::size_t index = 0; index < function_decl->get_parameters().size();
         ++index) {
        const auto &parameter_decl = function_decl->get_parameters()[index];
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        const SemanticType *parameter_type = get_node_type(context, param_decl);
        if (parameter_type == nullptr && function_type != nullptr &&
            index < function_type->get_parameter_types().size()) {
            parameter_type = function_type->get_parameter_types()[index];
        }
        const std::string address =
            backend.emit_alloca(param_decl->get_name(), parameter_type);
        state.symbol_value_map.bind_value(param_decl, address);
        backend.emit_store(address,
                           IRValue{"%" + param_decl->get_name(), parameter_type});
    }

    const EmissionResult emitted = emit_stmt(backend, context, state,
                                             function_decl->get_body(),
                                             return_type);
    if (emitted.success && !emitted.terminated && return_type != nullptr &&
        detail::strip_qualifiers(return_type) != nullptr &&
        detail::strip_qualifiers(return_type)->get_kind() ==
            SemanticTypeKind::Builtin &&
        static_cast<const BuiltinSemanticType *>(
            detail::strip_qualifiers(return_type))
                ->get_name() == "void") {
        backend.emit_return_void();
    }
    backend.end_function();
    return emitted.success;
}

bool emit_supported_function_declaration(
    IRBackend &backend, const CompilerContext &context,
    const FunctionDecl *function_decl,
    const std::unordered_set<std::string> &defined_function_names) {
    if (function_decl == nullptr || function_decl->get_body() != nullptr) {
        return false;
    }
    if (defined_function_names.find(get_function_ir_name(function_decl)) !=
        defined_function_names.end()) {
        return false;
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    if (!is_supported_return_type(return_type)) {
        return false;
    }

    const auto *function_type = dynamic_cast<const FunctionSemanticType *>(
        strip_qualifiers(get_node_type(context, function_decl)));
    std::vector<const SemanticType *> parameter_types;
    parameter_types.reserve(function_decl->get_parameters().size());
    for (std::size_t index = 0; index < function_decl->get_parameters().size();
         ++index) {
        const auto &parameter = function_decl->get_parameters()[index];
        const SemanticType *parameter_type = get_node_type(context, parameter.get());
        if (parameter_type == nullptr && function_type != nullptr &&
            index < function_type->get_parameter_types().size()) {
            parameter_type = function_type->get_parameter_types()[index];
        }
        if (parameter_type == nullptr ||
            !is_supported_storage_type(parameter_type)) {
            return false;
        }
        parameter_types.push_back(parameter_type);
    }

    backend.declare_function(get_function_ir_name(function_decl), return_type,
                             parameter_types, function_decl->get_is_variadic(),
                             get_function_is_internal_linkage(function_decl));
    return true;
}

bool validate_ir_function_definition(CompilerContext &context,
                                     const FunctionDecl *function_decl) {
    if (function_decl == nullptr || function_decl->get_body() == nullptr) {
        return true;
    }
    if (is_supported_function(context, function_decl)) {
        return true;
    }
    if (is_system_header_decl(context, function_decl)) {
        return true;
    }

    std::string detail;
    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    if (!is_supported_return_type(return_type)) {
        detail = "unsupported return type";
    } else {
        for (const auto &parameter : function_decl->get_parameters()) {
            detail = describe_unsupported_decl(context, parameter.get());
            if (!detail.empty()) {
                break;
            }
        }
        if (detail.empty()) {
            detail = describe_unsupported_stmt(context, function_decl->get_body());
        }
    }

    context.get_diagnostic_engine().add_error(
        DiagnosticStage::Compiler,
        "ir generation does not support function definition: " +
            function_decl->get_name() +
            (detail.empty() ? std::string() : " (" + detail + ")"),
        function_decl->get_source_span());
    return false;
}

bool validate_ir_function_declaration(
    CompilerContext &context, const FunctionDecl *function_decl,
    const std::unordered_set<std::string> &defined_function_names) {
    if (function_decl == nullptr || function_decl->get_body() != nullptr) {
        return true;
    }
    if (defined_function_names.find(get_function_ir_name(function_decl)) !=
        defined_function_names.end()) {
        return true;
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    if (!is_supported_return_type(return_type)) {
        if (is_system_header_decl(context, function_decl)) {
            return true;
        }
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "ir generation does not support function declaration: " +
                function_decl->get_name(),
            function_decl->get_source_span());
        return false;
    }

    const auto *function_type = dynamic_cast<const FunctionSemanticType *>(
        strip_qualifiers(get_node_type(context, function_decl)));
    for (std::size_t index = 0; index < function_decl->get_parameters().size();
         ++index) {
        const auto &parameter = function_decl->get_parameters()[index];
        const SemanticType *parameter_type = get_node_type(context, parameter.get());
        if (parameter_type == nullptr && function_type != nullptr &&
            index < function_type->get_parameter_types().size()) {
            parameter_type = function_type->get_parameter_types()[index];
        }
        if (parameter_type == nullptr ||
            !is_supported_storage_type(parameter_type)) {
            if (is_system_header_decl(context, function_decl)) {
                return true;
            }
            context.get_diagnostic_engine().add_error(
                DiagnosticStage::Compiler,
                "ir generation does not support function declaration: " +
                    function_decl->get_name(),
                parameter->get_source_span());
            return false;
        }
    }

    return true;
}

bool validate_ir_global_var(CompilerContext &context, const VarDecl *var_decl) {
    if (var_decl == nullptr) {
        return true;
    }

    const SemanticType *type = get_node_type(context, var_decl);
    const bool supports_storage_type =
        type != nullptr && is_supported_storage_type(type);
    const bool supports_extern_incomplete_array =
        type != nullptr && var_decl->get_is_extern() &&
        var_decl->get_initializer() == nullptr &&
        is_supported_extern_incomplete_array_type(type);
    if (type == nullptr ||
        (!supports_storage_type && !supports_extern_incomplete_array)) {
        if (is_system_header_decl(context, var_decl)) {
            return true;
        }
        std::string detail = "missing semantic type";
        if (type != nullptr) {
            detail = "unsupported storage type: " +
                     detail::get_llvm_type_name(type);
        }
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "ir generation does not support global variable: " +
                var_decl->get_name() + " (" + detail + ")",
            var_decl->get_source_span());
        return false;
    }

    const bool uses_backing_storage_alias =
        uses_global_storage_alias(context, var_decl, type);
    if (var_decl->get_initializer() != nullptr && !uses_backing_storage_alias &&
        !build_global_initializer_text(context, var_decl, type).has_value()) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "ir generation does not support initializer for global variable: " +
                var_decl->get_name(),
            var_decl->get_initializer()->get_source_span());
        return false;
    }

    return true;
}

bool validate_ir_translation_unit(
    CompilerContext &context, const TranslationUnit *translation_unit,
    const std::unordered_set<std::string> &defined_function_names) {
    if (translation_unit == nullptr) {
        return true;
    }

    for (const auto &decl : translation_unit->get_top_level_decls()) {
        if (decl == nullptr) {
            continue;
        }
        if (decl->get_kind() == AstKind::VarDecl &&
            !validate_ir_global_var(context, static_cast<const VarDecl *>(decl.get()))) {
            return false;
        }
        if (decl->get_kind() == AstKind::FunctionDecl) {
            const auto *function_decl =
                static_cast<const FunctionDecl *>(decl.get());
            if (!validate_ir_function_declaration(context, function_decl,
                                                  defined_function_names) ||
                !validate_ir_function_definition(context, function_decl)) {
                return false;
            }
        }
    }

    return true;
}

std::unordered_set<std::string> collect_defined_function_names(
    const TranslationUnit *translation_unit) {
    std::unordered_set<std::string> names;
    if (translation_unit == nullptr) {
        return names;
    }
    for (const auto &decl : translation_unit->get_top_level_decls()) {
        if (decl == nullptr || decl->get_kind() != AstKind::FunctionDecl) {
            continue;
        }
        const auto *function_decl = static_cast<const FunctionDecl *>(decl.get());
        if (function_decl->get_body() == nullptr) {
            continue;
        }
        names.insert(get_function_ir_name(function_decl));
    }
    return names;
}

void collect_global_emission_infos(
    const CompilerContext &context, const TranslationUnit *translation_unit,
    std::vector<GlobalEmissionInfo> &infos) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || translation_unit == nullptr) {
        return;
    }

    std::unordered_map<const SemanticSymbol *, std::size_t> info_indexes;
    for (const auto &decl : translation_unit->get_top_level_decls()) {
        if (decl == nullptr || decl->get_kind() != AstKind::VarDecl) {
            continue;
        }
        const auto *var_decl = static_cast<const VarDecl *>(decl.get());
        const SemanticSymbol *symbol = semantic_model->get_symbol_binding(var_decl);
        const SemanticType *type = semantic_model->get_node_type(var_decl);
        if (symbol == nullptr || type == nullptr ||
            (!is_supported_storage_type(type) &&
             !(var_decl->get_is_extern() && var_decl->get_initializer() == nullptr &&
               is_supported_extern_incomplete_array_type(type)))) {
            continue;
        }

        std::size_t info_index = infos.size();
        const auto it = info_indexes.find(symbol);
        if (it == info_indexes.end()) {
            GlobalEmissionInfo info;
            info.symbol = symbol;
            info.type = type;
            info.is_internal_linkage = var_decl->get_is_static();
            infos.push_back(info);
            info_indexes.emplace(symbol, info_index);
        } else {
            info_index = it->second;
        }

        GlobalEmissionInfo &info = infos[info_index];
        info.is_internal_linkage =
            info.is_internal_linkage || var_decl->get_is_static();
        if (var_decl->get_initializer() != nullptr) {
            info.initialized_definition = var_decl;
            continue;
        }
        if (var_decl->get_is_extern()) {
            if (info.extern_decl == nullptr) {
                info.extern_decl = var_decl;
            }
            continue;
        }
        if (info.tentative_definition == nullptr) {
            info.tentative_definition = var_decl;
        }
    }
}

void emit_global_objects(IRBackend &backend, const CompilerContext &context,
                         const TranslationUnit *translation_unit) {
    std::vector<GlobalEmissionInfo> infos;
    collect_global_emission_infos(context, translation_unit, infos);
    for (const GlobalEmissionInfo &info : infos) {
        if (info.symbol == nullptr || info.type == nullptr) {
            continue;
        }
        const VarDecl *definition_decl = info.initialized_definition != nullptr
                                             ? info.initialized_definition
                                             : info.tentative_definition;
        if (definition_decl == nullptr) {
            backend.declare_global(info.symbol->get_name(), info.type,
                                   info.is_internal_linkage);
            continue;
        }
        const std::optional<std::string> initializer_text =
            build_global_initializer_text(context, definition_decl, info.type);
        if (!initializer_text.has_value()) {
            if (!uses_global_storage_alias(context, definition_decl, info.type)) {
                continue;
            }

            const std::optional<std::string> backing_initializer_text =
                build_global_backing_initializer_text_for_expr(
                    context, definition_decl->get_initializer(), info.type);
            if (!backing_initializer_text.has_value()) {
                continue;
            }

            const std::string backing_name =
                build_global_backing_storage_name(info.symbol->get_name());
            const std::string backing_type_name =
                build_global_backing_storage_llvm_type_name(info.type);

            backend.define_raw_global(backing_name, backing_type_name,
                                      *backing_initializer_text,
                                      info.is_internal_linkage,
                                      detail::get_type_alignment(info.type));
            backend.define_global_alias(info.symbol->get_name(),
                                        detail::get_llvm_type_name(info.type),
                                        backing_name, info.is_internal_linkage);
            continue;
        }
        backend.define_global(info.symbol->get_name(), info.type,
                              *initializer_text, info.is_internal_linkage);
    }
}

} // namespace

std::unique_ptr<IRResult> IRBuilder::Build(CompilerContext &context) {
    backend_.begin_module();
    const AstNode *ast_root = context.get_ast_root();
    if (ast_root != nullptr &&
        ast_root->get_kind() == AstKind::TranslationUnit) {
        const auto *translation_unit = static_cast<const TranslationUnit *>(ast_root);
        const auto defined_function_names =
            collect_defined_function_names(translation_unit);
        if (!validate_ir_translation_unit(context, translation_unit,
                                          defined_function_names)) {
            return nullptr;
        }

        emit_global_objects(backend_, context, translation_unit);
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (decl != nullptr && decl->get_kind() == AstKind::FunctionDecl) {
                emit_supported_function_declaration(
                    backend_, context,
                    static_cast<const FunctionDecl *>(decl.get()),
                    defined_function_names);
            }
        }
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (decl != nullptr && decl->get_kind() == AstKind::FunctionDecl) {
                emit_supported_function(
                    backend_, context,
                    static_cast<const FunctionDecl *>(decl.get()),
                    defined_function_names);
            }
        }
    }
    backend_.end_module();
    return std::make_unique<IRResult>(backend_.get_kind(),
                                      backend_.get_output_text());
}

} // namespace sysycc
