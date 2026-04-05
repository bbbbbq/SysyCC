#include "frontend/preprocess/preprocess.hpp"

#include "common/diagnostic/diagnostic_engine.hpp"
#include "frontend/preprocess/detail/preprocess_session.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace sysycc {

namespace {

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::string current_line;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current_line);
            current_line.clear();
            continue;
        }
        current_line.push_back(ch);
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    return lines;
}

struct ParsedPathLinePrefix {
    std::string file_path;
    int line = 0;
    std::size_t message_begin = std::string::npos;
};

std::optional<ParsedPathLinePrefix>
parse_prefixed_path_line(const std::string &message_line) {
    std::optional<ParsedPathLinePrefix> parsed;
    for (std::size_t colon_index = 0; colon_index < message_line.size();
         ++colon_index) {
        if (message_line[colon_index] != ':' ||
            colon_index + 1 >= message_line.size()) {
            continue;
        }

        std::size_t line_begin = colon_index + 1;
        std::size_t line_end = line_begin;
        while (line_end < message_line.size() &&
               std::isdigit(static_cast<unsigned char>(message_line[line_end])) !=
                   0) {
            ++line_end;
        }

        if (line_end == line_begin || line_end >= message_line.size() ||
            message_line[line_end] != ':') {
            continue;
        }

        ParsedPathLinePrefix candidate;
        candidate.file_path = message_line.substr(0, colon_index);
        candidate.line = std::stoi(
            message_line.substr(line_begin, line_end - line_begin));
        candidate.message_begin = line_end + 1;
        if (candidate.message_begin < message_line.size() &&
            message_line[candidate.message_begin] == ' ') {
            ++candidate.message_begin;
        }
        parsed = std::move(candidate);
    }

    return parsed;
}

SourceSpan parse_path_line_span(CompilerContext &context,
                                const std::string &message_line) {
    const std::optional<ParsedPathLinePrefix> parsed =
        parse_prefixed_path_line(message_line);
    if (!parsed.has_value()) {
        return {};
    }

    const SourceFile *source_file =
        context.get_source_location_service().get_source_manager().get_source_file(
            parsed->file_path);
    const SourcePosition position(source_file, parsed->line, 1);
    return SourceSpan(position, position);
}

std::string strip_prefixed_path_line_message(const std::string &message_line) {
    const std::optional<ParsedPathLinePrefix> parsed =
        parse_prefixed_path_line(message_line);
    if (!parsed.has_value()) {
        return message_line;
    }
    return message_line.substr(parsed->message_begin);
}

SourceSpan parse_included_from_span(CompilerContext &context,
                                    const std::string &message_line) {
    constexpr const char *k_prefix = "included from ";
    if (message_line.rfind(k_prefix, 0) != 0) {
        return {};
    }

    const std::string location = message_line.substr(std::string(k_prefix).size());
    const std::size_t last_colon = location.rfind(':');
    if (last_colon == std::string::npos || last_colon + 1 >= location.size()) {
        return {};
    }

    const std::string file_path = location.substr(0, last_colon);
    const std::string line_text = location.substr(last_colon + 1);
    if (file_path.empty() || line_text.empty()) {
        return {};
    }

    for (char ch : line_text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return {};
        }
    }

    const int line = std::stoi(line_text);
    const SourceFile *source_file =
        context.get_source_location_service().get_source_manager().get_source_file(
            file_path);
    const SourcePosition position(source_file, line, 1);
    return SourceSpan(position, position);
}

std::string normalize_preprocess_note_message(const std::string &message_line) {
    if (message_line.rfind("included from ", 0) == 0) {
        return "included from here";
    }
    return strip_prefixed_path_line_message(message_line);
}

void emit_preprocess_diagnostics(CompilerContext &context,
                                 const std::string &message) {
    const std::vector<std::string> lines = split_lines(message);
    if (lines.empty()) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Preprocess,
                                                  message);
        return;
    }

    context.get_diagnostic_engine().add_error(
        DiagnosticStage::Preprocess,
        strip_prefixed_path_line_message(lines.front()),
        parse_path_line_span(context, lines.front()));
    for (std::size_t index = 1; index < lines.size(); ++index) {
        SourceSpan note_span = parse_included_from_span(context, lines[index]);
        if (note_span.empty()) {
            note_span = parse_path_line_span(context, lines[index]);
        }
        context.get_diagnostic_engine().add_note(DiagnosticStage::Preprocess,
                                                 normalize_preprocess_note_message(
                                                     lines[index]),
                                                 note_span);
    }
}

} // namespace

PassKind PreprocessPass::Kind() const { return PassKind::Preprocess; }

const char *PreprocessPass::Name() const { return "PreprocessPass"; }

PassResult PreprocessPass::Run(CompilerContext &context) {
    preprocess::detail::PreprocessSession session(context);
    PassResult result = session.Run();
    if (!result.ok) {
        emit_preprocess_diagnostics(context, result.message);
    }
    return result;
}

} // namespace sysycc
