#include "common/diagnostic/diagnostic_formatter.hpp"

#include <algorithm>
#include <optional>
#include <ostream>

namespace sysycc {

namespace {

constexpr int k_tab_width = 4;

std::string expand_tabs(const std::string &text) {
    std::string expanded;
    expanded.reserve(text.size());
    int visual_column = 1;

    for (char ch : text) {
        if (ch == '\t') {
            const int spaces_to_next_tab_stop =
                k_tab_width - ((visual_column - 1) % k_tab_width);
            expanded.append(static_cast<std::size_t>(spaces_to_next_tab_stop),
                            ' ');
            visual_column += spaces_to_next_tab_stop;
            continue;
        }

        expanded.push_back(ch);
        ++visual_column;
    }

    return expanded;
}

std::size_t visual_column_for_source_column(const std::string &text,
                                            int source_column) {
    if (source_column <= 1) {
        return 1;
    }

    int current_source_column = 1;
    std::size_t visual_column = 1;
    for (char ch : text) {
        if (current_source_column >= source_column) {
            break;
        }

        if (ch == '\t') {
            const int spaces_to_next_tab_stop =
                k_tab_width -
                ((static_cast<int>(visual_column) - 1) % k_tab_width);
            visual_column += static_cast<std::size_t>(spaces_to_next_tab_stop);
        } else {
            ++visual_column;
        }

        ++current_source_column;
    }

    return visual_column;
}

bool should_render_source_excerpt(const Diagnostic &diagnostic) {
    return diagnostic.get_level() != DiagnosticLevel::Note &&
           !diagnostic.get_source_span().empty();
}

} // namespace

std::string DiagnosticFormatter::format_diagnostic_header(
    const Diagnostic &diagnostic) {
    std::string rendered_message;
    const SourceSpan &source_span = diagnostic.get_source_span();
    if (!source_span.empty()) {
        if (source_span.get_file() != nullptr &&
            !source_span.get_file()->empty()) {
            rendered_message += source_span.get_file()->get_path();
            rendered_message += ":";
        }
        rendered_message += std::to_string(source_span.get_line_begin());
        rendered_message += ":";
        rendered_message += std::to_string(source_span.get_col_begin());
        rendered_message += ": ";
    }

    rendered_message += get_diagnostic_level_name(diagnostic.get_level());
    rendered_message += ": ";
    rendered_message += diagnostic.get_message();
    return rendered_message;
}

std::optional<DiagnosticCliSourceExcerpt>
DiagnosticFormatter::format_source_excerpt(const Diagnostic &diagnostic) {
    if (!should_render_source_excerpt(diagnostic)) {
        return std::nullopt;
    }

    const SourceSpan &source_span = diagnostic.get_source_span();
    if (source_span.get_line_begin() <= 0 ||
        source_span.get_line_begin() != source_span.get_line_end()) {
        return std::nullopt;
    }

    const SourceFile *source_file = source_span.get_file();
    if (source_file == nullptr || source_file->empty()) {
        return std::nullopt;
    }

    const std::optional<std::string> raw_line =
        source_file->get_line_text(source_span.get_line_begin());
    if (!raw_line.has_value()) {
        return std::nullopt;
    }

    const std::string expanded_line = expand_tabs(*raw_line);
    const std::size_t caret_start =
        visual_column_for_source_column(*raw_line, source_span.get_col_begin());
    const int raw_highlight_width =
        source_span.get_col_end() >= source_span.get_col_begin()
            ? source_span.get_col_end() - source_span.get_col_begin() + 1
            : 1;
    const std::size_t caret_end =
        visual_column_for_source_column(*raw_line,
                                        source_span.get_col_begin() +
                                            raw_highlight_width);
    const std::size_t highlight_width =
        std::max<std::size_t>(1, caret_end - caret_start);

    DiagnosticCliSourceExcerpt excerpt;
    excerpt.source_line = "  " + expanded_line;
    excerpt.caret_line =
        "  " + std::string(caret_start > 1 ? caret_start - 1 : 0, ' ') + "^";
    if (highlight_width > 1) {
        excerpt.caret_line += std::string(highlight_width - 1, '~');
    }

    return excerpt;
}

std::string DiagnosticFormatter::format_diagnostic_for_cli(
    const Diagnostic &diagnostic) {
    std::string rendered_message = format_diagnostic_header(diagnostic);
    const std::optional<DiagnosticCliSourceExcerpt> excerpt =
        format_source_excerpt(diagnostic);
    if (!excerpt.has_value()) {
        return rendered_message;
    }

    rendered_message += "\n";
    rendered_message += excerpt->source_line;
    rendered_message += "\n";
    rendered_message += excerpt->caret_line;
    return rendered_message;
}

void DiagnosticFormatter::print_diagnostics(
    std::ostream &os, const DiagnosticEngine &diagnostic_engine) {
    for (const Diagnostic &diagnostic : diagnostic_engine.get_diagnostics()) {
        os << format_diagnostic_for_cli(diagnostic) << '\n';
    }
}

} // namespace sysycc
