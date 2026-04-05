#include "frontend/semantic/semantic_pass.hpp"

#include <memory>
#include <string>

#include "frontend/ast/ast_node.hpp"
#include "common/diagnostic/diagnostic.hpp"
#include "frontend/semantic/support/builtin_symbols.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/analysis/semantic_analyzer.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"

namespace sysycc {

namespace {

bool has_error_diagnostics(const SemanticModel &semantic_model) {
    for (const auto &diagnostic : semantic_model.get_diagnostics()) {
        if (diagnostic.get_severity() == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

std::string first_error_message(const SemanticModel &semantic_model) {
    for (const auto &diagnostic : semantic_model.get_diagnostics()) {
        if (diagnostic.get_severity() != DiagnosticSeverity::Error) {
            continue;
        }
        return diagnostic.get_message();
    }
    return "semantic analysis failed";
}

DiagnosticLevel to_diagnostic_level(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return DiagnosticLevel::Error;
    case DiagnosticSeverity::Warning:
        return DiagnosticLevel::Warning;
    }

    return DiagnosticLevel::Error;
}

} // namespace

PassKind SemanticPass::Kind() const { return PassKind::Semantic; }

const char *SemanticPass::Name() const { return "SemanticPass"; }

PassResult SemanticPass::Run(CompilerContext &context) {
    context.clear_semantic_model();

    if (context.get_ast_root() == nullptr) {
        const std::string message =
            "failed to run semantic analysis: missing ast";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Semantic,
                                                  message);
        return PassResult::Failure(message);
    }

    auto owned_semantic_model = std::make_unique<SemanticModel>();
    if (!context.get_ast_complete()) {
        owned_semantic_model->set_success(false);
        context.set_semantic_model(std::move(owned_semantic_model));
        return PassResult::Success();
    }

    detail::SemanticContext semantic_context(context,
                                             std::move(owned_semantic_model));
    detail::ScopeStack scope_stack;
    detail::BuiltinSymbols builtin_symbols;
    detail::SemanticAnalyzer analyzer;

    scope_stack.push_scope();
    builtin_symbols.install(semantic_context.get_semantic_model(), scope_stack);

    if (context.get_ast_root()->get_kind() == AstKind::TranslationUnit) {
        const auto *translation_unit =
            static_cast<const TranslationUnit *>(context.get_ast_root());
        analyzer.Analyze(translation_unit, semantic_context, scope_stack);
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    for (const SemanticDiagnostic &diagnostic :
         semantic_model.get_diagnostics()) {
        context.get_diagnostic_engine().add_diagnostic(Diagnostic(
            to_diagnostic_level(diagnostic.get_severity()),
            DiagnosticStage::Semantic, diagnostic.get_message(),
            diagnostic.get_source_span(), diagnostic.get_warning_option()));
    }
    const bool success = !has_error_diagnostics(semantic_model);
    semantic_model.set_success(success);

    const std::string error_message =
        success ? "" : first_error_message(semantic_model);
    context.set_semantic_model(semantic_context.release_semantic_model());
    if (!success) {
        return PassResult::Failure(error_message);
    }
    return PassResult::Success();
}

} // namespace sysycc
