#include "frontend/semantic/type_system/constant_evaluator.hpp"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

#include "common/integer_literal.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/support/semantic_context.hpp"

namespace sysycc::detail {

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

} // namespace

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

std::optional<long long> ConstantEvaluator::get_scalar_constant_value_as_integer(
    const Expr *expr, const SemanticType *target_type,
    const SemanticContext &semantic_context) const {
    return get_scalar_constant_value_as_integer(
        expr, target_type, semantic_context.get_semantic_model());
}

std::optional<long long> ConstantEvaluator::get_scalar_constant_value_as_integer(
    const Expr *expr, const SemanticType *target_type,
    const SemanticModel &semantic_model) const {
    const auto value = evaluate_scalar_numeric_expr(expr, semantic_model);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return convert_scalar_numeric_value_to_integer(*value, target_type);
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
        return parse_integer_literal(
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

std::optional<long double> ConstantEvaluator::evaluate_scalar_numeric_expr(
    const Expr *expr, const SemanticContext &semantic_context) const {
    return evaluate_scalar_numeric_expr(expr,
                                        semantic_context.get_semantic_model());
}

std::optional<long double> ConstantEvaluator::evaluate_scalar_numeric_expr(
    const Expr *expr, const SemanticModel &semantic_model) const {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr: {
        const auto value = parse_integer_literal(
            static_cast<const IntegerLiteralExpr *>(expr)->get_value_text());
        return value.has_value() ? std::optional<long double>(*value) : std::nullopt;
    }
    case AstKind::FloatLiteralExpr: {
        const std::string &text =
            static_cast<const FloatLiteralExpr *>(expr)->get_value_text();
        char *end = nullptr;
        const long double value = std::strtold(text.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            return std::nullopt;
        }
        return value;
    }
    case AstKind::CharLiteralExpr: {
        const auto value = parse_char_literal(
            static_cast<const CharLiteralExpr *>(expr)->get_value_text());
        return value.has_value() ? std::optional<long double>(*value) : std::nullopt;
    }
    case AstKind::IdentifierExpr: {
        const auto value =
            semantic_model.get_integer_constant_value(expr);
        return value.has_value() ? std::optional<long double>(*value) : std::nullopt;
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        const auto operand =
            evaluate_scalar_numeric_expr(unary_expr->get_operand(), semantic_model);
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
            return *operand == 0 ? 1.0L : 0.0L;
        }
        return std::nullopt;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const auto lhs =
            evaluate_scalar_numeric_expr(binary_expr->get_lhs(), semantic_model);
        const auto rhs =
            evaluate_scalar_numeric_expr(binary_expr->get_rhs(), semantic_model);
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
                             : std::optional<long double>(*lhs / *rhs);
        }
        if (op == "%") {
            const long long lhs_int = static_cast<long long>(*lhs);
            const long long rhs_int = static_cast<long long>(*rhs);
            if (rhs_int == 0) {
                return std::nullopt;
            }
            return static_cast<long double>(lhs_int % rhs_int);
        }
        if (op == "<<") {
            return static_cast<long double>(
                static_cast<long long>(*lhs) << static_cast<long long>(*rhs));
        }
        if (op == ">>") {
            return static_cast<long double>(
                static_cast<long long>(*lhs) >> static_cast<long long>(*rhs));
        }
        if (op == "&") {
            return static_cast<long double>(
                static_cast<long long>(*lhs) & static_cast<long long>(*rhs));
        }
        if (op == "|") {
            return static_cast<long double>(
                static_cast<long long>(*lhs) | static_cast<long long>(*rhs));
        }
        if (op == "^") {
            return static_cast<long double>(
                static_cast<long long>(*lhs) ^ static_cast<long long>(*rhs));
        }
        if (op == "&&") {
            return (*lhs != 0 && *rhs != 0) ? 1.0L : 0.0L;
        }
        if (op == "||") {
            return (*lhs != 0 || *rhs != 0) ? 1.0L : 0.0L;
        }
        if (op == "<") {
            return *lhs < *rhs ? 1.0L : 0.0L;
        }
        if (op == "<=") {
            return *lhs <= *rhs ? 1.0L : 0.0L;
        }
        if (op == ">") {
            return *lhs > *rhs ? 1.0L : 0.0L;
        }
        if (op == ">=") {
            return *lhs >= *rhs ? 1.0L : 0.0L;
        }
        if (op == "==") {
            return *lhs == *rhs ? 1.0L : 0.0L;
        }
        if (op == "!=") {
            return *lhs != *rhs ? 1.0L : 0.0L;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

std::optional<long long>
ConstantEvaluator::convert_scalar_numeric_value_to_integer(
    long double value, const SemanticType *target_type) const {
    if (value > static_cast<long double>(std::numeric_limits<long long>::max()) ||
        value < static_cast<long double>(std::numeric_limits<long long>::min())) {
        return std::nullopt;
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto type_info =
        integer_conversion_service.get_integer_type_info(
            strip_qualifiers(target_type));
    if (!type_info.has_value()) {
        return static_cast<long long>(value);
    }

    std::uint64_t bits =
        static_cast<std::uint64_t>(static_cast<long long>(value));
    if (type_info->get_bit_width() > 0 && type_info->get_bit_width() < 64) {
        const std::uint64_t mask =
            (std::uint64_t{1} << type_info->get_bit_width()) - 1;
        bits &= mask;
        if (type_info->get_is_signed()) {
            const std::uint64_t sign_bit =
                std::uint64_t{1} << (type_info->get_bit_width() - 1);
            if ((bits & sign_bit) != 0) {
                bits |= ~mask;
            }
        }
    }

    return static_cast<long long>(bits);
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
