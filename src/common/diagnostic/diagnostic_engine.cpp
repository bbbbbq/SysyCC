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

void DiagnosticEngine::add_diagnostic(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::add_error(DiagnosticStage stage, std::string message,
                                 SourceSpan source_span) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Error, stage, std::move(message),
                              source_span));
}

void DiagnosticEngine::add_warning(DiagnosticStage stage, std::string message,
                                   SourceSpan source_span) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Warning, stage,
                              std::move(message), source_span));
}

void DiagnosticEngine::add_note(DiagnosticStage stage, std::string message,
                                SourceSpan source_span) {
    add_diagnostic(Diagnostic(DiagnosticLevel::Note, stage, std::move(message),
                              source_span));
}

} // namespace sysycc
