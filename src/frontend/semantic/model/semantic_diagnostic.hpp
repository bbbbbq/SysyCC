#pragma once

#include <string>

#include "common/source_span.hpp"

namespace sysycc {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

// Represents one semantic diagnostic produced during analysis.
class SemanticDiagnostic {
  private:
    DiagnosticSeverity severity_;
    std::string message_;
    SourceSpan source_span_;

  public:
    SemanticDiagnostic(DiagnosticSeverity severity, std::string message,
                       SourceSpan source_span = {});

    DiagnosticSeverity get_severity() const noexcept;
    const std::string &get_message() const noexcept;
    const SourceSpan &get_source_span() const noexcept;
};

} // namespace sysycc
