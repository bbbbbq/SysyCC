#include "frontend/semantic/analysis/expr_analyzer.hpp"

#include <memory>
#include <string>
#include <utility>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

const SemanticType *get_int_semantic_type(SemanticModel &semantic_model) {
    return semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int"));
}

const SemanticType *get_float_semantic_type(SemanticModel &semantic_model) {
    return semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("float"));
}

const SemanticType *get_pointer_decay_or_self(const SemanticType *type,
                                              SemanticModel &semantic_model) {
    if (type == nullptr) {
        return nullptr;
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return type;
    }
    if (type->get_kind() != SemanticTypeKind::Array) {
        return nullptr;
    }
    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    return semantic_model.own_type(
        std::make_unique<PointerSemanticType>(array_type->get_element_type()));
}

} // namespace

ExprAnalyzer::ExprAnalyzer(const TypeResolver &type_resolver,
                           const ConversionChecker &conversion_checker,
                           const ConstantEvaluator &constant_evaluator)
    : type_resolver_(type_resolver),
      conversion_checker_(conversion_checker),
      constant_evaluator_(constant_evaluator) {}

void ExprAnalyzer::add_error(SemanticContext &semantic_context,
                             std::string message,
                             const SourceSpan &source_span) const {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
}

void ExprAnalyzer::analyze_expr(const Expr *expr,
                                SemanticContext &semantic_context,
                                ScopeStack &scope_stack) const {
    if (expr == nullptr) {
        return;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();

    switch (expr->get_kind()) {
    case AstKind::IdentifierExpr: {
        const auto *identifier_expr = static_cast<const IdentifierExpr *>(expr);
        const SemanticSymbol *symbol = scope_stack.lookup(identifier_expr->get_name());
        if (symbol == nullptr) {
            add_error(semantic_context,
                      "undefined identifier: " + identifier_expr->get_name(),
                      identifier_expr->get_source_span());
            return;
        }
        semantic_model.bind_symbol(identifier_expr, symbol);
        semantic_model.bind_node_type(identifier_expr, symbol->get_type());
        if ((symbol->get_kind() == SymbolKind::Constant ||
             symbol->get_kind() == SymbolKind::Enumerator) &&
            symbol->get_decl_node() != nullptr) {
            const auto integer_constant_value =
                constant_evaluator_.get_integer_constant_value(
                    symbol->get_decl_node(), semantic_context);
            if (integer_constant_value.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    identifier_expr, *integer_constant_value, semantic_context);
            }
        }
        return;
    }
    case AstKind::IntegerLiteralExpr:
        semantic_model.bind_node_type(
            expr,
            semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int")));
        constant_evaluator_.bind_integer_constant_value(
            expr, std::stoll(static_cast<const IntegerLiteralExpr *>(expr)
                                 ->get_value_text()),
            semantic_context);
        return;
    case AstKind::FloatLiteralExpr:
        semantic_model.bind_node_type(
            expr, semantic_model.own_type(
                      std::make_unique<BuiltinSemanticType>("float")));
        return;
    case AstKind::CharLiteralExpr:
        semantic_model.bind_node_type(
            expr,
            semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int")));
        if (const auto char_constant =
                constant_evaluator_.get_integer_constant_value(expr,
                                                               semantic_context);
            char_constant.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                expr, *char_constant, semantic_context);
        }
        return;
    case AstKind::StringLiteralExpr: {
        const auto *char_type =
            semantic_model.own_type(std::make_unique<BuiltinSemanticType>("char"));
        semantic_model.bind_node_type(
            expr, semantic_model.own_type(
                      std::make_unique<PointerSemanticType>(char_type)));
        return;
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        analyze_expr(unary_expr->get_operand(), semantic_context, scope_stack);
        const SemanticType *operand_type =
            semantic_model.get_node_type(unary_expr->get_operand());
        if (operand_type == nullptr) {
            return;
        }
        if (unary_expr->get_operator_text() == "&") {
            if (!conversion_checker_.is_assignable_expr(unary_expr->get_operand())) {
                add_error(semantic_context,
                          "operator '&' requires an assignable operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr, semantic_model.own_type(
                                std::make_unique<PointerSemanticType>(
                                    operand_type)));
            return;
        }
        if (unary_expr->get_operator_text() == "*") {
            if (operand_type->get_kind() != SemanticTypeKind::Pointer) {
                add_error(semantic_context,
                          "operator '*' requires a pointer operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr,
                static_cast<const PointerSemanticType *>(operand_type)
                    ->get_pointee_type());
            return;
        }
        if (unary_expr->get_operator_text() == "!") {
            if (!conversion_checker_.is_scalar_type(operand_type)) {
                add_error(semantic_context,
                          "operator '!' requires a scalar operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(unary_expr,
                                          get_int_semantic_type(semantic_model));
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, *operand_constant == 0 ? 1 : 0,
                    semantic_context);
            }
            return;
        }
        if (unary_expr->get_operator_text() == "~") {
            if (!conversion_checker_.is_integer_like_type(operand_type)) {
                add_error(semantic_context,
                          "operator '~' requires an integer operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(unary_expr,
                                          get_int_semantic_type(semantic_model));
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, ~(*operand_constant), semantic_context);
            }
            return;
        }
        if (unary_expr->get_operator_text() == "+" ||
            unary_expr->get_operator_text() == "-") {
            if (!conversion_checker_.is_arithmetic_type(operand_type)) {
                add_error(semantic_context,
                          "operator '" + unary_expr->get_operator_text() +
                              "' requires an arithmetic operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr,
                conversion_checker_.get_usual_arithmetic_conversion_type(
                    operand_type, operand_type, semantic_model));
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value() &&
                unary_expr->get_operator_text() == "+") {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, *operand_constant, semantic_context);
            } else if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, -(*operand_constant), semantic_context);
            }
            return;
        }
        semantic_model.bind_node_type(unary_expr, operand_type);
        return;
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        analyze_expr(prefix_expr->get_operand(), semantic_context, scope_stack);
        const SemanticType *operand_type =
            semantic_model.get_node_type(prefix_expr->get_operand());
        if (!conversion_checker_.is_assignable_expr(prefix_expr->get_operand())) {
            add_error(semantic_context,
                      "prefix operator '" + prefix_expr->get_operator_text() +
                          "' requires an assignable operand",
                      prefix_expr->get_source_span());
        }
        if (operand_type != nullptr &&
            !conversion_checker_.is_incrementable_type(operand_type)) {
            add_error(semantic_context,
                      "prefix operator '" + prefix_expr->get_operator_text() +
                          "' requires an incrementable operand",
                      prefix_expr->get_source_span());
        }
        semantic_model.bind_node_type(prefix_expr, operand_type);
        return;
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        analyze_expr(postfix_expr->get_operand(), semantic_context, scope_stack);
        const SemanticType *operand_type =
            semantic_model.get_node_type(postfix_expr->get_operand());
        if (!conversion_checker_.is_assignable_expr(postfix_expr->get_operand())) {
            add_error(semantic_context,
                      "postfix operator '" + postfix_expr->get_operator_text() +
                          "' requires an assignable operand",
                      postfix_expr->get_source_span());
        }
        if (operand_type != nullptr &&
            !conversion_checker_.is_incrementable_type(operand_type)) {
            add_error(semantic_context,
                      "postfix operator '" + postfix_expr->get_operator_text() +
                          "' requires an incrementable operand",
                      postfix_expr->get_source_span());
        }
        semantic_model.bind_node_type(postfix_expr, operand_type);
        return;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        analyze_expr(binary_expr->get_lhs(), semantic_context, scope_stack);
        analyze_expr(binary_expr->get_rhs(), semantic_context, scope_stack);
        const SemanticType *lhs_type =
            semantic_model.get_node_type(binary_expr->get_lhs());
        const SemanticType *rhs_type =
            semantic_model.get_node_type(binary_expr->get_rhs());
        const auto lhs_constant = constant_evaluator_.get_integer_constant_value(
            binary_expr->get_lhs(), semantic_context);
        const auto rhs_constant = constant_evaluator_.get_integer_constant_value(
            binary_expr->get_rhs(), semantic_context);
        if (lhs_type == nullptr || rhs_type == nullptr) {
            return;
        }

        const std::string &operator_text = binary_expr->get_operator_text();
        const SemanticType *lhs_pointer_like =
            get_pointer_decay_or_self(lhs_type, semantic_model);
        const SemanticType *rhs_pointer_like =
            get_pointer_decay_or_self(rhs_type, semantic_model);

        if (operator_text == "+" || operator_text == "-" ||
            operator_text == "*" || operator_text == "/") {
            if ((operator_text == "+" || operator_text == "-") &&
                lhs_pointer_like != nullptr &&
                conversion_checker_.is_integer_like_type(rhs_type)) {
                semantic_model.bind_node_type(binary_expr, lhs_pointer_like);
                return;
            }
            if (operator_text == "+" &&
                conversion_checker_.is_integer_like_type(lhs_type) &&
                rhs_pointer_like != nullptr) {
                semantic_model.bind_node_type(binary_expr, rhs_pointer_like);
                return;
            }
            if (operator_text == "-" && lhs_pointer_like != nullptr &&
                rhs_pointer_like != nullptr &&
                conversion_checker_.is_same_type(lhs_pointer_like,
                                                 rhs_pointer_like)) {
                semantic_model.bind_node_type(binary_expr,
                                              get_int_semantic_type(semantic_model));
                return;
            }
            if (!conversion_checker_.is_arithmetic_type(lhs_type) ||
                !conversion_checker_.is_arithmetic_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires arithmetic operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                binary_expr,
                conversion_checker_.get_usual_arithmetic_conversion_type(
                    lhs_type, rhs_type, semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "+") {
                    result = *lhs_constant + *rhs_constant;
                } else if (operator_text == "-") {
                    result = *lhs_constant - *rhs_constant;
                } else if (operator_text == "*") {
                    result = *lhs_constant * *rhs_constant;
                } else if (*rhs_constant != 0) {
                    result = *lhs_constant / *rhs_constant;
                } else {
                    return;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "%") {
            if (!conversion_checker_.is_integer_like_type(lhs_type) ||
                !conversion_checker_.is_integer_like_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '%' requires integer operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value() &&
                *rhs_constant != 0) {
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, *lhs_constant % *rhs_constant, semantic_context);
            }
            return;
        }

        if (operator_text == "<<" || operator_text == ">>" ||
            operator_text == "&" || operator_text == "|" ||
            operator_text == "^") {
            if (!conversion_checker_.is_integer_like_type(lhs_type) ||
                !conversion_checker_.is_integer_like_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires integer operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "<<") {
                    result = *lhs_constant << *rhs_constant;
                } else if (operator_text == ">>") {
                    result = *lhs_constant >> *rhs_constant;
                } else if (operator_text == "&") {
                    result = *lhs_constant & *rhs_constant;
                } else if (operator_text == "|") {
                    result = *lhs_constant | *rhs_constant;
                } else {
                    result = *lhs_constant ^ *rhs_constant;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "&&" || operator_text == "||") {
            if (!conversion_checker_.is_scalar_type(lhs_type) ||
                !conversion_checker_.is_scalar_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires scalar operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                const long long result =
                    operator_text == "&&"
                        ? ((*lhs_constant != 0) && (*rhs_constant != 0))
                        : ((*lhs_constant != 0) || (*rhs_constant != 0));
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "<" || operator_text == "<=" ||
            operator_text == ">" || operator_text == ">=") {
            if (!conversion_checker_.is_arithmetic_type(lhs_type) ||
                !conversion_checker_.is_arithmetic_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires arithmetic operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "<") {
                    result = *lhs_constant < *rhs_constant;
                } else if (operator_text == "<=") {
                    result = *lhs_constant <= *rhs_constant;
                } else if (operator_text == ">") {
                    result = *lhs_constant > *rhs_constant;
                } else {
                    result = *lhs_constant >= *rhs_constant;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "==" || operator_text == "!=") {
            if (!conversion_checker_.is_compatible_equality_type(
                    lhs_type, rhs_type, binary_expr->get_lhs(),
                    binary_expr->get_rhs(), semantic_context,
                    constant_evaluator_)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires compatible operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                const long long result =
                    operator_text == "==" ? (*lhs_constant == *rhs_constant)
                                          : (*lhs_constant != *rhs_constant);
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        semantic_model.bind_node_type(binary_expr, lhs_type);
        return;
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        analyze_expr(assign_expr->get_target(), semantic_context, scope_stack);
        analyze_expr(assign_expr->get_value(), semantic_context, scope_stack);
        if (!conversion_checker_.is_assignable_expr(assign_expr->get_target())) {
            add_error(semantic_context, "assignment target is not assignable",
                      assign_expr->get_target()->get_source_span());
        }
        const SemanticType *target_type =
            semantic_model.get_node_type(assign_expr->get_target());
        const SemanticType *value_type =
            semantic_model.get_node_type(assign_expr->get_value());
        if (target_type != nullptr && value_type != nullptr &&
            !conversion_checker_.is_assignable_value(
                target_type, value_type, assign_expr->get_value(),
                semantic_context, constant_evaluator_)) {
            add_error(semantic_context,
                      "assignment value type does not match target type",
                      assign_expr->get_source_span());
        }
        semantic_model.bind_node_type(assign_expr, target_type);
        return;
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        analyze_expr(call_expr->get_callee(), semantic_context, scope_stack);
        for (const auto &argument : call_expr->get_arguments()) {
            analyze_expr(argument.get(), semantic_context, scope_stack);
        }

        const SemanticSymbol *callee_symbol =
            semantic_model.get_symbol_binding(call_expr->get_callee());
        const SemanticType *callee_type = nullptr;
        if (callee_symbol != nullptr) {
            callee_type = callee_symbol->get_type();
        } else {
            callee_type = semantic_model.get_node_type(call_expr->get_callee());
        }
        if (callee_type == nullptr) {
            return;
        }

        if (callee_type->get_kind() != SemanticTypeKind::Function) {
            add_error(semantic_context, "called object is not a function",
                      call_expr->get_source_span());
            return;
        }

        const auto *function_type = static_cast<const FunctionSemanticType *>(
            callee_type);
        if (function_type->get_parameter_types().size() !=
            call_expr->get_arguments().size()) {
            add_error(semantic_context,
                      "function call argument count does not match declaration",
                      call_expr->get_source_span());
        } else {
            for (std::size_t index = 0; index < call_expr->get_arguments().size();
                 ++index) {
                const SemanticType *argument_type =
                    semantic_model.get_node_type(call_expr->get_arguments()[index].get());
                const SemanticType *parameter_type =
                    function_type->get_parameter_types()[index];
                if (argument_type != nullptr && parameter_type != nullptr &&
                    !conversion_checker_.is_assignable_value(
                        parameter_type, argument_type,
                        call_expr->get_arguments()[index].get(), semantic_context,
                        constant_evaluator_)) {
                    add_error(semantic_context,
                              "function call argument type does not match declaration",
                              call_expr->get_arguments()[index]->get_source_span());
                    break;
                }
            }
        }
        semantic_model.bind_node_type(call_expr, function_type->get_return_type());
        return;
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        analyze_expr(index_expr->get_base(), semantic_context, scope_stack);
        analyze_expr(index_expr->get_index(), semantic_context, scope_stack);
        const SemanticType *base_type =
            semantic_model.get_node_type(index_expr->get_base());
        const SemanticType *index_type =
            semantic_model.get_node_type(index_expr->get_index());
        if (base_type == nullptr) {
            return;
        }
        if (base_type->get_kind() != SemanticTypeKind::Pointer &&
            base_type->get_kind() != SemanticTypeKind::Array) {
            add_error(semantic_context,
                      "subscripted object is not an indexable pointer",
                      index_expr->get_base()->get_source_span());
            return;
        }
        if (!conversion_checker_.is_integer_like_type(index_type)) {
            add_error(semantic_context,
                      "array subscript must have integer type",
                      index_expr->get_index()->get_source_span());
            return;
        }
        if (base_type->get_kind() == SemanticTypeKind::Pointer) {
            const auto *pointer_type =
                static_cast<const PointerSemanticType *>(base_type);
            semantic_model.bind_node_type(index_expr,
                                          pointer_type->get_pointee_type());
            return;
        }
        const auto *array_type = static_cast<const ArraySemanticType *>(base_type);
        semantic_model.bind_node_type(index_expr, array_type->get_element_type());
        return;
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        analyze_expr(member_expr->get_base(), semantic_context, scope_stack);
        const SemanticType *base_type =
            semantic_model.get_node_type(member_expr->get_base());

        const SemanticType *struct_type = nullptr;
        if (member_expr->get_operator_text() == "->") {
            if (!conversion_checker_.is_pointer_to_struct_type(base_type)) {
                add_error(semantic_context,
                          "operator '->' requires a pointer-to-struct operand",
                          member_expr->get_source_span());
                return;
            }
            struct_type =
                static_cast<const PointerSemanticType *>(base_type)
                    ->get_pointee_type();
        } else if (member_expr->get_operator_text() == ".") {
            if (!conversion_checker_.is_struct_type(base_type)) {
                add_error(semantic_context,
                          "operator '.' requires a struct operand",
                          member_expr->get_source_span());
                return;
            }
            struct_type = base_type;
        }

        if (struct_type != nullptr &&
            struct_type->get_kind() == SemanticTypeKind::Struct) {
            const std::string &struct_name =
                static_cast<const StructSemanticType *>(struct_type)->get_name();
            const SemanticSymbol *struct_symbol = scope_stack.lookup(struct_name);
            if (struct_symbol != nullptr &&
                struct_symbol->get_kind() == SymbolKind::StructName &&
                struct_symbol->get_decl_node() != nullptr &&
                struct_symbol->get_decl_node()->get_kind() == AstKind::StructDecl) {
                const auto *struct_decl =
                    static_cast<const StructDecl *>(struct_symbol->get_decl_node());
                for (const auto &field : struct_decl->get_fields()) {
                    const auto *field_decl =
                        static_cast<const FieldDecl *>(field.get());
                    if (field_decl->get_name() == member_expr->get_member_name()) {
                        semantic_model.bind_node_type(
                            expr, type_resolver_.resolve_type(
                                      field_decl->get_declared_type(),
                                      semantic_context));
                        return;
                    }
                }
                add_error(semantic_context,
                          "member '" + member_expr->get_member_name() +
                              "' does not exist in struct '" + struct_name + "'",
                          member_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(expr, struct_type);
        }
        return;
    }
    case AstKind::InitListExpr: {
        const auto *init_list_expr = static_cast<const InitListExpr *>(expr);
        for (const auto &element : init_list_expr->get_elements()) {
            analyze_expr(element.get(), semantic_context, scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

} // namespace sysycc::detail
