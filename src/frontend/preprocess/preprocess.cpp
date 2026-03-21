#include "frontend/preprocess/preprocess.hpp"

#include "common/diagnostic/diagnostic_engine.hpp"
#include "frontend/preprocess/detail/preprocess_session.hpp"

#include <cctype>
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

SourceSpan parse_path_line_span(CompilerContext &context,
                                const std::string &message_line) {
    const std::size_t first_colon = message_line.find(':');
    if (first_colon == std::string::npos || first_colon + 1 >= message_line.size()) {
        return {};
    }

    std::size_t index = first_colon + 1;
    while (index < message_line.size() &&
           std::isdigit(static_cast<unsigned char>(message_line[index])) != 0) {
        ++index;
    }

    if (index == first_colon + 1 || index >= message_line.size() ||
        message_line[index] != ':') {
        return {};
    }

    const std::string file_path = message_line.substr(0, first_colon);
    const int line = std::stoi(
        message_line.substr(first_colon + 1, index - first_colon - 1));
    const SourceFile *source_file =
        context.get_source_location_service().get_source_manager().get_source_file(
            file_path);
    const SourcePosition position(source_file, line, 1);
    return SourceSpan(position, position);
}

void emit_preprocess_diagnostics(CompilerContext &context,
                                 const std::string &message) {
    const std::vector<std::string> lines = split_lines(message);
    if (lines.empty()) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Preprocess,
                                                  message);
        return;
    }

    context.get_diagnostic_engine().add_error(DiagnosticStage::Preprocess,
                                              lines.front(),
                                              parse_path_line_span(context,
                                                                   lines.front()));
    for (std::size_t index = 1; index < lines.size(); ++index) {
        context.get_diagnostic_engine().add_note(DiagnosticStage::Preprocess,
                                                 lines[index],
                                                 parse_path_line_span(context,
                                                                      lines[index]));
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
