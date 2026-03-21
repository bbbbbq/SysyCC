#include "frontend/preprocess/detail/conditional/clang_extension_provider.hpp"

#include <cctype>
#include <string>

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

PassResult parse_parenthesized_operand(const std::string &expression,
                                       std::size_t &index,
                                       const std::string &probe_name,
                                       std::string &operand) {
    skip_spaces(expression, index);
    if (index >= expression.size() || expression[index] != '(') {
        return PassResult::Failure("missing '(' after " + probe_name +
                                   " in #if expression");
    }
    ++index;

    const std::size_t operand_begin = index;
    int nesting_depth = 1;
    while (index < expression.size() && nesting_depth > 0) {
        const char current = expression[index];
        if (current == '(') {
            ++nesting_depth;
        } else if (current == ')') {
            --nesting_depth;
            if (nesting_depth == 0) {
                break;
            }
        }
        ++index;
    }

    if (index >= expression.size() || nesting_depth != 0) {
        return PassResult::Failure("missing ')' after " + probe_name +
                                   " operand");
    }

    operand = trim_copy(expression.substr(operand_begin, index - operand_begin));
    ++index;
    if (operand.empty()) {
        return PassResult::Failure("invalid " + probe_name +
                                   " operand in #if expression");
    }

    return PassResult::Success();
}

} // namespace

PassResult ClangExtensionProvider::try_evaluate(const std::string &expression,
                                                std::size_t &index,
                                                long long &value,
                                                bool &handled) const {
    handled = false;
    const std::size_t original_index = index;

    static const char *kProbeNames[] = {
        "__has_feature", "__has_extension", "__has_builtin",
        "__has_attribute", "__has_cpp_attribute", "__building_module"};
    for (const char *probe_name : kProbeNames) {
        index = original_index;
        if (!consume_keyword(expression, index, probe_name)) {
            continue;
        }

        if (std::string(probe_name) == "__building_module") {
            skip_spaces(expression, index);
            if (index < expression.size() && expression[index] == '(') {
                std::string operand;
                PassResult parse_result = parse_parenthesized_operand(
                    expression, index, probe_name, operand);
                if (!parse_result.ok) {
                    return parse_result;
                }
            }
        } else {
            std::string operand;
            PassResult parse_result = parse_parenthesized_operand(
                expression, index, probe_name, operand);
            if (!parse_result.ok) {
                return parse_result;
            }
        }

        value = 0;
        handled = true;
        return PassResult::Success();
    }

    index = original_index;
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
