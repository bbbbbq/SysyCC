#pragma once

#include <string>

namespace sysycc {

class Expr;
class SemanticModel;
class SemanticType;

namespace detail {

class ConstantEvaluator;
class SemanticContext;

// Checks type compatibility and operand-category constraints.
class ConversionChecker {
  public:
    bool is_assignable_expr(const Expr *expr) const;
    bool is_same_type(const SemanticType *lhs, const SemanticType *rhs) const;
    bool is_assignable_type(const SemanticType *target,
                            const SemanticType *value) const;
    bool is_assignable_value(const SemanticType *target, const SemanticType *value,
                             const Expr *value_expr,
                             SemanticContext &semantic_context,
                             const ConstantEvaluator &constant_evaluator) const;
    bool is_compatible_equality_type(const SemanticType *lhs,
                                     const SemanticType *rhs,
                                     const Expr *lhs_expr,
                                     const Expr *rhs_expr,
                                     SemanticContext &semantic_context,
                                     const ConstantEvaluator &constant_evaluator) const;
    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs, const SemanticType *rhs,
        SemanticModel &semantic_model) const;
    bool is_arithmetic_type(const SemanticType *type) const;
    bool is_scalar_type(const SemanticType *type) const;
    bool is_integer_like_type(const SemanticType *type) const;
    bool is_incrementable_type(const SemanticType *type) const;
    bool is_void_type(const SemanticType *type) const;
    bool is_pointer_type(const SemanticType *type) const;
    bool is_struct_type(const SemanticType *type) const;
    bool is_pointer_to_struct_type(const SemanticType *type) const;
    bool is_null_pointer_constant(const Expr *expr,
                                  SemanticContext &semantic_context,
                                  const ConstantEvaluator &constant_evaluator) const;
    const SemanticType *get_decayed_type(const SemanticType *type,
                                         SemanticModel &semantic_model) const;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    bool is_same_or_decayed_pointer_target(const SemanticType *target,
                                           const SemanticType *value,
                                           SemanticModel &semantic_model) const;
};

} // namespace detail
} // namespace sysycc
