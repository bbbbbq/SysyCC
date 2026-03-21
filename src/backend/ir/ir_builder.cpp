#include "backend/ir/ir_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/detail/symbol_value_map.hpp"
#include "backend/ir/gnu_function_attribute_lowering_handler.hpp"
#include "backend/ir/ir_backend.hpp"
#include "backend/ir/ir_result.hpp"
#include "common/integer_literal.hpp"
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
           is_builtin_type_named(type, "ptrdiff_t");
}

bool is_supported_scalar_storage_type(const SemanticType *type) {
    return is_builtin_type_named(type, "int") ||
           is_builtin_type_named(type, "ptrdiff_t") ||
           is_builtin_type_named(type, "unsigned int") ||
           is_builtin_type_named(type, "char") ||
           is_builtin_type_named(type, "signed char") ||
           is_builtin_type_named(type, "unsigned char") ||
           is_builtin_type_named(type, "short") ||
           is_builtin_type_named(type, "unsigned short") ||
           is_builtin_type_named(type, "long int") ||
           is_builtin_type_named(type, "long long int") ||
           is_builtin_type_named(type, "unsigned long long") ||
           is_builtin_type_named(type, "float") ||
           is_builtin_type_named(type, "double") ||
           strip_qualifiers(type) != nullptr &&
               strip_qualifiers(type)->get_kind() == SemanticTypeKind::Pointer;
}

bool is_supported_aggregate_storage_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        for (const auto &field : struct_type->get_fields()) {
            if (!is_supported_scalar_storage_type(field.get_type())) {
                return false;
            }
        }
        return true;
    }
    if (type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        for (const auto &field : union_type->get_fields()) {
            if (!is_supported_scalar_storage_type(field.get_type())) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_supported_storage_type(const SemanticType *type) {
    return is_supported_scalar_storage_type(type) ||
           is_supported_aggregate_storage_type(type);
}

bool is_supported_return_type(const SemanticType *type) {
    return is_builtin_type_named(type, "void") ||
           is_supported_scalar_storage_type(type);
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

bool is_supported_logical_operator(const std::string &op) {
    return op == "&&" || op == "||";
}

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type);

const SemanticType *get_usual_arithmetic_conversion_type(
    const CompilerContext &context, const SemanticType *lhs_type,
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
    return conversion_checker.get_usual_arithmetic_conversion_type(
        lhs_type, rhs_type, const_cast<SemanticModel &>(*semantic_model));
}

std::optional<IRValue> coerce_binary_operand(IRBackend &backend,
                                             const IRValue &value,
                                             const SemanticType *target_type) {
    if (target_type == nullptr) {
        return std::nullopt;
    }
    return coerce_ir_value(backend, value, target_type);
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
        if (builtin_type->get_name() == "double") {
            static BuiltinSemanticType double_type("double");
            return &double_type;
        }
    }

    return nullptr;
}

bool is_supported_type_for_storage(const CompilerContext &context,
                                   const AstNode *node) {
    return is_supported_storage_type(get_node_type(context, node));
}

bool is_supported_expr(const CompilerContext &context, const Expr *expr);

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
};

struct GlobalEmissionInfo {
    const SemanticSymbol *symbol = nullptr;
    const SemanticType *type = nullptr;
    const VarDecl *extern_decl = nullptr;
    const VarDecl *tentative_definition = nullptr;
    const VarDecl *initialized_definition = nullptr;
};

std::optional<IRValue> build_expr(IRBackend &backend,
                                  const CompilerContext &context,
                                  EmissionState &state, const Expr *expr);

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type);

std::optional<LValueAddress> build_lvalue_address(
    IRBackend &backend, const CompilerContext &context, EmissionState &state,
    const Expr *expr);

std::optional<std::string> build_global_initializer_text(
    const CompilerContext &context, const VarDecl *var_decl,
    const SemanticType *target_type);

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

    const Expr *initializer = var_decl->get_initializer();
    if (initializer == nullptr) {
        return std::string("zeroinitializer");
    }

    const SemanticType *unqualified_target = strip_qualifiers(target_type);
    if (unqualified_target == nullptr) {
        return std::nullopt;
    }

    if (unqualified_target->get_kind() == SemanticTypeKind::Pointer) {
        const auto integer_constant = get_integer_constant_value(context, initializer);
        if (integer_constant.has_value() && *integer_constant == 0) {
            return std::string("null");
        }
        return std::nullopt;
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

bool is_supported_decl(const CompilerContext &context, const Decl *decl) {
    if (decl == nullptr) {
        return false;
    }

    if (decl->get_kind() == AstKind::VarDecl) {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        if (!var_decl->get_dimensions().empty() ||
            !is_supported_type_for_storage(context, var_decl)) {
            return false;
        }
        const Expr *initializer = var_decl->get_initializer();
        return initializer == nullptr || is_supported_expr(context, initializer);
    }

    if (decl->get_kind() == AstKind::ParamDecl) {
        const auto *param_decl = static_cast<const ParamDecl *>(decl);
        return param_decl->get_dimensions().empty() &&
               is_supported_type_for_storage(context, param_decl);
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
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
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
        return (is_supported_arithmetic_operator(binary_expr->get_operator_text()) ||
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
        if (assign_expr->get_target()->get_kind() != AstKind::IdentifierExpr &&
            assign_expr->get_target()->get_kind() != AstKind::MemberExpr) {
            return false;
        }
        return is_supported_scalar_storage_type(get_node_type(context, assign_expr)) &&
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
        if (!is_supported_scalar_storage_type(field_type)) {
            return false;
        }
        if (member_expr->get_operator_text() == "->") {
            return is_supported_expr(context, member_expr->get_base());
        }
        return member_expr->get_base() != nullptr &&
               (member_expr->get_base()->get_kind() == AstKind::IdentifierExpr ||
                member_expr->get_base()->get_kind() == AstKind::MemberExpr);
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&") {
            return unary_expr->get_operand() != nullptr &&
                   (unary_expr->get_operand()->get_kind() == AstKind::IdentifierExpr ||
                    unary_expr->get_operand()->get_kind() == AstKind::MemberExpr);
        }
        if (unary_expr->get_operator_text() == "*") {
            return is_supported_expr(context, unary_expr->get_operand()) &&
                   is_supported_scalar_storage_type(get_node_type(context, expr));
        }
        return false;
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        return is_supported_expr(context, index_expr->get_base()) &&
               is_supported_expr(context, index_expr->get_index()) &&
               is_supported_scalar_storage_type(get_node_type(context, expr));
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
        if (parameter_types.size() != call_expr->get_arguments().size()) {
            return false;
        }

        for (std::size_t index = 0; index < call_expr->get_arguments().size();
             ++index) {
            if (!is_supported_scalar_storage_type(parameter_types[index]) ||
                !is_supported_expr(context,
                                   call_expr->get_arguments()[index].get())) {
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
                backend.declare_global(symbol->get_name(), symbol->get_type());
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
                backend.declare_global(symbol->get_name(), symbol->get_type());
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
        return LValueAddress{member_address, field_type};
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

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr: {
        const auto *integer_literal =
            static_cast<const IntegerLiteralExpr *>(expr);
        const auto parsed_value =
            parse_integer_literal(integer_literal->get_value_text());
        if (!parsed_value.has_value() ||
            *parsed_value < std::numeric_limits<int>::min() ||
            *parsed_value > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return backend.emit_integer_literal(static_cast<int>(*parsed_value));
    }
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return std::nullopt;
        }

        if (symbol->get_kind() == SymbolKind::Function) {
            return IRValue{"@" + symbol->get_name(), symbol->get_type()};
        }

        const AstNode *decl_node = symbol->get_decl_node();
        if (decl_node == nullptr) {
            const VariableSemanticInfo *variable_info =
                get_variable_info(context, symbol);
            if (variable_info != nullptr && variable_info->get_is_global_storage()) {
                backend.declare_global(symbol->get_name(), symbol->get_type());
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
                backend.declare_global(symbol->get_name(), symbol->get_type());
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
        return backend.emit_load(member_address->address, member_address->type);
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
            return backend.emit_load(address->address, address->type);
        }
        return std::nullopt;
    }
    case AstKind::IndexExpr: {
        std::optional<LValueAddress> element_address =
            build_lvalue_address(backend, context, state, expr);
        if (!element_address.has_value() || element_address->type == nullptr) {
            return std::nullopt;
        }
        return backend.emit_load(element_address->address, element_address->type);
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const SemanticType *result_type = get_node_type(context, expr);
        if (result_type == nullptr) {
            return std::nullopt;
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
                        context, lhs_type, rhs_type);
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

        std::optional<IRValue> coerced_value =
            coerce_ir_value(backend, *value, target_address->type);
        if (!coerced_value.has_value()) {
            return std::nullopt;
        }

        backend.emit_store(target_address->address, *coerced_value);
        return coerced_value;
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
        if (callee_symbol->get_decl_node() == nullptr) {
            backend.declare_function(callee_symbol->get_name(),
                                     function_type->get_return_type(),
                                     function_type->get_parameter_types());
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
            std::optional<IRValue> coerced_argument = coerce_ir_value(
                backend, *lowered_argument,
                function_type->get_parameter_types()[index]);
            if (!coerced_argument.has_value()) {
                return std::nullopt;
            }
            arguments.push_back(*coerced_argument);
        }

        return backend.emit_call(callee->text, arguments, return_type);
    }
    default:
        return std::nullopt;
    }
}

bool emit_decl(IRBackend &backend, const CompilerContext &context,
               EmissionState &state, const Decl *decl) {
    if (decl == nullptr || decl->get_kind() != AstKind::VarDecl) {
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
        std::optional<IRValue> initializer =
            build_expr(backend, context, state,
                       var_decl->get_initializer());
        if (!initializer.has_value()) {
            return false;
        }
        std::optional<IRValue> coerced_initializer =
            coerce_ir_value(backend, *initializer, declared_type);
        if (!coerced_initializer.has_value()) {
            return false;
        }
        backend.emit_store(address, *coerced_initializer);
    }

    return true;
}

std::optional<IRValue> coerce_ir_value(IRBackend &backend, const IRValue &value,
                                       const SemanticType *target_type) {
    if (target_type == nullptr || value.type == nullptr) {
        return std::nullopt;
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

EmissionResult emit_stmt(IRBackend &backend, const CompilerContext &context,
                         EmissionState &state, const Stmt *stmt,
                         const SemanticType *function_return_type) {
    if (stmt == nullptr) {
        return {};
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            EmissionResult child_result =
                emit_stmt(backend, context, state, child.get(),
                          function_return_type);
            if (!child_result.success) {
                return {};
            }
            if (child_result.terminated) {
                return {true, true};
            }
        }
        return {true, false};
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
                             const FunctionDecl *function_decl) {
    if (!is_supported_function(context, function_decl)) {
        return false;
    }

    std::vector<IRFunctionParameter> parameters;
    parameters.reserve(function_decl->get_parameters().size());
    for (const auto &parameter_decl : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        parameters.push_back(
            IRFunctionParameter{param_decl->get_name(),
                                get_node_type(context, param_decl)});
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    backend.begin_function(function_decl->get_name(), return_type, parameters,
                           collect_ir_function_attributes(context, function_decl));

    EmissionState state;
    for (const auto &parameter_decl : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        const SemanticType *parameter_type = get_node_type(context, param_decl);
        const std::string address =
            backend.emit_alloca(param_decl->get_name(), parameter_type);
        state.symbol_value_map.bind_value(param_decl, address);
        backend.emit_store(address,
                           IRValue{"%" + param_decl->get_name(), parameter_type});
    }

    const EmissionResult emitted = emit_stmt(backend, context, state,
                                             function_decl->get_body(),
                                             return_type);
    backend.end_function();
    return emitted.success;
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
            !is_supported_storage_type(type)) {
            continue;
        }

        std::size_t info_index = infos.size();
        const auto it = info_indexes.find(symbol);
        if (it == info_indexes.end()) {
            GlobalEmissionInfo info;
            info.symbol = symbol;
            info.type = type;
            infos.push_back(info);
            info_indexes.emplace(symbol, info_index);
        } else {
            info_index = it->second;
        }

        GlobalEmissionInfo &info = infos[info_index];
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
            backend.declare_global(info.symbol->get_name(), info.type);
            continue;
        }
        const std::optional<std::string> initializer_text =
            build_global_initializer_text(context, definition_decl, info.type);
        if (!initializer_text.has_value()) {
            continue;
        }
        backend.define_global(info.symbol->get_name(), info.type,
                              *initializer_text);
    }
}

} // namespace

std::unique_ptr<IRResult> IRBuilder::Build(const CompilerContext &context) {
    backend_.begin_module();
    const AstNode *ast_root = context.get_ast_root();
    if (ast_root != nullptr &&
        ast_root->get_kind() == AstKind::TranslationUnit) {
        const auto *translation_unit = static_cast<const TranslationUnit *>(ast_root);
        emit_global_objects(backend_, context, translation_unit);
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (decl != nullptr && decl->get_kind() == AstKind::FunctionDecl) {
                emit_supported_function(
                    backend_, context,
                    static_cast<const FunctionDecl *>(decl.get()));
            }
        }
    }
    backend_.end_module();
    return std::make_unique<IRResult>(backend_.get_kind(),
                                      backend_.get_output_text());
}

} // namespace sysycc
