#include "frontend/preprocess/detail/conditional/builtin_probe_evaluator.hpp"

#include <cctype>
#include <string>

#include "frontend/preprocess/detail/include_resolver.hpp"

namespace sysycc::preprocess::detail {

namespace {

void skip_spaces(const std::string &expression, std::size_t &index) {
    while (index < expression.size() &&
           std::isspace(static_cast<unsigned char>(expression[index])) != 0) {
        ++index;
    }
}

bool is_identifier_continue(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool consume_keyword(const std::string &expression, std::size_t &index,
                     const std::string &keyword) {
    skip_spaces(expression, index);
    if (expression.compare(index, keyword.size(), keyword) != 0) {
        return false;
    }

    const std::size_t end = index + keyword.size();
    if (end < expression.size() && is_identifier_continue(expression[end])) {
        return false;
    }

    index = end;
    return true;
}

bool parse_identifier(const std::string &expression, std::size_t &index,
                      std::string &identifier) {
    skip_spaces(expression, index);
    if (index >= expression.size()) {
        return false;
    }

    const char ch = expression[index];
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
        return false;
    }

    const std::size_t begin = index;
    ++index;
    while (index < expression.size() &&
           is_identifier_continue(expression[index])) {
        ++index;
    }

    identifier = expression.substr(begin, index - begin);
    return true;
}

std::string trim_copy(const std::string &text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

PassResult parse_include_operand(const std::string &expression,
                                 std::size_t &index,
                                 const std::string &probe_name,
                                 std::string &include_token) {
    skip_spaces(expression, index);
    if (index >= expression.size() || expression[index] != '(') {
        return PassResult::Failure("missing '(' after " + probe_name +
                                   " in #if expression");
    }
    ++index;

    skip_spaces(expression, index);
    if (index >= expression.size()) {
        return PassResult::Failure("missing header name in " + probe_name +
                                   " expression");
    }

    const char opening = expression[index];
    if (opening != '"' && opening != '<') {
        return PassResult::Failure("invalid " + probe_name +
                                   " operand in #if expression");
    }

    const char closing = opening == '"' ? '"' : '>';
    const std::size_t start_index = index;
    ++index;
    while (index < expression.size() && expression[index] != closing) {
        ++index;
    }

    if (index >= expression.size()) {
        return PassResult::Failure("missing header terminator in " + probe_name +
                                   " expression");
    }

    include_token = expression.substr(start_index, index - start_index + 1);
    ++index;

    skip_spaces(expression, index);
    if (index >= expression.size() || expression[index] != ')') {
        return PassResult::Failure("missing ')' after " + probe_name +
                                   " operand");
    }
    ++index;

    return PassResult::Success();
}

} // namespace

BuiltinProbeEvaluator::BuiltinProbeEvaluator()
    : include_resolver_(new IncludeResolver()) {}

BuiltinProbeEvaluator::~BuiltinProbeEvaluator() { delete include_resolver_; }

PassResult BuiltinProbeEvaluator::try_evaluate(
    const std::string &expression, std::size_t &index,
    const MacroTable &macro_table, const std::string &current_file_path,
    const std::vector<std::string> &include_directories,
    const std::vector<std::string> &quote_include_directories,
    const std::vector<std::string> &system_include_directories,
    const DialectManager &dialect_manager, long long &value,
    bool &handled) const {
    handled = false;
    const std::size_t original_index = index;

    std::string identifier;
    if (consume_keyword(expression, index, "defined")) {
        skip_spaces(expression, index);
        if (index < expression.size() && expression[index] == '(') {
            ++index;
            if (!parse_identifier(expression, index, identifier)) {
                return PassResult::Failure(
                    "invalid defined() operand in #if expression");
            }
            skip_spaces(expression, index);
            if (index >= expression.size() || expression[index] != ')') {
                return PassResult::Failure(
                    "missing ')' after defined() operand");
            }
            ++index;
        } else if (!parse_identifier(expression, index, identifier)) {
            return PassResult::Failure(
                "invalid defined operand in #if expression");
        }

        value = macro_table.has_macro(identifier) ? 1 : 0;
        handled = true;
        return PassResult::Success();
    }

    const bool include_next =
        consume_keyword(expression, index, "__has_include_next");
    if (include_next || consume_keyword(expression, index, "__has_include")) {
        if (!dialect_manager.get_preprocess_feature_registry().has_feature(
                PreprocessFeature::HasIncludeFamily)) {
            index = original_index;
            return PassResult::Success();
        }

        const std::string probe_name =
            include_next ? "__has_include_next" : "__has_include";
        std::string include_token;
        PassResult parse_result = parse_include_operand(
            expression, index, probe_name, include_token);
        if (!parse_result.ok) {
            return parse_result;
        }

        std::string resolved_file_path;
        PassResult resolve_result = include_resolver_->resolve_include(
            include_token, current_file_path, include_directories,
            quote_include_directories, system_include_directories, include_next,
            include_token, resolved_file_path);
        value = resolve_result.ok ? 1 : 0;
        handled = true;
        return PassResult::Success();
    }

    index = original_index;
    if (!dialect_manager.get_preprocess_feature_registry().has_feature(
            PreprocessFeature::ClangBuiltinProbes)) {
        return PassResult::Success();
    }

    PassResult extension_result = non_standard_extension_manager_.try_evaluate(
        expression, index,
        dialect_manager.get_preprocess_probe_handler_registry(), value,
        handled);
    if (!extension_result.ok || handled) {
        return extension_result;
    }

    index = original_index;
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
