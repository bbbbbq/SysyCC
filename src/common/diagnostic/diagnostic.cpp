#include "common/diagnostic/diagnostic.hpp"

#include <utility>

namespace sysycc {

Diagnostic::Diagnostic(DiagnosticLevel level, DiagnosticStage stage,
                       std::string message, SourceSpan source_span)
    : level_(level), stage_(stage), message_(std::move(message)),
      source_span_(source_span) {}

DiagnosticLevel Diagnostic::get_level() const noexcept { return level_; }

DiagnosticStage Diagnostic::get_stage() const noexcept { return stage_; }

const std::string &Diagnostic::get_message() const noexcept { return message_; }

const SourceSpan &Diagnostic::get_source_span() const noexcept {
    return source_span_;
}

const char *get_diagnostic_level_name(DiagnosticLevel level) noexcept {
    switch (level) {
    case DiagnosticLevel::Error:
        return "error";
    case DiagnosticLevel::Warning:
        return "warning";
    case DiagnosticLevel::Note:
        return "note";
    }

    return "unknown";
}

const char *get_diagnostic_stage_name(DiagnosticStage stage) noexcept {
    switch (stage) {
    case DiagnosticStage::Preprocess:
        return "preprocess";
    case DiagnosticStage::Lexer:
        return "lexer";
    case DiagnosticStage::Parser:
        return "parser";
    case DiagnosticStage::Ast:
        return "ast";
    case DiagnosticStage::Semantic:
        return "semantic";
    case DiagnosticStage::Compiler:
        return "compiler";
    }

    return "unknown";
}

} // namespace sysycc
