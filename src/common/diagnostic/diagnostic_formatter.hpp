#pragma once

#include <stdint.h>
#include <iosfwd>
#include <string>

#include "common/diagnostic/diagnostic.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

enum class DiagnosticCliMessagePolicy : uint8_t {
    PassThrough,
    StageLevelMessage,
};

enum class DiagnosticCliSpanPolicy : uint8_t {
    Omit,
    AppendAtSpan,
};

struct DiagnosticCliFormatPolicy {
    DiagnosticCliMessagePolicy message_policy_ =
        DiagnosticCliMessagePolicy::PassThrough;
    DiagnosticCliSpanPolicy span_policy_ = DiagnosticCliSpanPolicy::Omit;
};

// Formats shared diagnostics for CLI-oriented human-readable output using one
// explicit CLI policy per diagnostic.
class DiagnosticFormatter {
  public:
    static DiagnosticCliFormatPolicy
    get_cli_format_policy(const Diagnostic &diagnostic) noexcept;
    static std::string format_source_span(const SourceSpan &source_span);
    static std::string format_diagnostic_for_cli(const Diagnostic &diagnostic);
    static void print_diagnostics(std::ostream &os,
                                  const DiagnosticEngine &diagnostic_engine);
};

} // namespace sysycc
