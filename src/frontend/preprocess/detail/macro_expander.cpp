#include "frontend/preprocess/detail/macro_expander.hpp"

#include <cctype>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sysycc::preprocess::detail {

namespace {

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string trim_left(const std::string &text) {
    std::size_t index = 0;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    return text.substr(index);
}

std::string trim_right(const std::string &text) {
    std::size_t end = text.size();
    while (end > 0 &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(0, end);
}

std::string trim(const std::string &text) {
    return trim_right(trim_left(text));
}

bool is_token_char(char ch) {
    return is_identifier_char(ch);
}

} // namespace

std::string MacroExpander::stringify_argument(const std::string &argument) const {
    std::string output = "\"";
    for (char ch : trim(argument)) {
        if (ch == '\\' || ch == '"') {
            output.push_back('\\');
        }
        output.push_back(ch);
    }
    output.push_back('"');
    return output;
}

std::size_t MacroExpander::find_parameter_index(
    const std::string &identifier,
    const std::vector<std::string> &parameters) const {
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (parameters[index] == identifier) {
            return index;
        }
    }
    return parameters.size();
}

std::string MacroExpander::apply_stringification(
    const std::string &replacement, const std::vector<std::string> &parameters,
    const std::vector<std::string> &raw_arguments) const {
    std::string output;
    std::size_t index = 0;
    while (index < replacement.size()) {
        if (replacement[index] == '#' && index + 1 < replacement.size() &&
            replacement[index + 1] == '#') {
            output += "##";
            index += 2;
            continue;
        }

        if (replacement[index] != '#') {
            output.push_back(replacement[index]);
            ++index;
            continue;
        }

        std::size_t next = index + 1;
        while (next < replacement.size() &&
               std::isspace(static_cast<unsigned char>(replacement[next])) != 0) {
            ++next;
        }

        if (next >= replacement.size() || !is_identifier_start(replacement[next])) {
            output.push_back(replacement[index]);
            ++index;
            continue;
        }

        std::size_t end = next + 1;
        while (end < replacement.size() && is_identifier_char(replacement[end])) {
            ++end;
        }

        const std::string identifier = replacement.substr(next, end - next);
        const std::size_t parameter_index =
            find_parameter_index(identifier, parameters);
        if (parameter_index == parameters.size()) {
            output.push_back(replacement[index]);
            ++index;
            continue;
        }

        output += stringify_argument(raw_arguments[parameter_index]);
        index = end;
    }

    return output;
}

std::string MacroExpander::apply_token_pasting(
    const std::string &replacement, const std::vector<std::string> &parameters,
    const std::vector<std::string> &raw_arguments) const {
    std::string output = replacement;

    while (true) {
        const std::size_t paste_index = output.find("##");
        if (paste_index == std::string::npos) {
            break;
        }

        std::size_t left_end = paste_index;
        while (left_end > 0 &&
               std::isspace(static_cast<unsigned char>(output[left_end - 1])) != 0) {
            --left_end;
        }
        if (left_end == 0) {
            break;
        }

        std::size_t left_begin = left_end - 1;
        if (is_token_char(output[left_begin])) {
            while (left_begin > 0 && is_token_char(output[left_begin - 1])) {
                --left_begin;
            }
        }

        std::size_t right_begin = paste_index + 2;
        while (right_begin < output.size() &&
               std::isspace(static_cast<unsigned char>(output[right_begin])) != 0) {
            ++right_begin;
        }
        if (right_begin >= output.size()) {
            break;
        }

        std::size_t right_end = right_begin + 1;
        if (is_token_char(output[right_begin])) {
            while (right_end < output.size() && is_token_char(output[right_end])) {
                ++right_end;
            }
        }

        std::string left_token = output.substr(left_begin, left_end - left_begin);
        std::string right_token = output.substr(right_begin, right_end - right_begin);

        const std::size_t left_parameter_index =
            find_parameter_index(left_token, parameters);
        if (left_parameter_index != parameters.size()) {
            left_token = trim(raw_arguments[left_parameter_index]);
        }

        const std::size_t right_parameter_index =
            find_parameter_index(right_token, parameters);
        if (right_parameter_index != parameters.size()) {
            right_token = trim(raw_arguments[right_parameter_index]);
        }

        output.replace(left_begin, right_end - left_begin, left_token + right_token);
    }

    return output;
}

std::string MacroExpander::expand_line(const std::string &line,
                                       const MacroTable &macro_table) const {
    std::unordered_set<std::string> active_macros;
    return expand_text(line, macro_table, active_macros, 0);
}

std::string MacroExpander::expand_text(
    const std::string &line, const MacroTable &macro_table,
    std::unordered_set<std::string> &active_macros, int depth) const {
    if (depth > 16) {
        return line;
    }

    std::string output;
    std::size_t index = 0;
    while (index < line.size()) {
        if (!is_identifier_start(line[index])) {
            output.push_back(line[index]);
            ++index;
            continue;
        }

        std::size_t end = index + 1;
        while (end < line.size() && is_identifier_char(line[end])) {
            ++end;
        }

        const std::string identifier = line.substr(index, end - index);
        const MacroDefinition *definition =
            macro_table.get_macro_definition(identifier);
        if (definition == nullptr ||
            active_macros.find(identifier) != active_macros.end()) {
            output += identifier;
            index = end;
            continue;
        }

        active_macros.insert(identifier);

        if (!definition->get_is_function_like()) {
            output += expand_text(definition->get_replacement(), macro_table,
                                  active_macros, depth + 1);
            active_macros.erase(identifier);
            index = end;
            continue;
        }

        std::size_t next_index = end;
        while (next_index < line.size() &&
               std::isspace(static_cast<unsigned char>(line[next_index])) !=
                   0) {
            ++next_index;
        }

        if (next_index >= line.size() || line[next_index] != '(') {
            output += identifier;
            active_macros.erase(identifier);
            index = end;
            continue;
        }

        std::vector<std::string> arguments;
        std::size_t invocation_end = next_index;
        if (!parse_macro_arguments(line, next_index, arguments,
                                   invocation_end) ||
            arguments.size() != definition->get_parameters().size()) {
            output += identifier;
            active_macros.erase(identifier);
            index = end;
            continue;
        }

        std::vector<std::string> raw_arguments = arguments;
        for (std::string &argument : arguments) {
            std::unordered_set<std::string> argument_active_macros =
                active_macros;
            argument_active_macros.erase(identifier);
            argument = expand_text(trim_right(trim_left(argument)), macro_table,
                                   argument_active_macros, depth + 1);
        }

        const std::string substituted =
            substitute_parameters(definition->get_replacement(),
                                  definition->get_parameters(), raw_arguments,
                                  arguments);
        output +=
            expand_text(substituted, macro_table, active_macros, depth + 1);

        active_macros.erase(identifier);
        index = invocation_end;
    }

    return output;
}

bool MacroExpander::parse_macro_arguments(const std::string &line,
                                          std::size_t open_paren_index,
                                          std::vector<std::string> &arguments,
                                          std::size_t &end_index) const {
    if (open_paren_index >= line.size() || line[open_paren_index] != '(') {
        return false;
    }

    arguments.clear();
    std::string current_argument;
    int depth = 1;
    std::size_t index = open_paren_index + 1;
    while (index < line.size()) {
        const char ch = line[index];
        if (ch == '(') {
            ++depth;
            current_argument.push_back(ch);
            ++index;
            continue;
        }

        if (ch == ')') {
            --depth;
            if (depth == 0) {
                if (!current_argument.empty() || !arguments.empty()) {
                    arguments.push_back(current_argument);
                }
                end_index = index + 1;
                return true;
            }

            current_argument.push_back(ch);
            ++index;
            continue;
        }

        if (ch == ',' && depth == 1) {
            arguments.push_back(current_argument);
            current_argument.clear();
            ++index;
            continue;
        }

        current_argument.push_back(ch);
        ++index;
    }

    return false;
}

std::string MacroExpander::substitute_parameters(
    const std::string &replacement, const std::vector<std::string> &parameters,
    const std::vector<std::string> &raw_arguments,
    const std::vector<std::string> &expanded_arguments) const {
    const std::string stringified =
        apply_stringification(replacement, parameters, raw_arguments);
    const std::string pasted =
        apply_token_pasting(stringified, parameters, raw_arguments);

    std::string output;
    std::size_t index = 0;
    while (index < pasted.size()) {
        if (!is_identifier_start(pasted[index])) {
            output.push_back(pasted[index]);
            ++index;
            continue;
        }

        std::size_t end = index + 1;
        while (end < pasted.size() && is_identifier_char(pasted[end])) {
            ++end;
        }

        const std::string identifier = pasted.substr(index, end - index);
        const std::size_t parameter_index =
            find_parameter_index(identifier, parameters);
        if (parameter_index != parameters.size()) {
            output += expanded_arguments[parameter_index];
        } else {
            output += identifier;
        }
        index = end;
    }

    return output;
}

} // namespace sysycc::preprocess::detail
