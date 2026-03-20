#include "frontend/preprocess/detail/constant_expression_evaluator.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>

#include "frontend/preprocess/detail/include_resolver.hpp"

namespace sysycc::preprocess::detail {

namespace {

class ExpressionParser {
  private:
    const std::string &expression_;
    const MacroTable &macro_table_;
    const std::string &current_file_path_;
    const std::vector<std::string> &include_directories_;
    const std::vector<std::string> &system_include_directories_;
    std::size_t index_ = 0;
    int depth_ = 0;

    bool probe_include(bool include_next, const std::string &include_token) {
        IncludeResolver include_resolver;
        std::string resolved_file_path;
        PassResult resolve_result = include_resolver.resolve_include(
            include_token, current_file_path_, include_directories_,
            system_include_directories_, include_next, include_token,
            resolved_file_path);
        return resolve_result.ok;
    }

    void skip_spaces() {
        while (index_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[index_])) !=
                   0) {
            ++index_;
        }
    }

    bool consume(const std::string &token) {
        skip_spaces();
        if (expression_.compare(index_, token.size(), token) != 0) {
            return false;
        }
        index_ += token.size();
        return true;
    }

    bool consume_single_char_operator(char token, const char *double_token) {
        skip_spaces();
        if (index_ >= expression_.size() || expression_[index_] != token) {
            return false;
        }
        if (double_token != nullptr &&
            expression_.compare(index_, 2, double_token) == 0) {
            return false;
        }
        ++index_;
        return true;
    }

    bool parse_identifier(std::string &identifier) {
        skip_spaces();
        if (index_ >= expression_.size()) {
            return false;
        }
        const char ch = expression_[index_];
        if (std::isalpha(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
            return false;
        }

        const std::size_t begin = index_;
        ++index_;
        while (index_ < expression_.size()) {
            const char current = expression_[index_];
            if (std::isalnum(static_cast<unsigned char>(current)) == 0 &&
                current != '_') {
                break;
            }
            ++index_;
        }

        identifier = expression_.substr(begin, index_ - begin);
        return true;
    }

    PassResult parse_number(long long &value) {
        skip_spaces();
        if (index_ >= expression_.size()) {
            return PassResult::Failure("expected number");
        }

        const std::size_t begin = index_;
        if (expression_[index_] == '0' && index_ + 1 < expression_.size() &&
            (expression_[index_ + 1] == 'x' ||
             expression_[index_ + 1] == 'X')) {
            index_ += 2;
            while (index_ < expression_.size() &&
                   std::isxdigit(
                       static_cast<unsigned char>(expression_[index_])) != 0) {
                ++index_;
            }
        } else {
            while (index_ < expression_.size() &&
                   std::isalnum(
                       static_cast<unsigned char>(expression_[index_])) != 0) {
                ++index_;
            }
        }

        const std::string token = expression_.substr(begin, index_ - begin);
        char *end_ptr = nullptr;
        errno = 0;
        const long long parsed_value = std::strtoll(token.c_str(), &end_ptr, 0);
        if (end_ptr == token.c_str() || *end_ptr != '\0' || errno == ERANGE) {
            return PassResult::Failure(
                "invalid integer literal in #if expression: " + token);
        }

        value = parsed_value;
        return PassResult::Success();
    }

    PassResult evaluate_macro_replacement(const std::string &identifier,
                                          long long &value) {
        const MacroDefinition *definition =
            macro_table_.get_macro_definition(identifier);
        if (definition == nullptr) {
            value = 0;
            return PassResult::Success();
        }

        // Function-like macros only participate in #if expression replacement
        // when they are actually invoked. A bare identifier must behave like
        // any other leftover identifier and evaluate to 0.
        if (definition->get_is_function_like()) {
            value = 0;
            return PassResult::Success();
        }

        if (depth_ > 16) {
            return PassResult::Failure(
                "macro expansion too deep in #if expression");
        }

        ExpressionParser nested_parser(definition->get_replacement(),
                                       macro_table_, current_file_path_,
                                       include_directories_,
                                       system_include_directories_,
                                       depth_ + 1);
        return nested_parser.parse_complete_expression(value);
    }

    PassResult parse_primary(long long &value) {
        skip_spaces();
        if (consume("(")) {
            PassResult expr_result = parse_subexpression(value);
            if (!expr_result.ok) {
                return expr_result;
            }
            if (!consume(")")) {
                return PassResult::Failure("missing ')' in #if expression");
            }
            return PassResult::Success();
        }

        if (index_ < expression_.size() &&
            std::isdigit(static_cast<unsigned char>(expression_[index_])) !=
                0) {
            return parse_number(value);
        }

        std::string identifier;
        if (!parse_identifier(identifier)) {
            return PassResult::Failure("invalid token in #if expression");
        }

        return evaluate_macro_replacement(identifier, value);
    }

    PassResult parse_unary(long long &value) {
        skip_spaces();
        if (consume("defined")) {
            std::string identifier;
            if (consume("(")) {
                if (!parse_identifier(identifier)) {
                    return PassResult::Failure(
                        "invalid defined() operand in #if expression");
                }
                if (!consume(")")) {
                    return PassResult::Failure(
                        "missing ')' after defined() operand");
                }
            } else if (!parse_identifier(identifier)) {
                return PassResult::Failure(
                    "invalid defined operand in #if expression");
            }

            value = macro_table_.has_macro(identifier) ? 1 : 0;
            return PassResult::Success();
        }

        const bool include_next = consume("__has_include_next");
        if (include_next || consume("__has_include")) {
            skip_spaces();
            if (!consume("(")) {
                return PassResult::Failure(
                    "missing '(' after __has_include in #if expression");
            }

            skip_spaces();
            if (index_ >= expression_.size()) {
                return PassResult::Failure(
                    "missing header name in __has_include expression");
            }

            const char opening = expression_[index_];
            if (opening != '"' && opening != '<') {
                return PassResult::Failure(
                    "invalid __has_include operand in #if expression");
            }

            const char closing = opening == '"' ? '"' : '>';
            const std::size_t start_index = index_;
            ++index_;
            while (index_ < expression_.size() &&
                   expression_[index_] != closing) {
                ++index_;
            }
            if (index_ >= expression_.size()) {
                return PassResult::Failure(
                    "missing header terminator in __has_include expression");
            }
            const std::string include_token =
                expression_.substr(start_index, index_ - start_index + 1);
            ++index_;

            skip_spaces();
            if (!consume(")")) {
                return PassResult::Failure(
                    "missing ')' after __has_include operand");
            }

            value = probe_include(include_next, include_token) ? 1 : 0;
            return PassResult::Success();
        }

        if (consume("!")) {
            PassResult operand_result = parse_unary(value);
            if (!operand_result.ok) {
                return operand_result;
            }
            value = !value;
            return PassResult::Success();
        }

        if (consume("+")) {
            return parse_unary(value);
        }

        if (consume("-")) {
            PassResult operand_result = parse_unary(value);
            if (!operand_result.ok) {
                return operand_result;
            }
            value = -value;
            return PassResult::Success();
        }

        if (consume("~")) {
            PassResult operand_result = parse_unary(value);
            if (!operand_result.ok) {
                return operand_result;
            }
            value = ~value;
            return PassResult::Success();
        }

        return parse_primary(value);
    }

    PassResult parse_multiplicative(long long &value) {
        PassResult result = parse_unary(value);
        if (!result.ok) {
            return result;
        }

        while (true) {
            if (consume("*")) {
                long long rhs = 0;
                result = parse_unary(rhs);
                if (!result.ok) {
                    return result;
                }
                value *= rhs;
                continue;
            }
            if (consume("/")) {
                long long rhs = 0;
                result = parse_unary(rhs);
                if (!result.ok) {
                    return result;
                }
                if (rhs == 0) {
                    return PassResult::Failure(
                        "division by zero in #if expression");
                }
                value /= rhs;
                continue;
            }
            if (consume("%")) {
                long long rhs = 0;
                result = parse_unary(rhs);
                if (!result.ok) {
                    return result;
                }
                if (rhs == 0) {
                    return PassResult::Failure(
                        "modulo by zero in #if expression");
                }
                value %= rhs;
                continue;
            }
            return PassResult::Success();
        }
    }

    PassResult parse_additive(long long &value) {
        PassResult result = parse_multiplicative(value);
        if (!result.ok) {
            return result;
        }

        while (true) {
            if (consume("+")) {
                long long rhs = 0;
                result = parse_multiplicative(rhs);
                if (!result.ok) {
                    return result;
                }
                value += rhs;
                continue;
            }
            if (consume("-")) {
                long long rhs = 0;
                result = parse_multiplicative(rhs);
                if (!result.ok) {
                    return result;
                }
                value -= rhs;
                continue;
            }
            return PassResult::Success();
        }
    }

    PassResult parse_shift(long long &value) {
        PassResult result = parse_additive(value);
        if (!result.ok) {
            return result;
        }

        while (true) {
            if (consume("<<")) {
                long long rhs = 0;
                result = parse_additive(rhs);
                if (!result.ok) {
                    return result;
                }
                value <<= rhs;
                continue;
            }
            if (consume(">>")) {
                long long rhs = 0;
                result = parse_additive(rhs);
                if (!result.ok) {
                    return result;
                }
                value >>= rhs;
                continue;
            }
            return PassResult::Success();
        }
    }

    PassResult parse_relational(long long &value) {
        PassResult result = parse_shift(value);
        if (!result.ok) {
            return result;
        }

        while (true) {
            if (consume("<=")) {
                long long rhs = 0;
                result = parse_shift(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value <= rhs;
                continue;
            }
            if (consume(">=")) {
                long long rhs = 0;
                result = parse_shift(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value >= rhs;
                continue;
            }
            if (consume("<")) {
                long long rhs = 0;
                result = parse_shift(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value < rhs;
                continue;
            }
            if (consume(">")) {
                long long rhs = 0;
                result = parse_shift(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value > rhs;
                continue;
            }
            return PassResult::Success();
        }
    }

    PassResult parse_equality(long long &value) {
        PassResult result = parse_relational(value);
        if (!result.ok) {
            return result;
        }

        while (true) {
            if (consume("==")) {
                long long rhs = 0;
                result = parse_relational(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value == rhs;
                continue;
            }
            if (consume("!=")) {
                long long rhs = 0;
                result = parse_relational(rhs);
                if (!result.ok) {
                    return result;
                }
                value = value != rhs;
                continue;
            }
            return PassResult::Success();
        }
    }

    PassResult parse_bitwise_and(long long &value) {
        PassResult result = parse_equality(value);
        if (!result.ok) {
            return result;
        }

        while (consume_single_char_operator('&', "&&")) {
            long long rhs = 0;
            result = parse_equality(rhs);
            if (!result.ok) {
                return result;
            }
            value &= rhs;
        }

        return PassResult::Success();
    }

    PassResult parse_bitwise_xor(long long &value) {
        PassResult result = parse_bitwise_and(value);
        if (!result.ok) {
            return result;
        }

        while (consume("^")) {
            long long rhs = 0;
            result = parse_bitwise_and(rhs);
            if (!result.ok) {
                return result;
            }
            value ^= rhs;
        }

        return PassResult::Success();
    }

    PassResult parse_bitwise_or(long long &value) {
        PassResult result = parse_bitwise_xor(value);
        if (!result.ok) {
            return result;
        }

        while (consume_single_char_operator('|', "||")) {
            long long rhs = 0;
            result = parse_bitwise_xor(rhs);
            if (!result.ok) {
                return result;
            }
            value |= rhs;
        }

        return PassResult::Success();
    }

    PassResult parse_logical_and(long long &value) {
        PassResult result = parse_bitwise_or(value);
        if (!result.ok) {
            return result;
        }

        while (consume("&&")) {
            long long rhs = 0;
            result = parse_bitwise_or(rhs);
            if (!result.ok) {
                return result;
            }
            value = (value != 0) && (rhs != 0);
        }

        return PassResult::Success();
    }

    PassResult parse_logical_or(long long &value) {
        PassResult result = parse_logical_and(value);
        if (!result.ok) {
            return result;
        }

        while (consume("||")) {
            long long rhs = 0;
            result = parse_logical_and(rhs);
            if (!result.ok) {
                return result;
            }
            value = (value != 0) || (rhs != 0);
        }

        return PassResult::Success();
    }

    PassResult parse_conditional(long long &value) {
        PassResult result = parse_logical_or(value);
        if (!result.ok) {
            return result;
        }

        skip_spaces();
        if (!consume("?")) {
            return PassResult::Success();
        }

        long long true_value = 0;
        result = parse_conditional(true_value);
        if (!result.ok) {
            return result;
        }

        if (!consume(":")) {
            return PassResult::Failure(
                "missing ':' in #if conditional expression");
        }

        long long false_value = 0;
        result = parse_conditional(false_value);
        if (!result.ok) {
            return result;
        }

        value = value != 0 ? true_value : false_value;
        return PassResult::Success();
    }

    PassResult parse_comma(long long &value) {
        PassResult result = parse_conditional(value);
        if (!result.ok) {
            return result;
        }

        while (consume(",")) {
            long long rhs = 0;
            result = parse_conditional(rhs);
            if (!result.ok) {
                return result;
            }
            value = rhs;
        }

        return PassResult::Success();
    }

  public:
    ExpressionParser(const std::string &expression,
                     const MacroTable &macro_table,
                     const std::string &current_file_path,
                     const std::vector<std::string> &include_directories,
                     const std::vector<std::string> &system_include_directories,
                     int depth)
        : expression_(expression), macro_table_(macro_table),
          current_file_path_(current_file_path),
          include_directories_(include_directories),
          system_include_directories_(system_include_directories),
          depth_(depth) {}

    PassResult parse_subexpression(long long &value) {
        PassResult result = parse_comma(value);
        if (!result.ok) {
            return result;
        }

        return PassResult::Success();
    }

    PassResult parse_complete_expression(long long &value) {
        PassResult result = parse_subexpression(value);
        if (!result.ok) {
            return result;
        }

        skip_spaces();
        if (index_ != expression_.size()) {
            return PassResult::Failure(
                "unsupported trailing tokens in #if expression");
        }

        return PassResult::Success();
    }
};

} // namespace

PassResult ConstantExpressionEvaluator::evaluate(const std::string &expression,
                                                 const MacroTable &macro_table,
                                                 const std::string &current_file_path,
                                                 const std::vector<std::string> &include_directories,
                                                 const std::vector<std::string> &system_include_directories,
                                                 long long &value) const {
    ExpressionParser parser(expression, macro_table, current_file_path,
                            include_directories, system_include_directories, 0);
    return parser.parse_complete_expression(value);
}

} // namespace sysycc::preprocess::detail
