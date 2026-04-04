#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "common/diagnostic/diagnostic.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

struct DiagnosticCliSourceExcerpt {
    std::string source_line;
    std::string caret_line;
};

// Formats shared diagnostics into GCC-like CLI output.
class DiagnosticFormatter {
  public:
    static std::string format_diagnostic_header(const Diagnostic &diagnostic);
    static std::optional<DiagnosticCliSourceExcerpt>
    format_source_excerpt(const Diagnostic &diagnostic);
    static std::string format_diagnostic_for_cli(const Diagnostic &diagnostic);
    static void print_diagnostics(std::ostream &os,
                                  const DiagnosticEngine &diagnostic_engine);
};

} // namespace sysycc
