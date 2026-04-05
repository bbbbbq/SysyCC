#include "frontend/semantic/analysis/semantic_analyzer.hpp"

#include <memory>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/attribute/attribute_analyzer.hpp"
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

void add_warning(SemanticContext &semantic_context, std::string message,
                 const SourceSpan &source_span) {
    if (semantic_context.is_system_header_span(source_span)) {
        return;
    }
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Warning, std::move(message),
                           source_span));
}

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

bool function_decl_has_body(const SemanticSymbol *symbol) {
    if (symbol == nullptr || symbol->get_decl_node() == nullptr ||
        symbol->get_decl_node()->get_kind() != AstKind::FunctionDecl) {
        return false;
    }

    const auto *function_decl =
        static_cast<const FunctionDecl *>(symbol->get_decl_node());
    return function_decl->get_body() != nullptr;
}

bool expr_is_obviously_nonzero_constant(const Expr *expr) {
    if (expr == nullptr) {
        return false;
    }
    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return static_cast<const IntegerLiteralExpr *>(expr)->get_value_text() != "0";
    case AstKind::CharLiteralExpr:
        return static_cast<const CharLiteralExpr *>(expr)->get_value_text() != "'\\0'";
    default:
        return false;
    }
}

void emit_unused_static_function_warnings(
    SemanticContext &semantic_context,
    const std::vector<const SemanticSymbol *> &static_function_candidates) {
    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    for (const SemanticSymbol *symbol : static_function_candidates) {
        if (symbol == nullptr || symbol->get_decl_node() == nullptr ||
            symbol->get_name().empty()) {
            continue;
        }
        if (semantic_context.is_system_header_span(
                symbol->get_decl_node()->get_source_span())) {
            continue;
        }
        if (semantic_model.get_symbol_use_count(symbol) != 0U) {
            continue;
        }
        add_warning(semantic_context,
                    "unused static function '" + symbol->get_name() + "'",
                    symbol->get_decl_node()->get_source_span());
    }
}

void emit_unused_symbol_warnings(SemanticContext &semantic_context) {
    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    for (const SemanticSymbol *symbol : semantic_context.get_function_local_symbols()) {
        if (symbol == nullptr || symbol->get_decl_node() == nullptr ||
            symbol->get_name().empty()) {
            continue;
        }
        const std::size_t use_count = semantic_model.get_symbol_use_count(symbol);
        const std::size_t write_count =
            semantic_model.get_symbol_write_count(symbol);

        if (semantic_context.is_system_header_span(
                symbol->get_decl_node()->get_source_span())) {
            continue;
        }

        switch (symbol->get_kind()) {
        case SymbolKind::Parameter:
            if (use_count == write_count) {
                add_warning(semantic_context,
                            "unused parameter '" + symbol->get_name() + "'",
                            symbol->get_decl_node()->get_source_span());
            }
            break;
        case SymbolKind::Variable:
            if (use_count == 0U) {
                add_warning(semantic_context,
                            "unused variable '" + symbol->get_name() + "'",
                            symbol->get_decl_node()->get_source_span());
            } else if (write_count != 0U && use_count == write_count) {
                add_warning(semantic_context,
                            "variable '" + symbol->get_name() +
                                "' set but not used",
                            symbol->get_decl_node()->get_source_span());
            }
            break;
        default:
            break;
        }
    }
}

void emit_unused_label_warnings(SemanticContext &semantic_context) {
    for (const auto &label_definition :
         semantic_context.get_unused_label_definitions()) {
        add_warning(semantic_context,
                    "unused label '" + label_definition.label_name + "'",
                    label_definition.source_span);
    }
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
    ConversionChecker conversion_checker(
        &semantic_context.get_compiler_context()
             .get_dialect_manager()
             .get_semantic_feature_registry(),
        &semantic_context.get_compiler_context()
             .get_dialect_manager()
             .get_builtin_type_semantic_handler_registry());
    ConstantEvaluator constant_evaluator;
    ExprAnalyzer expr_analyzer(type_resolver, conversion_checker,
                               constant_evaluator);
    AttributeAnalyzer attribute_analyzer;
    DeclAnalyzer decl_analyzer(type_resolver, conversion_checker,
                               constant_evaluator, expr_analyzer);
    StmtAnalyzer stmt_analyzer(decl_analyzer, expr_analyzer, conversion_checker,
                               constant_evaluator);
    std::vector<const SemanticSymbol *> static_function_candidates;

    for (const auto &decl : translation_unit->get_top_level_decls()) {
        if (decl->get_kind() == AstKind::FunctionDecl) {
            analyze_function_decl(decl.get(), semantic_context, scope_stack,
                                  type_resolver, conversion_checker,
                                  decl_analyzer, stmt_analyzer,
                                  attribute_analyzer,
                                  static_function_candidates);
            continue;
        }
        decl_analyzer.analyze_decl(decl.get(), semantic_context, scope_stack);
    }

    emit_unused_static_function_warnings(semantic_context,
                                         static_function_candidates);
}

const SemanticType *SemanticAnalyzer::build_function_type(
    const Decl *decl, SemanticContext &semantic_context,
    const TypeResolver &type_resolver, const ScopeStack &scope_stack) const {
    if (decl == nullptr || decl->get_kind() != AstKind::FunctionDecl) {
        return nullptr;
    }

    const auto *function_decl = static_cast<const FunctionDecl *>(decl);
    const SemanticType *return_type =
        type_resolver.resolve_type(function_decl->get_return_type(),
                                   semantic_context, &scope_stack);
    std::vector<const SemanticType *> parameter_types;
    for (const auto &parameter : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter.get());
        const SemanticType *parameter_type = type_resolver.apply_array_dimensions(
            type_resolver.resolve_type(param_decl->get_declared_type(),
                                       semantic_context, &scope_stack),
            param_decl->get_dimensions(), semantic_context);
        parameter_types.push_back(
            type_resolver.adjust_parameter_type(parameter_type, semantic_context));
    }

    return semantic_context.get_semantic_model().own_type(
        std::make_unique<FunctionSemanticType>(return_type, parameter_types,
                                               function_decl->get_is_variadic()));
}

void SemanticAnalyzer::analyze_function_decl(
    const Decl *decl, SemanticContext &semantic_context, ScopeStack &scope_stack,
    const TypeResolver &type_resolver,
    const ConversionChecker &conversion_checker,
    const DeclAnalyzer &decl_analyzer,
    const StmtAnalyzer &stmt_analyzer,
    const AttributeAnalyzer &attribute_analyzer,
    std::vector<const SemanticSymbol *> &static_function_candidates) const {
    if (decl == nullptr || decl->get_kind() != AstKind::FunctionDecl) {
        return;
    }

    auto *function_decl = static_cast<const FunctionDecl *>(decl);
    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    const SemanticType *function_type =
        build_function_type(function_decl, semantic_context, type_resolver,
                            scope_stack);
    const SemanticSymbol *function_symbol = nullptr;
    bool function_defined = false;
    if (const SemanticSymbol *existing_symbol =
            scope_stack.lookup_local(function_decl->get_name());
        existing_symbol != nullptr) {
        if (existing_symbol->get_kind() != SymbolKind::Function ||
            !conversion_checker.is_same_type(existing_symbol->get_type(),
                                            function_type) ||
            (function_decl->get_body() != nullptr &&
             function_decl_has_body(existing_symbol))) {
            add_error(semantic_context,
                      "redefinition of symbol: " + function_decl->get_name(),
                      function_decl->get_source_span());
        } else {
            function_symbol = existing_symbol;
            function_defined = true;
        }
    } else {
        function_symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::Function,
                                             function_decl->get_name(),
                                             function_type, function_decl));
        function_defined = define_symbol(semantic_context, scope_stack,
                                         function_symbol,
                                         function_decl->get_source_span());
    }
    if (function_defined) {
        semantic_model.bind_symbol(function_decl, function_symbol);
        semantic_model.bind_node_type(function_decl, function_type);
        if (function_decl->get_is_static() && function_decl->get_body() != nullptr) {
            static_function_candidates.push_back(function_symbol);
        }
    }
    semantic_model.bind_function_attributes(
        function_decl,
        attribute_analyzer.analyze_function_attributes(function_decl,
                                                      semantic_context));

    const auto *resolved_function_type =
        static_cast<const FunctionSemanticType *>(function_type);
    if (function_decl->get_body() == nullptr) {
        return;
    }

    scope_stack.push_scope();
    semantic_context.set_current_function(function_decl);
    semantic_context.set_current_return_type(
        resolved_function_type != nullptr ? resolved_function_type->get_return_type()
                                          : nullptr);
    semantic_context.begin_function_labels();
    for (const auto &parameter : function_decl->get_parameters()) {
        decl_analyzer.analyze_decl(parameter.get(), semantic_context, scope_stack);
    }
    stmt_analyzer.analyze_stmt(function_decl->get_body(), semantic_context,
                               scope_stack);
    for (const auto &reference :
         semantic_context.get_undefined_goto_references()) {
        add_error(semantic_context,
                  "undefined label '" + reference.label_name + "'",
                  reference.source_span);
    }
    emit_unused_symbol_warnings(semantic_context);
    emit_unused_label_warnings(semantic_context);
    if (resolved_function_type != nullptr &&
        resolved_function_type->get_return_type() != nullptr &&
        !conversion_checker.is_void_type(
            resolved_function_type->get_return_type()) &&
        !stmt_guarantees_return(function_decl->get_body())) {
        add_error(semantic_context,
                  "non-void function may exit without returning a value",
                  function_decl->get_source_span());
    }
    semantic_context.end_function_labels();
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
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        return expr_is_obviously_nonzero_constant(while_stmt->get_condition());
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(stmt);
        return stmt_guarantees_return(label_stmt->get_body());
    }
    default:
        return false;
    }
}

} // namespace sysycc::detail
