#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sysycc {

class AstNode;
class Expr;

namespace detail {

class ConversionChecker;
class SemanticContext;

// Reads and stores foldable integer constant-expression results.
class ConstantEvaluator {
  public:
    std::optional<long long>
    get_integer_constant_value(const AstNode *node,
                               const SemanticContext &semantic_context) const;
    std::optional<long long> get_scalar_constant_value_as_integer(
        const Expr *expr, const SemanticContext &semantic_context) const;
    void bind_integer_constant_value(const AstNode *node, long long value,
                                     SemanticContext &semantic_context) const;
    bool is_integer_constant_expr(const Expr *expr,
                                  const SemanticContext &semantic_context,
                                  const ConversionChecker &conversion_checker) const;

  private:
    std::optional<long long>
    evaluate_integer_expr(const Expr *expr,
                          const SemanticContext &semantic_context) const;
    std::optional<long double>
    evaluate_scalar_numeric_expr(const Expr *expr,
                                 const SemanticContext &semantic_context) const;
    std::optional<long long> parse_char_literal(
        const std::string &value_text) const;
};

} // namespace detail
} // namespace sysycc
