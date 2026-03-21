#include "common/diagnostic/diagnostic_formatter.hpp"

#include <ostream>

namespace sysycc {

DiagnosticCliFormatPolicy DiagnosticFormatter::get_cli_format_policy(
    const Diagnostic &diagnostic) noexcept {
    if (diagnostic.get_level() == DiagnosticLevel::Warning &&
        !diagnostic.get_source_span().empty()) {
        return {
            DiagnosticCliMessagePolicy::StageLevelMessage,
            DiagnosticCliSpanPolicy::AppendAtSpan,
        };
    }

    if (diagnostic.get_stage() == DiagnosticStage::Semantic &&
        !diagnostic.get_source_span().empty()) {
        return {
            DiagnosticCliMessagePolicy::StageLevelMessage,
            DiagnosticCliSpanPolicy::AppendAtSpan,
        };
    }

    return {};
}

std::string DiagnosticFormatter::format_source_span(
    const SourceSpan &source_span) {
    std::string formatted_span;
    if (source_span.get_file() != nullptr &&
        !source_span.get_file()->empty()) {
        formatted_span += source_span.get_file()->get_path();
        formatted_span += ":";
    }
    formatted_span += std::to_string(source_span.get_line_begin()) + ":" +
                      std::to_string(source_span.get_col_begin()) + "-" +
                      std::to_string(source_span.get_line_end()) + ":" +
                      std::to_string(source_span.get_col_end());
    return formatted_span;
}

std::string DiagnosticFormatter::format_diagnostic_for_cli(
    const Diagnostic &diagnostic) {
    const DiagnosticCliFormatPolicy policy = get_cli_format_policy(diagnostic);

    std::string rendered_message;
    switch (policy.message_policy_) {
    case DiagnosticCliMessagePolicy::PassThrough:
        rendered_message = diagnostic.get_message();
        break;
    case DiagnosticCliMessagePolicy::StageLevelMessage:
        rendered_message =
            std::string(get_diagnostic_stage_name(diagnostic.get_stage())) +
            " " +
            std::string(get_diagnostic_level_name(diagnostic.get_level())) +
            ": " + diagnostic.get_message();
        break;
    }

    switch (policy.span_policy_) {
    case DiagnosticCliSpanPolicy::Omit:
        return rendered_message;
    case DiagnosticCliSpanPolicy::AppendAtSpan:
        if (diagnostic.get_source_span().empty()) {
            return rendered_message;
        }
        return rendered_message + " at " +
               format_source_span(diagnostic.get_source_span());
    }
}

void DiagnosticFormatter::print_diagnostics(
    std::ostream &os, const DiagnosticEngine &diagnostic_engine) {
    for (const Diagnostic &diagnostic : diagnostic_engine.get_diagnostics()) {
        os << format_diagnostic_for_cli(diagnostic) << '\n';
    }
}

} // namespace sysycc
