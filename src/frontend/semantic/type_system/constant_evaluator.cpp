#include "frontend/semantic/type_system/constant_evaluator.hpp"

#include <optional>
#include <string>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_model.hpp"

namespace sysycc::detail {

std::optional<long long> ConstantEvaluator::get_integer_constant_value(
    const AstNode *node, const SemanticContext &semantic_context) const {
    const auto stored =
        semantic_context.get_semantic_model().get_integer_constant_value(node);
    if (stored.has_value()) {
        return stored;
    }
    const auto *expr = dynamic_cast<const Expr *>(node);
    if (expr == nullptr) {
        return std::nullopt;
    }
    return evaluate_integer_expr(expr, semantic_context);
}

void ConstantEvaluator::bind_integer_constant_value(
    const AstNode *node, long long value,
    SemanticContext &semantic_context) const {
    semantic_context.get_semantic_model().bind_integer_constant_value(node, value);
}

bool ConstantEvaluator::is_integer_constant_expr(
    const Expr *expr, const SemanticContext &semantic_context,
    const ConversionChecker &conversion_checker) const {
    if (expr == nullptr) {
        return false;
    }
    const SemanticModel &semantic_model = semantic_context.get_semantic_model();
    const SemanticType *type = semantic_model.get_node_type(expr);
    if (!conversion_checker.is_integer_like_type(type)) {
        return false;
    }
    return get_integer_constant_value(expr, semantic_context).has_value();
}

std::optional<long long> ConstantEvaluator::evaluate_integer_expr(
    const Expr *expr, const SemanticContext &semantic_context) const {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return std::stoll(
            static_cast<const IntegerLiteralExpr *>(expr)->get_value_text());
    case AstKind::CharLiteralExpr:
        return parse_char_literal(
            static_cast<const CharLiteralExpr *>(expr)->get_value_text());
    case AstKind::IdentifierExpr:
        return semantic_context.get_semantic_model().get_integer_constant_value(
            expr);
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        const auto operand =
            evaluate_integer_expr(unary_expr->get_operand(), semantic_context);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        const auto &op = unary_expr->get_operator_text();
        if (op == "+") {
            return *operand;
        }
        if (op == "-") {
            return -(*operand);
        }
        if (op == "!") {
            return *operand == 0 ? 1LL : 0LL;
        }
        if (op == "~") {
            return ~(*operand);
        }
        return std::nullopt;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const auto lhs =
            evaluate_integer_expr(binary_expr->get_lhs(), semantic_context);
        const auto rhs =
            evaluate_integer_expr(binary_expr->get_rhs(), semantic_context);
        if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
        }
        const auto &op = binary_expr->get_operator_text();
        if (op == "+") {
            return *lhs + *rhs;
        }
        if (op == "-") {
            return *lhs - *rhs;
        }
        if (op == "*") {
            return *lhs * *rhs;
        }
        if (op == "/") {
            return *rhs == 0 ? std::nullopt
                             : std::optional<long long>(*lhs / *rhs);
        }
        if (op == "%") {
            return *rhs == 0 ? std::nullopt
                             : std::optional<long long>(*lhs % *rhs);
        }
        if (op == "<<") {
            return *lhs << *rhs;
        }
        if (op == ">>") {
            return *lhs >> *rhs;
        }
        if (op == "&") {
            return *lhs & *rhs;
        }
        if (op == "|") {
            return *lhs | *rhs;
        }
        if (op == "^") {
            return *lhs ^ *rhs;
        }
        if (op == "&&") {
            return (*lhs != 0 && *rhs != 0) ? 1LL : 0LL;
        }
        if (op == "||") {
            return (*lhs != 0 || *rhs != 0) ? 1LL : 0LL;
        }
        if (op == "<") {
            return *lhs < *rhs ? 1LL : 0LL;
        }
        if (op == "<=") {
            return *lhs <= *rhs ? 1LL : 0LL;
        }
        if (op == ">") {
            return *lhs > *rhs ? 1LL : 0LL;
        }
        if (op == ">=") {
            return *lhs >= *rhs ? 1LL : 0LL;
        }
        if (op == "==") {
            return *lhs == *rhs ? 1LL : 0LL;
        }
        if (op == "!=") {
            return *lhs != *rhs ? 1LL : 0LL;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

std::optional<long long>
ConstantEvaluator::parse_char_literal(const std::string &value_text) const {
    if (value_text.size() < 3 || value_text.front() != '\'' ||
        value_text.back() != '\'') {
        return std::nullopt;
    }
    if (value_text[1] != '\\') {
        return static_cast<long long>(
            static_cast<unsigned char>(value_text[1]));
    }
    if (value_text.size() < 4) {
        return std::nullopt;
    }
    switch (value_text[2]) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        return static_cast<long long>(
            static_cast<unsigned char>(value_text[2]));
    }
}

} // namespace sysycc::detail
