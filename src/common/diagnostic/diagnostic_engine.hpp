#pragma once

#include <string>
#include <vector>

#include "common/diagnostic/diagnostic.hpp"

namespace sysycc {

// Collects diagnostics emitted across compiler passes.
class DiagnosticEngine {
  private:
    std::vector<Diagnostic> diagnostics_;

  public:
    DiagnosticEngine() = default;

    const std::vector<Diagnostic> &get_diagnostics() const noexcept;
    std::vector<Diagnostic> &get_diagnostics() noexcept;

    void clear();
    bool has_error() const noexcept;

    void add_diagnostic(Diagnostic diagnostic);
    void add_error(DiagnosticStage stage, std::string message,
                   SourceSpan source_span = {});
    void add_warning(DiagnosticStage stage, std::string message,
                     SourceSpan source_span = {});
    void add_note(DiagnosticStage stage, std::string message,
                  SourceSpan source_span = {});
};

} // namespace sysycc
