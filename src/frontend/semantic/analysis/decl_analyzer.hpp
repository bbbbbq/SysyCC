#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/source_span.hpp"

namespace sysycc {

class Decl;
class SemanticSymbol;
class TypeNode;

namespace detail {

class ConstantEvaluator;
class ConversionChecker;
class ExprAnalyzer;
class SemanticContext;
class ScopeStack;
class TypeResolver;

// Performs semantic analysis for non-function declarations.
class DeclAnalyzer {
  private:
    const TypeResolver &type_resolver_;
    const ConversionChecker &conversion_checker_;
    const ConstantEvaluator &constant_evaluator_;
    const ExprAnalyzer &expr_analyzer_;

    void add_error(SemanticContext &semantic_context, std::string message,
                   const class SourceSpan &source_span) const;
    bool define_symbol(SemanticContext &semantic_context,
                       ScopeStack &scope_stack, const SemanticSymbol *symbol,
                       const class SourceSpan &source_span) const;

  public:
    DeclAnalyzer(const TypeResolver &type_resolver,
                 const ConversionChecker &conversion_checker,
                 const ConstantEvaluator &constant_evaluator,
                 const ExprAnalyzer &expr_analyzer);

    void analyze_inline_type_declarations(const TypeNode *type_node,
                                          SemanticContext &semantic_context,
                                          ScopeStack &scope_stack) const;

    void analyze_enum_enumerators(
        const std::vector<std::unique_ptr<Decl>> &enumerators,
        SemanticContext &semantic_context, ScopeStack &scope_stack) const;

    void analyze_decl(const Decl *decl, SemanticContext &semantic_context,
                      ScopeStack &scope_stack) const;
};

} // namespace detail
} // namespace sysycc
