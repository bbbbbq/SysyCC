#pragma once

#include <stdint.h>
#include <string>

#include "common/source_span.hpp"

namespace sysycc {

enum class DiagnosticLevel : uint8_t {
    Error,
    Warning,
    Note,
};

enum class DiagnosticStage : uint8_t {
    Preprocess,
    Lexer,
    Parser,
    Ast,
    Semantic,
    Compiler,
};

// Represents one pass-independent diagnostic entry.
class Diagnostic {
  private:
    DiagnosticLevel level_;
    DiagnosticStage stage_;
    std::string message_;
    SourceSpan source_span_;

  public:
    Diagnostic(DiagnosticLevel level, DiagnosticStage stage, std::string message,
               SourceSpan source_span = {});

    DiagnosticLevel get_level() const noexcept;
    DiagnosticStage get_stage() const noexcept;
    const std::string &get_message() const noexcept;
    const SourceSpan &get_source_span() const noexcept;
};

const char *get_diagnostic_level_name(DiagnosticLevel level) noexcept;
const char *get_diagnostic_stage_name(DiagnosticStage stage) noexcept;

} // namespace sysycc
