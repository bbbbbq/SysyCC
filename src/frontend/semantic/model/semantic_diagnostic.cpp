#include "frontend/semantic/model/semantic_diagnostic.hpp"

#include <utility>

namespace sysycc {

SemanticDiagnostic::SemanticDiagnostic(DiagnosticSeverity severity,
                                       std::string message,
                                       SourceSpan source_span)
    : severity_(severity), message_(std::move(message)),
      source_span_(source_span) {}

DiagnosticSeverity SemanticDiagnostic::get_severity() const noexcept {
    return severity_;
}

const std::string &SemanticDiagnostic::get_message() const noexcept {
    return message_;
}

const SourceSpan &SemanticDiagnostic::get_source_span() const noexcept {
    return source_span_;
}

} // namespace sysycc
