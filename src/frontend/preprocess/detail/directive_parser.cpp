#include "frontend/preprocess/detail/directive_parser.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <unordered_set>
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

std::string trim(const std::string &text) {
    const std::string left_trimmed = trim_left(text);
    std::size_t end = left_trimmed.size();
    while (end > 0 &&
           std::isspace(static_cast<unsigned char>(left_trimmed[end - 1])) !=
               0) {
        --end;
    }
    return left_trimmed.substr(0, end);
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
    if (keyword == "#include_next") {
        return DirectiveKind::IncludeNext;
    }
    if (keyword == "#error") {
        return DirectiveKind::Error;
    }
    if (keyword == "#pragma") {
        return DirectiveKind::Pragma;
    }
    if (keyword == "#line") {
        return DirectiveKind::Line;
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
    if (keyword == "#elifdef") {
        return DirectiveKind::Elifdef;
    }
    if (keyword == "#elifndef") {
        return DirectiveKind::Elifndef;
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

bool is_valid_identifier(const std::string &text) {
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    for (char ch : text) {
        if (!is_identifier_char(ch)) {
            return false;
        }
    }
    return true;
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

        // Reject names that cannot ever participate in identifier-based macro
        // lookup before we continue parsing the rest of the directive.
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
        bool is_variadic_macro = false;
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
                std::unordered_set<std::string> parameter_names;
                while (std::getline(parameter_stream, parameter, ',')) {
                    parameter = trim(parameter);
                    if (!parameter.empty()) {
                        if (is_variadic_macro) {
                            return PassResult::Failure(
                                "invalid #define directive: variadic macro "
                                "parameter must be last");
                        }
                        if (parameter == "...") {
                            is_variadic_macro = true;
                            continue;
                        }
                        if (parameter.size() >= 3 &&
                            parameter.compare(parameter.size() - 3, 3,
                                              "...") == 0) {
                            parameter.erase(parameter.size() - 3);
                            parameter = trim(parameter);
                            if (!is_valid_identifier(parameter)) {
                                return PassResult::Failure(
                                    "invalid #define directive: invalid macro "
                                    "parameter name");
                            }
                            if (!parameter_names.insert(parameter).second) {
                                return PassResult::Failure(
                                    "invalid #define directive: duplicate "
                                    "macro parameter");
                            }
                            macro_parameters.push_back(parameter);
                            is_variadic_macro = true;
                            continue;
                        }
                        if (!is_valid_identifier(parameter)) {
                            return PassResult::Failure(
                                "invalid #define directive: invalid macro "
                                "parameter name");
                        }
                        if (!parameter_names.insert(parameter).second) {
                            return PassResult::Failure(
                                "invalid #define directive: duplicate macro "
                                "parameter");
                        }
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
                      is_function_like_macro, is_variadic_macro,
                      std::move(macro_parameters));
        return PassResult::Success();
    } else if (kind == DirectiveKind::If || kind == DirectiveKind::Elif) {
        std::string expression;
        std::getline(iss, expression);
        expression = trim_left(expression);
        if (!expression.empty()) {
            arguments.push_back(expression);
        }
    } else if (kind == DirectiveKind::Ifdef ||
               kind == DirectiveKind::Ifndef ||
               kind == DirectiveKind::Elifdef ||
               kind == DirectiveKind::Elifndef) {
        std::string argument;
        iss >> argument;
        if (!argument.empty()) {
            arguments.push_back(argument);
        }
    } else if (kind == DirectiveKind::Error ||
               kind == DirectiveKind::Pragma ||
               kind == DirectiveKind::Line) {
        std::string message;
        std::getline(iss, message);
        message = trim_left(message);
        if (!message.empty()) {
            arguments.push_back(message);
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
