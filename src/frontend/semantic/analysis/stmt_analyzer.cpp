#include "frontend/semantic/analysis/stmt_analyzer.hpp"

#include <string>
#include <utility>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/analysis/decl_analyzer.hpp"
#include "frontend/semantic/analysis/expr_analyzer.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

StmtAnalyzer::StmtAnalyzer(const DeclAnalyzer &decl_analyzer,
                           const ExprAnalyzer &expr_analyzer,
                           const ConversionChecker &conversion_checker,
                           const ConstantEvaluator &constant_evaluator)
    : decl_analyzer_(decl_analyzer),
      expr_analyzer_(expr_analyzer),
      conversion_checker_(conversion_checker),
      constant_evaluator_(constant_evaluator) {}

void StmtAnalyzer::add_error(SemanticContext &semantic_context,
                             std::string message,
                             const SourceSpan &source_span) const {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
}

void StmtAnalyzer::analyze_stmt(const Stmt *stmt,
                                SemanticContext &semantic_context,
                                ScopeStack &scope_stack) const {
    if (stmt == nullptr) {
        return;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        scope_stack.push_scope();
        for (const auto &statement : block_stmt->get_statements()) {
            analyze_stmt(statement.get(), semantic_context, scope_stack);
        }
        scope_stack.pop_scope();
        return;
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            decl_analyzer_.analyze_decl(decl.get(), semantic_context, scope_stack);
        }
        return;
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        expr_analyzer_.analyze_expr(expr_stmt->get_expression(), semantic_context,
                                    scope_stack);
        return;
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        expr_analyzer_.analyze_expr(if_stmt->get_condition(), semantic_context,
                                    scope_stack);
        if (!conversion_checker_.is_scalar_type(
                semantic_model.get_node_type(if_stmt->get_condition()))) {
            add_error(semantic_context,
                      "condition expression must have scalar type",
                      if_stmt->get_condition()->get_source_span());
        }
        analyze_stmt(if_stmt->get_then_branch(), semantic_context, scope_stack);
        analyze_stmt(if_stmt->get_else_branch(), semantic_context, scope_stack);
        return;
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        expr_analyzer_.analyze_expr(while_stmt->get_condition(), semantic_context,
                                    scope_stack);
        if (!conversion_checker_.is_scalar_type(
                semantic_model.get_node_type(while_stmt->get_condition()))) {
            add_error(semantic_context,
                      "condition expression must have scalar type",
                      while_stmt->get_condition()->get_source_span());
        }
        semantic_context.enter_loop();
        analyze_stmt(while_stmt->get_body(), semantic_context, scope_stack);
        semantic_context.leave_loop();
        return;
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        semantic_context.enter_loop();
        analyze_stmt(do_while_stmt->get_body(), semantic_context, scope_stack);
        semantic_context.leave_loop();
        expr_analyzer_.analyze_expr(do_while_stmt->get_condition(),
                                    semantic_context, scope_stack);
        if (!conversion_checker_.is_scalar_type(
                semantic_model.get_node_type(do_while_stmt->get_condition()))) {
            add_error(semantic_context,
                      "condition expression must have scalar type",
                      do_while_stmt->get_condition()->get_source_span());
        }
        return;
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        scope_stack.push_scope();
        semantic_context.enter_loop();
        expr_analyzer_.analyze_expr(for_stmt->get_init(), semantic_context,
                                    scope_stack);
        expr_analyzer_.analyze_expr(for_stmt->get_condition(), semantic_context,
                                    scope_stack);
        if (for_stmt->get_condition() != nullptr &&
            !conversion_checker_.is_scalar_type(
                semantic_model.get_node_type(for_stmt->get_condition()))) {
            add_error(semantic_context,
                      "condition expression must have scalar type",
                      for_stmt->get_condition()->get_source_span());
        }
        expr_analyzer_.analyze_expr(for_stmt->get_step(), semantic_context,
                                    scope_stack);
        analyze_stmt(for_stmt->get_body(), semantic_context, scope_stack);
        semantic_context.leave_loop();
        scope_stack.pop_scope();
        return;
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        expr_analyzer_.analyze_expr(switch_stmt->get_condition(), semantic_context,
                                    scope_stack);
        if (!conversion_checker_.is_scalar_type(
                semantic_model.get_node_type(switch_stmt->get_condition()))) {
            add_error(semantic_context,
                      "condition expression must have scalar type",
                      switch_stmt->get_condition()->get_source_span());
        }
        semantic_context.enter_switch();
        analyze_stmt(switch_stmt->get_body(), semantic_context, scope_stack);
        semantic_context.leave_switch();
        return;
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(stmt);
        if (semantic_context.get_switch_depth() <= 0) {
            add_error(semantic_context,
                      "case label is only allowed inside switch statements",
                      case_stmt->get_source_span());
        }
        expr_analyzer_.analyze_expr(case_stmt->get_value(), semantic_context,
                                    scope_stack);
        if (!constant_evaluator_.is_integer_constant_expr(
                case_stmt->get_value(), semantic_context, conversion_checker_)) {
            add_error(semantic_context,
                      "case label must be an integer constant expression",
                      case_stmt->get_value()->get_source_span());
        } else {
            const auto value = constant_evaluator_.get_integer_constant_value(
                case_stmt->get_value(), semantic_context);
            if (value.has_value() &&
                !semantic_context.record_case_value(*value)) {
                add_error(semantic_context,
                          "duplicate case label value in switch statement",
                          case_stmt->get_value()->get_source_span());
            }
        }
        analyze_stmt(case_stmt->get_body(), semantic_context, scope_stack);
        return;
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(stmt);
        if (semantic_context.get_switch_depth() <= 0) {
            add_error(semantic_context,
                      "default label is only allowed inside switch statements",
                      default_stmt->get_source_span());
        } else if (!semantic_context.record_default_label()) {
            add_error(semantic_context,
                      "multiple default labels in one switch statement",
                      default_stmt->get_source_span());
        }
        analyze_stmt(default_stmt->get_body(), semantic_context, scope_stack);
        return;
    }
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        expr_analyzer_.analyze_expr(return_stmt->get_value(), semantic_context,
                                    scope_stack);

        const SemanticType *expected_type =
            semantic_context.get_current_return_type();
        const SemanticType *actual_type =
            semantic_model.get_node_type(return_stmt->get_value());

        if (expected_type == nullptr) {
            return;
        }

        if (conversion_checker_.is_void_type(expected_type)) {
            if (return_stmt->get_value() != nullptr) {
                add_error(semantic_context,
                          "return statement with a value in a void function",
                          return_stmt->get_source_span());
            }
            return;
        }

        if (return_stmt->get_value() == nullptr) {
            add_error(semantic_context,
                      "return statement is missing a value for a non-void function",
                      return_stmt->get_source_span());
            return;
        }

        if (actual_type == nullptr) {
            return;
        }

        if (!conversion_checker_.is_assignable_value(
                expected_type, actual_type, return_stmt->get_value(),
                semantic_context, constant_evaluator_)) {
            add_error(semantic_context,
                      "return type does not match function return type",
                      return_stmt->get_source_span());
        }
        return;
    }
    case AstKind::BreakStmt:
        if (semantic_context.get_loop_depth() <= 0 &&
            semantic_context.get_switch_depth() <= 0) {
            add_error(semantic_context,
                      "break statement is only allowed inside loops or switch statements",
                      stmt->get_source_span());
        }
        return;
    case AstKind::ContinueStmt:
        if (semantic_context.get_loop_depth() <= 0) {
            add_error(semantic_context,
                      "continue statement is only allowed inside loops",
                      stmt->get_source_span());
        }
        return;
    default:
        return;
    }
}

} // namespace sysycc::detail
