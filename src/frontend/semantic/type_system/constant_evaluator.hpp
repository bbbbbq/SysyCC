#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sysycc {

class AstNode;
class Expr;
class SemanticModel;
class SemanticType;

namespace detail {

class ConversionChecker;
class SemanticContext;

// Reads and stores foldable integer constant-expression results.
class ConstantEvaluator {
  public:
    std::optional<long long>
    get_integer_constant_value(const AstNode *node,
                               const SemanticContext &semantic_context) const;
    std::optional<long long>
    get_integer_constant_value(const AstNode *node,
                               const SemanticModel &semantic_model) const;
    std::optional<long long> get_scalar_constant_value_as_integer(
        const Expr *expr, const SemanticType *target_type,
        const SemanticContext &semantic_context) const;
    std::optional<long double>
    get_scalar_numeric_constant_value(const Expr *expr,
                                      const SemanticModel &semantic_model) const;
    std::optional<long long> get_scalar_constant_value_as_integer(
        const Expr *expr, const SemanticType *target_type,
        const SemanticModel &semantic_model) const;
    void bind_integer_constant_value(const AstNode *node, long long value,
                                     SemanticContext &semantic_context) const;
    bool is_integer_constant_expr(const Expr *expr,
                                  const SemanticContext &semantic_context,
                                  const ConversionChecker &conversion_checker) const;
    bool is_static_storage_initializer(const Expr *expr,
                                       const SemanticType *target_type,
                                       const SemanticModel &semantic_model) const;

  private:
    std::optional<long long>
    evaluate_integer_expr(const Expr *expr,
                          const SemanticContext &semantic_context) const;
    std::optional<long long>
    evaluate_integer_expr(const Expr *expr,
                          const SemanticModel &semantic_model) const;
    std::optional<long double>
    evaluate_scalar_numeric_expr(const Expr *expr,
                                 const SemanticContext &semantic_context) const;
    std::optional<long double>
    evaluate_scalar_numeric_expr(const Expr *expr,
                                 const SemanticModel &semantic_model) const;
    bool is_static_storage_initializer_impl(const Expr *expr,
                                            const SemanticType *target_type,
                                            const SemanticModel &semantic_model) const;
    bool is_static_address_value_expr(const Expr *expr,
                                      const SemanticModel &semantic_model) const;
    bool is_static_address_lvalue_expr(const Expr *expr,
                                       const SemanticModel &semantic_model) const;
    std::optional<long long>
    convert_scalar_numeric_value_to_integer(long double value,
                                            const SemanticType *target_type) const;
    std::optional<long long> parse_char_literal(
        const std::string &value_text) const;
};

} // namespace detail
} // namespace sysycc
