#include "common/diagnostic/diagnostic_engine.hpp"

#include <utility>

namespace sysycc {

const std::vector<Diagnostic> &DiagnosticEngine::get_diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<Diagnostic> &DiagnosticEngine::get_diagnostics() noexcept {
    return diagnostics_;
}

void DiagnosticEngine::clear() { diagnostics_.clear(); }

bool DiagnosticEngine::has_error() const noexcept {
    for (const Diagnostic &diagnostic : diagnostics_) {
        if (diagnostic.get_level() == DiagnosticLevel::Error) {
            return true;
        }
    }
    return false;
}

const WarningPolicy &DiagnosticEngine::get_warning_policy() const noexcept {
    return warning_policy_;
}

void DiagnosticEngine::set_warning_policy(WarningPolicy warning_policy) {
    warning_policy_ = std::move(warning_policy);
}

void DiagnosticEngine::add_diagnostic(Diagnostic diagnostic) {
    if (diagnostic.get_level() == DiagnosticLevel::Warning) {
        if (!warning_policy_.should_emit_warning(diagnostic.get_warning_option())) {
            return;
        }
        if (warning_policy_.should_treat_warning_as_error(
                diagnostic.get_warning_option())) {
            diagnostic = Diagnostic(DiagnosticLevel::Error,
                                    diagnostic.get_stage(),
                                    diagnostic.get_message(),
                                    diagnostic.get_source_span(),
                                    diagnostic.get_warning_option(), true);
        }
    }
    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::add_error(DiagnosticStage stage, std::string message,
                                 SourceSpan source_span) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Error, stage, std::move(message),
                              source_span));
}

void DiagnosticEngine::add_warning(DiagnosticStage stage, std::string message,
                                   SourceSpan source_span,
                                   std::string warning_option) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Warning, stage,
                              std::move(message), source_span,
                              std::move(warning_option)));
}

void DiagnosticEngine::add_note(DiagnosticStage stage, std::string message,
                                SourceSpan source_span) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Note, stage, std::move(message),
                              source_span));
}

} // namespace sysycc
