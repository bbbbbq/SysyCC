#include "frontend/preprocess/detail/directive_parser.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace sysycc::preprocess::detail {

namespace {

std::string trim_left(const std::string &text) {
    std::size_t index = 0;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    return text.substr(index);
}

DirectiveKind to_directive_kind(const std::string &keyword) {
    if (keyword == "#define") {
        return DirectiveKind::Define;
    }
    if (keyword == "#undef") {
        return DirectiveKind::Undef;
    }
    if (keyword == "#include") {
        return DirectiveKind::Include;
    }
    if (keyword == "#ifdef") {
        return DirectiveKind::Ifdef;
    }
    if (keyword == "#ifndef") {
        return DirectiveKind::Ifndef;
    }
    if (keyword == "#if") {
        return DirectiveKind::If;
    }
    if (keyword == "#elif") {
        return DirectiveKind::Elif;
    }
    if (keyword == "#else") {
        return DirectiveKind::Else;
    }
    if (keyword == "#endif") {
        return DirectiveKind::Endif;
    }
    return DirectiveKind::Unknown;
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

} // namespace

bool DirectiveParser::is_directive(const std::string &line) const {
    const std::string trimmed = trim_left(line);
    return !trimmed.empty() && trimmed.front() == '#';
}

PassResult DirectiveParser::parse(const std::string &line,
                                  Directive &directive) const {
    if (!is_directive(line)) {
        return PassResult::Failure("not a directive");
    }

    std::istringstream iss(trim_left(line));
    std::string keyword;
    iss >> keyword;
    const DirectiveKind kind = to_directive_kind(keyword);

    if (kind == DirectiveKind::Unknown) {
        directive = Directive(kind, keyword, {});
        return PassResult::Success();
    }

    std::vector<std::string> arguments;
    if (kind == DirectiveKind::Define) {
        std::string remainder;
        std::getline(iss, remainder);
        remainder = trim_left(remainder);

        if (!remainder.empty() && !is_identifier_start(remainder[0])) {
            return PassResult::Failure(
                "invalid #define directive: invalid macro name");
        }

        std::size_t index = 0;
        while (index < remainder.size() &&
               is_identifier_char(remainder[index])) {
            ++index;
        }

        const std::string name = remainder.substr(0, index);
        if (!name.empty()) {
            arguments.push_back(name);
        }

        bool is_function_like_macro = false;
        std::vector<std::string> macro_parameters;
        std::string replacement;

        if (!name.empty() && index < remainder.size() &&
            remainder[index] == '(') {
            is_function_like_macro = true;
            ++index;

            std::size_t parameter_begin = index;
            int depth = 1;
            while (index < remainder.size() && depth > 0) {
                if (remainder[index] == '(') {
                    ++depth;
                } else if (remainder[index] == ')') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                ++index;
            }

            if (index < remainder.size()) {
                const std::string parameter_text =
                    remainder.substr(parameter_begin, index - parameter_begin);
                std::istringstream parameter_stream(parameter_text);
                std::string parameter;
                while (std::getline(parameter_stream, parameter, ',')) {
                    parameter = trim_left(parameter);
                    while (!parameter.empty() &&
                           std::isspace(static_cast<unsigned char>(
                               parameter.back())) != 0) {
                        parameter.pop_back();
                    }
                    if (!parameter.empty()) {
                        macro_parameters.push_back(parameter);
                    }
                }
                ++index;
            }

            replacement = trim_left(remainder.substr(index));
        } else {
            replacement = trim_left(remainder.substr(index));
        }

        if (!replacement.empty()) {
            arguments.push_back(replacement);
        }

        directive =
            Directive(kind, keyword, std::move(arguments),
                      is_function_like_macro, std::move(macro_parameters));
        return PassResult::Success();
    } else if (kind == DirectiveKind::If || kind == DirectiveKind::Elif) {
        std::string expression;
        std::getline(iss, expression);
        expression = trim_left(expression);
        if (!expression.empty()) {
            arguments.push_back(expression);
        }
    } else {
        std::string argument;
        iss >> argument;
        if (!argument.empty()) {
            arguments.push_back(argument);
        }
    }

    directive = Directive(kind, keyword, std::move(arguments));
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
