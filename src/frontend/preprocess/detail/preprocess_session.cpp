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

void append_comment_placeholder(std::string &text) {
    if (text.empty() || ends_with_whitespace(text)) {
        return;
    }

    text.push_back(' ');
}

} // namespace

PreprocessSession::PreprocessSession(CompilerContext &context)
    : context_(context) {}

PassResult PreprocessSession::Run() {
    runtime_.clear();
    macro_table_.clear();
    conditional_stack_.clear();

    PassResult result = preprocess_file(context_.get_input_file());
    if (!result.ok) {
        context_.set_preprocessed_file_path("");
        return result;
    }

    std::string output_file_path;
    PassResult write_result = write_preprocessed_file(output_file_path);
    if (!write_result.ok) {
        context_.set_preprocessed_file_path("");
        return write_result;
    }

    context_.set_preprocessed_file_path(std::move(output_file_path));
    return PassResult::Success();
}

PassResult PreprocessSession::evaluate_if_condition(const Directive &directive,
                                                    bool &condition) const {
    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure("invalid " + directive.get_keyword() +
                                   " directive: missing condition");
    }

    long long value = 0;
    PassResult evaluate_result = constant_expression_evaluator_.evaluate(
        arguments[0], macro_table_, value);
    if (!evaluate_result.ok) {
        return evaluate_result;
    }

    condition = value != 0;
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
        if (runtime_.get_in_block_comment()) {
            const std::size_t comment_end = line.find("*/", index);
            if (comment_end == std::string::npos) {
                return PassResult::Success();
            }

            runtime_.set_in_block_comment(false);
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
            runtime_.set_in_block_comment(true);
            index += 2;
            continue;
        }

        stripped_line.push_back(current);
        ++index;
    }

    return PassResult::Success();
}

PassResult
PreprocessSession::handle_non_directive_line(const std::string &line) {
    if (conditional_stack_.is_in_active_region()) {
        runtime_.append_output_line(
            macro_expander_.expand_line(line, macro_table_));
    }

    return PassResult::Success();
}

PassResult
PreprocessSession::handle_conditional_directive(const Directive &directive) {
    const std::vector<std::string> &arguments = directive.get_arguments();
    bool condition = false;

    switch (directive.get_kind()) {
    case DirectiveKind::Ifdef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition = macro_table_.has_macro(arguments[0]);
        return conditional_stack_.push_if(condition);
    case DirectiveKind::Ifndef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition = !macro_table_.has_macro(arguments[0]);
        return conditional_stack_.push_if(condition);
    case DirectiveKind::If: {
        PassResult condition_result =
            evaluate_if_condition(directive, condition);
        if (!condition_result.ok) {
            return condition_result;
        }
        return conditional_stack_.push_if(condition);
    }
    case DirectiveKind::Elif: {
        PassResult condition_result =
            evaluate_if_condition(directive, condition);
        if (!condition_result.ok) {
            return condition_result;
        }
        return conditional_stack_.handle_elif(condition);
    }
    case DirectiveKind::Else:
        return conditional_stack_.handle_else();
    case DirectiveKind::Endif:
        return conditional_stack_.handle_endif();
    default:
        return PassResult::Failure("unexpected conditional directive kind");
    }
}

PassResult PreprocessSession::handle_include_directive(
    const std::string &line, const Directive &directive,
    const std::string &current_file_path) {
    if (directive.get_kind() != DirectiveKind::Include) {
        return PassResult::Failure("unexpected include directive kind");
    }

    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure(
            "invalid #include directive: missing include path");
    }

    const std::string expanded_include_token =
        macro_expander_.expand_line(arguments[0], macro_table_);

    std::string resolved_file_path;
    PassResult resolve_result = include_resolver_.resolve_include(
        line, current_file_path, context_.get_include_directories(),
        context_.get_system_include_directories(),
        expanded_include_token, resolved_file_path);
    if (!resolve_result.ok) {
        return resolve_result;
    }

    return preprocess_file(resolved_file_path);
}

PassResult PreprocessSession::handle_macro_directive(
    const std::string &line, int line_number, const Directive &directive,
    const std::string &current_file_path) {
    const std::vector<std::string> &arguments = directive.get_arguments();

    if (directive.get_kind() == DirectiveKind::Define) {
        if (arguments.empty()) {
            return PassResult::Failure(
                "invalid #define directive: missing macro name");
        }

        std::string replacement;
        if (arguments.size() > 1) {
            replacement = arguments[1];
        }

        return macro_table_.define_macro(MacroDefinition(
            arguments[0], replacement, directive.get_is_function_like_macro(),
            directive.get_macro_parameters(),
            SourceSpan(SourcePosition(get_source_file(current_file_path),
                                      line_number, 1),
                       SourcePosition(get_source_file(current_file_path),
                                      line_number,
                                      static_cast<int>(line.size())))));
    }

    if (directive.get_kind() == DirectiveKind::Undef) {
        if (arguments.empty()) {
            return PassResult::Failure(
                "invalid #undef directive: missing macro name");
        }

        macro_table_.undefine_macro(arguments[0]);
        return PassResult::Success();
    }

    return PassResult::Failure("unexpected macro directive kind");
}

PassResult
PreprocessSession::process_line(const std::string &line, int line_number,
                                const std::string &current_file_path) {
    std::string stripped_line;
    PassResult strip_result =
        strip_comments_from_line(line, stripped_line);
    if (!strip_result.ok) {
        return strip_result;
    }

    if (!directive_parser_.is_directive(stripped_line)) {
        return handle_non_directive_line(stripped_line);
    }

    Directive directive;
    PassResult parse_result =
        directive_parser_.parse(stripped_line, directive);
    if (!parse_result.ok) {
        return parse_result;
    }

    // Route directives by kind directly instead of relying on sentinel error
    // strings from helper functions.
    switch (directive.get_kind()) {
    case DirectiveKind::Ifdef:
    case DirectiveKind::Ifndef:
    case DirectiveKind::If:
    case DirectiveKind::Elif:
    case DirectiveKind::Else:
    case DirectiveKind::Endif:
        return handle_conditional_directive(directive);
    default:
        break;
    }

    if (!conditional_stack_.is_in_active_region()) {
        return PassResult::Success();
    }

    switch (directive.get_kind()) {
    case DirectiveKind::Include:
        return handle_include_directive(stripped_line, directive,
                                        current_file_path);
    case DirectiveKind::Define:
    case DirectiveKind::Undef:
        return handle_macro_directive(stripped_line, line_number, directive,
                                      current_file_path);
    default:
        break;
    }

    return PassResult::Failure("unsupported preprocess directive: " +
                               directive.get_keyword());
}

PassResult PreprocessSession::write_preprocessed_file(
    std::string &output_file_path) const {
    const std::filesystem::path output_dir("build/intermediate_results");
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path input_path(context_.get_input_file());
    const std::filesystem::path output_path =
        output_dir / (input_path.stem().string() + ".preprocessed.sy");

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        return PassResult::Failure(
            "failed to open preprocessed output file in intermediate_results");
    }

    ofs << runtime_.build_output_text();
    if (!ofs.good()) {
        return PassResult::Failure("failed to write preprocessed output file");
    }

    output_file_path = output_path.string();
    return PassResult::Success();
}

PassResult PreprocessSession::preprocess_file(const std::string &file_path) {
    // Reject recursive re-entry before descending into the next include.
    if (runtime_.has_file_in_stack(file_path)) {
        return PassResult::Failure("include cycle detected: " + file_path);
    }

    std::vector<std::string> lines;
    PassResult load_result = file_loader_.read_lines(file_path, lines);
    if (!load_result.ok) {
        return load_result;
    }

    const std::size_t conditional_frame_count_before =
        conditional_stack_.get_frame_count();
    runtime_.push_file(file_path);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        PassResult result =
            process_line(lines[index], static_cast<int>(index + 1), file_path);
        if (!result.ok) {
            runtime_.pop_file();
            return result;
        }
    }

    runtime_.pop_file();
    if (runtime_.get_in_block_comment()) {
        return PassResult::Failure(
            "unterminated block comment in preprocessor");
    }
    if (conditional_stack_.get_frame_count() !=
        conditional_frame_count_before) {
        return PassResult::Failure("missing #endif directive");
    }

    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
