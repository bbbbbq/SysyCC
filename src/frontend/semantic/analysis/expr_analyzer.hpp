#pragma once

#include <string>

#include "common/source_span.hpp"

namespace sysycc {

class Expr;

namespace detail {

class ConstantEvaluator;
class ConversionChecker;
class SemanticContext;
class ScopeStack;
class TypeResolver;

// Performs semantic analysis for AST expressions.
class ExprAnalyzer {
  private:
    const TypeResolver &type_resolver_;
    const ConversionChecker &conversion_checker_;
    const ConstantEvaluator &constant_evaluator_;

    void add_error(SemanticContext &semantic_context, std::string message,
                   const class SourceSpan &source_span) const;
    void add_warning(SemanticContext &semantic_context, std::string message,
                     const class SourceSpan &source_span,
                     std::string warning_option = {}) const;

  public:
    ExprAnalyzer(const TypeResolver &type_resolver,
                 const ConversionChecker &conversion_checker,
                 const ConstantEvaluator &constant_evaluator);

    void analyze_expr(const Expr *expr, SemanticContext &semantic_context,
                      ScopeStack &scope_stack) const;
};

} // namespace detail
} // namespace sysycc
