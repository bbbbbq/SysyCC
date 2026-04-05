#include "frontend/semantic/model/semantic_diagnostic.hpp"

#include <utility>

namespace sysycc {

SemanticDiagnostic::SemanticDiagnostic(DiagnosticSeverity severity,
                                       std::string message,
                                       SourceSpan source_span,
                                       std::string warning_option)
    : severity_(severity), message_(std::move(message)),
      source_span_(source_span), warning_option_(std::move(warning_option)) {}

DiagnosticSeverity SemanticDiagnostic::get_severity() const noexcept {
    return severity_;
}

const std::string &SemanticDiagnostic::get_message() const noexcept {
    return message_;
}

const SourceSpan &SemanticDiagnostic::get_source_span() const noexcept {
    return source_span_;
}

const std::string &SemanticDiagnostic::get_warning_option() const noexcept {
    return warning_option_;
}

} // namespace sysycc
