#pragma once

#include <optional>
#include <string>

namespace sysycc {

class Expr;
class BuiltinTypeSemanticHandlerRegistry;
class SemanticFeatureRegistry;
enum class SemanticFeature : unsigned char;
class SemanticModel;
class SemanticType;

namespace detail {

class ConstantEvaluator;
class SemanticContext;

// Checks type compatibility and operand-category constraints.
class ConversionChecker {
  private:
    const SemanticFeatureRegistry *semantic_feature_registry_ = nullptr;
    const BuiltinTypeSemanticHandlerRegistry
        *builtin_type_semantic_handler_registry_ = nullptr;

    bool has_semantic_feature(SemanticFeature feature) const noexcept;
    bool has_extended_builtin_scalar_handler() const noexcept;

  public:
    explicit ConversionChecker(
        const SemanticFeatureRegistry *semantic_feature_registry = nullptr,
        const BuiltinTypeSemanticHandlerRegistry
            *builtin_type_semantic_handler_registry = nullptr) noexcept
        : semantic_feature_registry_(semantic_feature_registry),
          builtin_type_semantic_handler_registry_(
              builtin_type_semantic_handler_registry) {}

    bool is_assignable_expr(const Expr *expr) const;
    bool is_same_type(const SemanticType *lhs, const SemanticType *rhs) const;
    bool is_assignable_type(const SemanticType *target,
                            const SemanticType *value) const;
    bool is_assignable_value(const SemanticType *target, const SemanticType *value,
                             const Expr *value_expr,
                             SemanticContext &semantic_context,
                             const ConstantEvaluator &constant_evaluator) const;
    bool is_incompatible_pointer_assignment(const SemanticType *target,
                                            const SemanticType *value,
                                            SemanticModel &semantic_model) const;
    bool is_compatible_equality_type(const SemanticType *lhs,
                                     const SemanticType *rhs,
                                     const Expr *lhs_expr,
                                     const Expr *rhs_expr,
                                     SemanticContext &semantic_context,
                                     const ConstantEvaluator &constant_evaluator) const;
    bool should_warn_sign_compare(const SemanticType *lhs,
                                  const SemanticType *rhs,
                                  SemanticModel &semantic_model) const;
    bool should_warn_implicit_integer_narrowing(
        const SemanticType *target, const SemanticType *value,
        std::optional<long long> constant_value) const;
    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs, const SemanticType *rhs,
        SemanticModel &semantic_model) const;
    bool is_castable_type(const SemanticType *target,
                          const SemanticType *value) const;
    bool is_arithmetic_type(const SemanticType *type) const;
    bool is_scalar_type(const SemanticType *type) const;
    bool is_integer_like_type(const SemanticType *type) const;
    bool is_incrementable_type(const SemanticType *type) const;
    bool is_void_type(const SemanticType *type) const;
    bool is_pointer_type(const SemanticType *type) const;
    bool is_void_pointer_type(const SemanticType *type) const;
    bool is_struct_type(const SemanticType *type) const;
    bool is_union_type(const SemanticType *type) const;
    bool is_pointer_to_struct_type(const SemanticType *type) const;
    bool is_pointer_to_union_type(const SemanticType *type) const;
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
