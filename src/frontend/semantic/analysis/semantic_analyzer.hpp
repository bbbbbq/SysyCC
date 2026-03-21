#pragma once

namespace sysycc {

class Decl;
class Stmt;
class SemanticType;
class TranslationUnit;
class AttributeAnalyzer;

namespace detail {

class ScopeStack;
class SemanticContext;
class TypeResolver;
class ConversionChecker;
class ConstantEvaluator;
class ExprAnalyzer;
class DeclAnalyzer;
class StmtAnalyzer;

// Coordinates the specialized semantic analyzers over the AST.
class SemanticAnalyzer {
  public:
    void Analyze(const TranslationUnit *translation_unit,
                 SemanticContext &semantic_context,
                 ScopeStack &scope_stack) const;

  private:
    const SemanticType *build_function_type(
        const Decl *decl, SemanticContext &semantic_context,
        const TypeResolver &type_resolver) const;
    void analyze_function_decl(const Decl *decl,
                               SemanticContext &semantic_context,
                               ScopeStack &scope_stack,
                               const TypeResolver &type_resolver,
                               const ConversionChecker &conversion_checker,
                               const DeclAnalyzer &decl_analyzer,
                               const StmtAnalyzer &stmt_analyzer,
                               const AttributeAnalyzer &attribute_analyzer) const;
    bool stmt_guarantees_return(const Stmt *stmt) const;
};

} // namespace detail
} // namespace sysycc
