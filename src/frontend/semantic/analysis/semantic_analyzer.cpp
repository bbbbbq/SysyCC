#include "frontend/semantic/analysis/semantic_analyzer.hpp"

#include <memory>
#include <vector>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/analysis/decl_analyzer.hpp"
#include "frontend/semantic/analysis/expr_analyzer.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/analysis/stmt_analyzer.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

void add_error(SemanticContext &semantic_context, std::string message,
               const SourceSpan &source_span) {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
}

bool define_symbol(SemanticContext &semantic_context, ScopeStack &scope_stack,
                   const SemanticSymbol *symbol, const SourceSpan &source_span) {
    if (symbol == nullptr) {
        return false;
    }
    if (scope_stack.define(symbol)) {
        return true;
    }
    add_error(semantic_context, "redefinition of symbol: " + symbol->get_name(),
              source_span);
    return false;
}

} // namespace

void SemanticAnalyzer::Analyze(const TranslationUnit *translation_unit,
                               SemanticContext &semantic_context,
                               ScopeStack &scope_stack) const {
    if (translation_unit == nullptr) {
        semantic_context.get_semantic_model().set_success(false);
        return;
    }

    TypeResolver type_resolver;
    ConversionChecker conversion_checker;
    ConstantEvaluator constant_evaluator;
    ExprAnalyzer expr_analyzer(type_resolver, conversion_checker,
                               constant_evaluator);
    DeclAnalyzer decl_analyzer(type_resolver, conversion_checker,
                               constant_evaluator, expr_analyzer);
    StmtAnalyzer stmt_analyzer(decl_analyzer, expr_analyzer, conversion_checker,
                               constant_evaluator);

    for (const auto &decl : translation_unit->get_top_level_decls()) {
        if (decl->get_kind() == AstKind::FunctionDecl) {
            analyze_function_decl(decl.get(), semantic_context, scope_stack,
                                  type_resolver, conversion_checker,
                                  decl_analyzer, stmt_analyzer);
            continue;
        }
        decl_analyzer.analyze_decl(decl.get(), semantic_context, scope_stack);
    }
}

const SemanticType *SemanticAnalyzer::build_function_type(
    const Decl *decl, SemanticContext &semantic_context,
    const TypeResolver &type_resolver) const {
    if (decl == nullptr || decl->get_kind() != AstKind::FunctionDecl) {
        return nullptr;
    }

    const auto *function_decl = static_cast<const FunctionDecl *>(decl);
    const SemanticType *return_type =
        type_resolver.resolve_type(function_decl->get_return_type(),
                                   semantic_context);
    std::vector<const SemanticType *> parameter_types;
    for (const auto &parameter : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter.get());
        parameter_types.push_back(type_resolver.apply_array_dimensions(
            type_resolver.resolve_type(param_decl->get_declared_type(),
                                       semantic_context),
            param_decl->get_dimensions(), semantic_context));
    }

    return semantic_context.get_semantic_model().own_type(
        std::make_unique<FunctionSemanticType>(return_type, parameter_types));
}

void SemanticAnalyzer::analyze_function_decl(
    const Decl *decl, SemanticContext &semantic_context, ScopeStack &scope_stack,
    const TypeResolver &type_resolver,
    const ConversionChecker &conversion_checker,
    const DeclAnalyzer &decl_analyzer,
    const StmtAnalyzer &stmt_analyzer) const {
    if (decl == nullptr || decl->get_kind() != AstKind::FunctionDecl) {
        return;
    }

    auto *function_decl = static_cast<const FunctionDecl *>(decl);
    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    const SemanticType *function_type =
        build_function_type(function_decl, semantic_context, type_resolver);
    const auto *function_symbol = semantic_model.own_symbol(
        std::make_unique<SemanticSymbol>(SymbolKind::Function,
                                         function_decl->get_name(),
                                         function_type, function_decl));
    const bool function_defined =
        define_symbol(semantic_context, scope_stack, function_symbol,
                      function_decl->get_source_span());
    if (function_defined) {
        semantic_model.bind_symbol(function_decl, function_symbol);
        semantic_model.bind_node_type(function_decl, function_type);
    }

    const auto *resolved_function_type =
        static_cast<const FunctionSemanticType *>(function_type);
    scope_stack.push_scope();
    semantic_context.set_current_function(function_decl);
    semantic_context.set_current_return_type(
        resolved_function_type != nullptr ? resolved_function_type->get_return_type()
                                          : nullptr);
    for (const auto &parameter : function_decl->get_parameters()) {
        decl_analyzer.analyze_decl(parameter.get(), semantic_context, scope_stack);
    }
    stmt_analyzer.analyze_stmt(function_decl->get_body(), semantic_context,
                               scope_stack);
    if (resolved_function_type != nullptr &&
        resolved_function_type->get_return_type() != nullptr &&
        !conversion_checker.is_void_type(
            resolved_function_type->get_return_type()) &&
        !stmt_guarantees_return(function_decl->get_body())) {
        add_error(semantic_context,
                  "non-void function may exit without returning a value",
                  function_decl->get_source_span());
    }
    semantic_context.set_current_function(nullptr);
    semantic_context.set_current_return_type(nullptr);
    scope_stack.pop_scope();
}

bool SemanticAnalyzer::stmt_guarantees_return(const Stmt *stmt) const {
    if (stmt == nullptr) {
        return false;
    }

    switch (stmt->get_kind()) {
    case AstKind::ReturnStmt:
        return true;
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &statement : block_stmt->get_statements()) {
            if (stmt_guarantees_return(statement.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        return if_stmt->get_then_branch() != nullptr &&
               if_stmt->get_else_branch() != nullptr &&
               stmt_guarantees_return(if_stmt->get_then_branch()) &&
               stmt_guarantees_return(if_stmt->get_else_branch());
    }
    default:
        return false;
    }
}

} // namespace sysycc::detail
