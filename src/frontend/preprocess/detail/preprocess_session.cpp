#include "frontend/preprocess/detail/preprocess_session.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace sysycc::preprocess::detail {

namespace {

bool ends_with_whitespace(const std::string &text) {
    if (text.empty()) {
        return false;
    }

    return std::isspace(static_cast<unsigned char>(text.back())) != 0;
}

bool ends_with_line_continuation(const std::string &text) {
    return !text.empty() && text.back() == '\\';
}

void append_comment_placeholder(std::string &text) {
    if (text.empty() || ends_with_whitespace(text)) {
        return;
    }

    text.push_back(' ');
}

std::string build_logical_line(const std::vector<std::string> &lines,
                               std::size_t &index) {
    std::string logical_line = lines[index];
    while (ends_with_line_continuation(logical_line) &&
           index + 1 < lines.size()) {
        logical_line.pop_back();
        ++index;
        logical_line += lines[index];
    }
    return logical_line;
}

bool has_line_location_prefix(const std::string &message) {
    const std::size_t first_colon = message.find(':');
    if (first_colon == std::string::npos || first_colon + 1 >= message.size()) {
        return false;
    }

    std::size_t second_colon = first_colon + 1;
    while (second_colon < message.size() &&
           std::isdigit(static_cast<unsigned char>(message[second_colon])) !=
               0) {
        ++second_colon;
    }

    return second_colon > first_colon + 1 && second_colon < message.size() &&
           message[second_colon] == ':';
}

PassResult annotate_preprocess_error(const PassResult &result,
                                     const SourceMapper &source_mapper,
                                     int line_number) {
    if (result.ok) {
        return result;
    }

    std::string message = result.message;
    if (!has_line_location_prefix(message)) {
        const SourcePosition position =
            source_mapper.get_logical_position(line_number, 1);
        const SourceFile *source_file = position.get_file();
        const std::string file_path =
            source_file != nullptr ? source_file->get_path() : std::string();
        if (!file_path.empty()) {
            message = file_path + ":" + std::to_string(position.get_line()) +
                      ": " + message;
        }
    }

    if (message.find("\nincluded from ") == std::string::npos) {
        const std::vector<SourcePosition> include_trace =
            source_mapper.get_include_trace();
        for (const SourcePosition &position : include_trace) {
            const SourceFile *source_file = position.get_file();
            if (source_file == nullptr || source_file->empty()) {
                continue;
            }

            message += "\nincluded from " + source_file->get_path() + ":" +
                       std::to_string(position.get_line());
        }
    }

    return PassResult::Failure(message);
}

} // namespace

PreprocessSession::PreprocessSession(CompilerContext &context)
    : preprocess_context_(context),
      directive_executor_(preprocess_context_, constant_expression_evaluator_,
                          macro_expander_, include_resolver_) {}

PassResult PreprocessSession::Run() {
    preprocess_context_.clear();
    preprocess_context_.initialize_predefined_macros();
    preprocess_context_.get_compiler_context().clear_preprocessed_line_map();

    PassResult result = preprocess_file(preprocess_context_.get_input_file());
    if (!result.ok) {
        preprocess_context_.set_preprocessed_file_path("");
        return result;
    }

    std::string output_file_path;
    PassResult write_result = write_preprocessed_file(output_file_path);
    if (!write_result.ok) {
        preprocess_context_.set_preprocessed_file_path("");
        return write_result;
    }

    preprocess_context_.set_preprocessed_file_path(std::move(output_file_path));
    preprocess_context_.set_preprocessed_line_map(
        preprocess_context_.get_runtime().get_output_line_map());
    return PassResult::Success();
}

PassResult
PreprocessSession::strip_comments_from_line(const std::string &line,
                                            std::string &stripped_line) {
    stripped_line.clear();

    bool in_string_literal = false;
    bool in_char_literal = false;
    bool escaping = false;
    std::size_t index = 0;
    while (index < line.size()) {
        if (preprocess_context_.get_runtime().get_in_block_comment()) {
            const std::size_t comment_end = line.find("*/", index);
            if (comment_end == std::string::npos) {
                return PassResult::Success();
            }

            preprocess_context_.get_runtime().set_in_block_comment(false);
            index = comment_end + 2;
            continue;
        }

        const char current = line[index];
        if (in_string_literal) {
            stripped_line.push_back(current);
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (current == '"') {
                in_string_literal = false;
            }
            ++index;
            continue;
        }

        if (in_char_literal) {
            stripped_line.push_back(current);
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (current == '\'') {
                in_char_literal = false;
            }
            ++index;
            continue;
        }

        if (current == '"') {
            in_string_literal = true;
            stripped_line.push_back(current);
            ++index;
            continue;
        }

        if (current == '\'') {
            in_char_literal = true;
            stripped_line.push_back(current);
            ++index;
            continue;
        }

        if (index + 1 < line.size() && line[index] == '/' &&
            line[index + 1] == '/') {
            break;
        }

        if (index + 1 < line.size() && line[index] == '/' &&
            line[index + 1] == '*') {
            append_comment_placeholder(stripped_line);
            preprocess_context_.get_runtime().set_in_block_comment(true);
            index += 2;
            continue;
        }

        stripped_line.push_back(current);
        ++index;
    }

    return PassResult::Success();
}

PassResult PreprocessSession::handle_non_directive_line(const std::string &line,
                                                        int line_number) {
    if (preprocess_context_.get_conditional_stack().is_in_active_region()) {
        preprocess_context_.get_runtime().append_output_line(
            macro_expander_.expand_line(line,
                                        preprocess_context_.get_macro_table()),
            preprocess_context_.get_source_mapper().get_logical_position(
                line_number, 1));
    }

    return PassResult::Success();
}

PassResult
PreprocessSession::process_line(const std::string &line, int line_number,
                                const std::string &current_file_path) {
    std::string stripped_line;
    PassResult strip_result =
        strip_comments_from_line(line, stripped_line);
    if (!strip_result.ok) {
        return annotate_preprocess_error(
            strip_result, preprocess_context_.get_source_mapper(), line_number);
    }

    if (!directive_parser_.is_directive(stripped_line)) {
        return handle_non_directive_line(stripped_line, line_number);
    }

    Directive directive;
    const bool validate_directive_syntax =
        preprocess_context_.get_conditional_stack().is_in_active_region();
    PassResult parse_result = directive_parser_.parse(
        stripped_line, directive, validate_directive_syntax);
    if (!parse_result.ok) {
        return annotate_preprocess_error(
            parse_result, preprocess_context_.get_source_mapper(), line_number);
    }
    PassResult execute_result = directive_executor_.execute(
        stripped_line, line_number, directive, current_file_path,
        [this](const std::string &file_path, SourcePosition include_position) {
            return preprocess_file(file_path, include_position);
        });
    return annotate_preprocess_error(execute_result,
                                     preprocess_context_.get_source_mapper(),
                                     line_number);
}

PassResult PreprocessSession::write_preprocessed_file(
    std::string &output_file_path) const {
    const std::filesystem::path output_dir("build/intermediate_results");
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path input_path(preprocess_context_.get_input_file());
    const std::filesystem::path output_path =
        output_dir / (input_path.stem().string() + ".preprocessed.sy");

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        return PassResult::Failure(
            "failed to open preprocessed output file in intermediate_results");
    }

    ofs << preprocess_context_.get_runtime().build_output_text();
    if (!ofs.good()) {
        return PassResult::Failure("failed to write preprocessed output file");
    }

    output_file_path = output_path.string();
    return PassResult::Success();
}

PassResult PreprocessSession::preprocess_file(const std::string &file_path,
                                             SourcePosition include_position) {
    if (preprocess_context_.get_runtime().should_skip_file(file_path)) {
        return PassResult::Success();
    }

    // Reject recursive re-entry before descending into the next include.
    if (preprocess_context_.get_source_mapper().has_file_in_stack(file_path)) {
        return PassResult::Failure("include cycle detected: " + file_path);
    }

    std::vector<std::string> lines;
    PassResult load_result = file_loader_.read_lines(file_path, lines);
    if (!load_result.ok) {
        return load_result;
    }

    const std::size_t conditional_frame_count_before =
        preprocess_context_.get_conditional_stack().get_frame_count();
    preprocess_context_.get_source_mapper().push_file(file_path,
                                                      include_position);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const int line_number = static_cast<int>(index + 1);
        const std::string logical_line = build_logical_line(lines, index);
        PassResult result =
            process_line(logical_line, line_number, file_path);
        if (!result.ok) {
            preprocess_context_.get_source_mapper().pop_file();
            return result;
        }
    }

    preprocess_context_.get_source_mapper().pop_file();
    if (preprocess_context_.get_runtime().get_in_block_comment()) {
        return PassResult::Failure(
            "unterminated block comment in preprocessor");
    }
    if (preprocess_context_.get_conditional_stack().get_frame_count() !=
        conditional_frame_count_before) {
        return PassResult::Failure("missing #endif directive");
    }

    preprocess_context_.get_runtime().mark_file_processed(file_path);
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
