#include "frontend/preprocess/detail/preprocess_session.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/intermediate_results_path.hpp"

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

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_valid_macro_name(const std::string &name) {
    if (name.empty() || !is_identifier_start(name.front())) {
        return false;
    }
    for (char ch : name) {
        if (!is_identifier_char(ch)) {
            return false;
        }
    }
    return true;
}

bool has_identifier_boundary(const std::string &text, std::size_t begin,
                             std::size_t end) {
    const bool left_ok =
        begin == 0 || !is_identifier_char(text[begin - 1]);
    const bool right_ok =
        end >= text.size() || !is_identifier_char(text[end]);
    return left_ok && right_ok;
}

std::size_t find_pragma_operator_end(const std::string &text,
                                     std::size_t begin) {
    constexpr std::string_view pragma_operator = "_Pragma";
    std::size_t index = begin + pragma_operator.size();
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    if (index >= text.size() || text[index] != '(') {
        return std::string::npos;
    }

    bool in_string_literal = false;
    bool in_char_literal = false;
    bool escaping = false;
    int paren_depth = 0;
    for (; index < text.size(); ++index) {
        const char current = text[index];
        if (in_string_literal) {
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (current == '"') {
                in_string_literal = false;
            }
            continue;
        }

        if (in_char_literal) {
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (current == '\'') {
                in_char_literal = false;
            }
            continue;
        }

        if (current == '"') {
            in_string_literal = true;
            continue;
        }

        if (current == '\'') {
            in_char_literal = true;
            continue;
        }

        if (current == '(') {
            ++paren_depth;
            continue;
        }

        if (current == ')') {
            --paren_depth;
            if (paren_depth == 0) {
                return index + 1;
            }
        }
    }

    return std::string::npos;
}

std::string strip_pragma_operators(const std::string &text) {
    constexpr std::string_view pragma_operator = "_Pragma";
    std::string output;
    bool in_string_literal = false;
    bool in_char_literal = false;
    bool escaping = false;
    std::size_t index = 0;
    while (index < text.size()) {
        const char current = text[index];
        if (in_string_literal) {
            output.push_back(current);
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
            output.push_back(current);
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
            output.push_back(current);
            ++index;
            continue;
        }

        if (current == '\'') {
            in_char_literal = true;
            output.push_back(current);
            ++index;
            continue;
        }

        if (text.compare(index, pragma_operator.size(), pragma_operator) == 0 &&
            has_identifier_boundary(text, index,
                                    index + pragma_operator.size())) {
            const std::size_t end = find_pragma_operator_end(text, index);
            if (end != std::string::npos) {
                index = end;
                continue;
            }
        }

        output.push_back(current);
        ++index;
    }

    return output;
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

bool needs_function_macro_continuation(const std::string &line,
                                       const MacroTable &macro_table) {
    std::size_t first_non_space = 0;
    while (first_non_space < line.size() &&
           std::isspace(static_cast<unsigned char>(line[first_non_space])) !=
               0) {
        ++first_non_space;
    }
    if (first_non_space < line.size() && line[first_non_space] == '#') {
        return false;
    }

    bool in_string_literal = false;
    bool in_char_literal = false;
    bool escaping = false;
    int active_macro_depth = 0;
    std::size_t index = 0;
    while (index < line.size()) {
        const char current = line[index];
        if (in_string_literal) {
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
            ++index;
            continue;
        }

        if (current == '\'') {
            in_char_literal = true;
            ++index;
            continue;
        }

        if (active_macro_depth > 0) {
            if (current == '(') {
                ++active_macro_depth;
            } else if (current == ')') {
                --active_macro_depth;
            }
            ++index;
            continue;
        }

        if (!is_identifier_start(current)) {
            ++index;
            continue;
        }

        const std::size_t identifier_begin = index;
        ++index;
        while (index < line.size() && is_identifier_char(line[index])) {
            ++index;
        }

        const std::string identifier =
            line.substr(identifier_begin, index - identifier_begin);
        const MacroDefinition *definition =
            macro_table.get_macro_definition(identifier);
        if (definition == nullptr || !definition->get_is_function_like()) {
            continue;
        }

        std::size_t next_index = index;
        while (next_index < line.size() &&
               std::isspace(static_cast<unsigned char>(line[next_index])) !=
                   0) {
            ++next_index;
        }
        if (next_index >= line.size()) {
            return true;
        }
        if (line[next_index] == '(') {
            active_macro_depth = 1;
            index = next_index + 1;
        }
    }

    return active_macro_depth > 0;
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

    PassResult macro_option_result = apply_command_line_macro_options();
    if (!macro_option_result.ok) {
        preprocess_context_.set_preprocessed_file_path("");
        return macro_option_result;
    }

    PassResult forced_include_result = preprocess_forced_includes();
    if (!forced_include_result.ok) {
        preprocess_context_.set_preprocessed_file_path("");
        return forced_include_result;
    }

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

PassResult PreprocessSession::apply_command_line_macro_options() {
    for (const CommandLineMacroOption &macro_option :
         preprocess_context_.get_command_line_macro_options()) {
        if (!is_valid_macro_name(macro_option.get_name())) {
            return PassResult::Failure("invalid command-line macro name: " +
                                       macro_option.get_name());
        }

        if (macro_option.get_action_kind() ==
            CommandLineMacroActionKind::Undefine) {
            preprocess_context_.get_macro_table().undefine_macro(
                macro_option.get_name());
            continue;
        }

        const std::string replacement =
            macro_option.has_replacement() ? macro_option.get_replacement() : "1";
        PassResult define_result = preprocess_context_.get_macro_table().define_macro(
            MacroDefinition(macro_option.get_name(), replacement), true);
        if (!define_result.ok) {
            return define_result;
        }
    }

    return PassResult::Success();
}

PassResult PreprocessSession::preprocess_forced_includes() {
    for (const std::string &forced_include :
         preprocess_context_.get_forced_include_files()) {
        std::string resolved_file_path;
        const std::filesystem::path include_path(forced_include);
        if (std::filesystem::exists(include_path)) {
            resolved_file_path = include_path.lexically_normal().string();
        } else {
            for (const std::string &quote_include_directory :
                 preprocess_context_.get_quote_include_directories()) {
                const std::filesystem::path candidate =
                    std::filesystem::path(quote_include_directory) /
                    forced_include;
                if (!std::filesystem::exists(candidate)) {
                    continue;
                }
                resolved_file_path = candidate.lexically_normal().string();
                break;
            }

            if (!resolved_file_path.empty()) {
                PassResult result = preprocess_file(resolved_file_path);
                if (!result.ok) {
                    return result;
                }
                continue;
            }

            for (const std::string &include_directory :
                 preprocess_context_.get_include_directories()) {
                const std::filesystem::path candidate =
                    std::filesystem::path(include_directory) / forced_include;
                if (!std::filesystem::exists(candidate)) {
                    continue;
                }
                resolved_file_path = candidate.lexically_normal().string();
                break;
            }

            if (resolved_file_path.empty()) {
                for (const std::string &system_include_directory :
                     preprocess_context_.get_system_include_directories()) {
                    const std::filesystem::path candidate =
                        std::filesystem::path(system_include_directory) /
                        forced_include;
                    if (!std::filesystem::exists(candidate)) {
                        continue;
                    }
                    resolved_file_path = candidate.lexically_normal().string();
                    break;
                }
            }
        }

        if (resolved_file_path.empty()) {
            return PassResult::Failure("failed to resolve forced include file: " +
                                       forced_include);
        }

        PassResult result = preprocess_file(resolved_file_path);
        if (!result.ok) {
            return result;
        }
    }

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
        const std::string expanded_line = macro_expander_.expand_line(
            line, preprocess_context_.get_macro_table());
        preprocess_context_.get_runtime().append_output_line(
            strip_pragma_operators(expanded_line),
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
    const std::filesystem::path output_dir = sysycc::get_intermediate_results_dir();
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
        std::string logical_line = build_logical_line(lines, index);
        while (index + 1 < lines.size() &&
               needs_function_macro_continuation(
                   logical_line, preprocess_context_.get_macro_table())) {
            ++index;
            std::size_t continuation_index = index;
            logical_line += " ";
            logical_line += build_logical_line(lines, continuation_index);
            index = continuation_index;
        }
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
