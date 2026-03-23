#pragma once

#include <string>

#include "common/source_span.hpp"

namespace sysycc {

class Stmt;

namespace detail {

class ConstantEvaluator;
class ConversionChecker;
class DeclAnalyzer;
class ExprAnalyzer;
class SemanticContext;
class ScopeStack;

// Performs semantic analysis for AST statements.
class StmtAnalyzer {
  private:
    const DeclAnalyzer &decl_analyzer_;
    const ExprAnalyzer &expr_analyzer_;
    const ConversionChecker &conversion_checker_;
    const ConstantEvaluator &constant_evaluator_;

    void add_error(SemanticContext &semantic_context, std::string message,
                   const class SourceSpan &source_span) const;
    void add_warning(SemanticContext &semantic_context, std::string message,
                     const class SourceSpan &source_span) const;

  public:
    StmtAnalyzer(const DeclAnalyzer &decl_analyzer,
                 const ExprAnalyzer &expr_analyzer,
                 const ConversionChecker &conversion_checker,
                 const ConstantEvaluator &constant_evaluator);

    void analyze_stmt(const Stmt *stmt, SemanticContext &semantic_context,
                      ScopeStack &scope_stack) const;
};

} // namespace detail
} // namespace sysycc
