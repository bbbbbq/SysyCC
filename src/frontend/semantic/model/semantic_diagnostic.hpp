#pragma once

#include <stdint.h>
#include <string>

#include "common/source_span.hpp"

namespace sysycc {

enum class DiagnosticSeverity : uint8_t {
    Error,
    Warning,
};

// Represents one semantic diagnostic produced during analysis.
class SemanticDiagnostic {
  private:
    DiagnosticSeverity severity_;
    std::string message_;
    SourceSpan source_span_;
    std::string warning_option_;

  public:
    SemanticDiagnostic(DiagnosticSeverity severity, std::string message,
                       SourceSpan source_span = {},
                       std::string warning_option = {});

    DiagnosticSeverity get_severity() const noexcept;
    const std::string &get_message() const noexcept;
    const SourceSpan &get_source_span() const noexcept;
    const std::string &get_warning_option() const noexcept;
};

} // namespace sysycc
