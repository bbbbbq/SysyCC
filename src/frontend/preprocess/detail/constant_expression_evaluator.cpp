#include "frontend/preprocess/detail/constant_expression_evaluator.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>

#include "frontend/preprocess/detail/macro_expander.hpp"

namespace sysycc::preprocess::detail {

namespace {

bool is_integer_suffix_char(char ch) {
    return ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L';
}

bool is_valid_integer_suffix(const std::string &suffix) {
    if (suffix.empty()) {
        return true;
    }

    int unsigned_count = 0;
    int long_count = 0;
    for (char ch : suffix) {
        if (!is_integer_suffix_char(ch)) {
            return false;
        }
        if (ch == 'u' || ch == 'U') {
            ++unsigned_count;
            continue;
        }
        ++long_count;
    }

    return unsigned_count <= 1 && long_count <= 2;
}

class ExpressionParser {
  private:
    const std::string &expression_;
    const MacroTable &macro_table_;
    const BuiltinProbeEvaluator &builtin_probe_evaluator_;
    const std::string &current_file_path_;
    const std::vector<std::string> &include_directories_;
    const std::vector<std::string> &quote_include_directories_;
    const std::vector<std::string> &system_include_directories_;
    const DialectManager &dialect_manager_;
    std::size_t index_ = 0;
    int depth_ = 0;

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

    bool find_macro_invocation_end(std::size_t open_paren_index,
                                   std::size_t &end_index) const {
        if (open_paren_index >= expression_.size() ||
            expression_[open_paren_index] != '(') {
            return false;
        }

        int depth = 1;
        bool in_string_literal = false;
        bool in_char_literal = false;
        bool escaping = false;
        std::size_t index = open_paren_index + 1;
        while (index < expression_.size()) {
            const char ch = expression_[index];
            if (in_string_literal) {
                if (escaping) {
                    escaping = false;
                } else if (ch == '\\') {
                    escaping = true;
                } else if (ch == '"') {
                    in_string_literal = false;
                }
                ++index;
                continue;
            }

            if (in_char_literal) {
                if (escaping) {
                    escaping = false;
                } else if (ch == '\\') {
                    escaping = true;
                } else if (ch == '\'') {
                    in_char_literal = false;
                }
                ++index;
                continue;
            }

            if (ch == '"') {
                in_string_literal = true;
                ++index;
                continue;
            }

            if (ch == '\'') {
                in_char_literal = true;
                ++index;
                continue;
            }

            if (ch == '(') {
                ++depth;
                ++index;
                continue;
            }

            if (ch == ')') {
                --depth;
                ++index;
                if (depth == 0) {
                    end_index = index;
                    return true;
                }
                continue;
            }

            ++index;
        }

        return false;
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
            while (index_ < expression_.size() &&
                   is_integer_suffix_char(expression_[index_])) {
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
        std::size_t numeric_end = token.size();
        while (numeric_end > 0 &&
               is_integer_suffix_char(token[numeric_end - 1])) {
            --numeric_end;
        }

        const std::string numeric_part = token.substr(0, numeric_end);
        const std::string suffix = token.substr(numeric_end);
        if (numeric_part.empty() || !is_valid_integer_suffix(suffix)) {
            return PassResult::Failure(
                "invalid integer literal in #if expression: " + token);
        }

        char *end_ptr = nullptr;
        errno = 0;
        const long long parsed_value =
            std::strtoll(numeric_part.c_str(), &end_ptr, 0);
        if (end_ptr == numeric_part.c_str() || *end_ptr != '\0' ||
            errno == ERANGE) {
            return PassResult::Failure(
                "invalid integer literal in #if expression: " + token);
        }

        value = parsed_value;
        return PassResult::Success();
    }

    PassResult parse_character_literal(long long &value) {
        skip_spaces();
        if (index_ >= expression_.size()) {
            return PassResult::Failure("expected character literal");
        }

        if ((expression_[index_] == 'L' || expression_[index_] == 'u' ||
             expression_[index_] == 'U') &&
            index_ + 1 < expression_.size() && expression_[index_ + 1] == '\'') {
            ++index_;
        }
        if (index_ >= expression_.size() || expression_[index_] != '\'') {
            return PassResult::Failure("expected character literal");
        }
        ++index_;
        if (index_ >= expression_.size()) {
            return PassResult::Failure("unterminated character literal");
        }

        unsigned char parsed_value = 0;
        if (expression_[index_] == '\\') {
            ++index_;
            if (index_ >= expression_.size()) {
                return PassResult::Failure("unterminated character literal");
            }
            const char escape = expression_[index_++];
            switch (escape) {
            case '0':
                parsed_value = 0;
                while (index_ < expression_.size() &&
                       expression_[index_] >= '0' && expression_[index_] <= '7') {
                    ++index_;
                }
                break;
            case 'n':
                parsed_value = '\n';
                break;
            case 't':
                parsed_value = '\t';
                break;
            case 'r':
                parsed_value = '\r';
                break;
            case 'a':
                parsed_value = '\a';
                break;
            case 'b':
                parsed_value = '\b';
                break;
            case 'f':
                parsed_value = '\f';
                break;
            case 'v':
                parsed_value = '\v';
                break;
            case '\\':
                parsed_value = '\\';
                break;
            case '\'':
                parsed_value = '\'';
                break;
            default:
                parsed_value = static_cast<unsigned char>(escape);
                break;
            }
        } else {
            parsed_value = static_cast<unsigned char>(expression_[index_++]);
        }

        while (index_ < expression_.size() && expression_[index_] != '\'') {
            ++index_;
        }
        if (index_ >= expression_.size()) {
            return PassResult::Failure("unterminated character literal");
        }
        ++index_;
        value = parsed_value;
        return PassResult::Success();
    }

    PassResult evaluate_macro_replacement(const std::string &identifier,
                                          std::size_t identifier_begin,
                                          long long &value) {
        const MacroDefinition *definition =
            macro_table_.get_macro_definition(identifier);
        if (definition == nullptr) {
            value = 0;
            return PassResult::Success();
        }

        if (definition->get_is_function_like()) {
            std::size_t next_index = index_;
            while (next_index < expression_.size() &&
                   std::isspace(static_cast<unsigned char>(
                       expression_[next_index])) != 0) {
                ++next_index;
            }
            std::size_t invocation_end = next_index;
            if (next_index >= expression_.size() ||
                expression_[next_index] != '(' ||
                !find_macro_invocation_end(next_index, invocation_end)) {
                // Function-like macros only participate in #if expression
                // replacement when they are actually invoked. A bare
                // identifier behaves like any other leftover identifier.
                value = 0;
                return PassResult::Success();
            }

            if (depth_ > 16) {
                return PassResult::Failure(
                    "macro expansion too deep in #if expression");
            }

            const std::string invocation = expression_.substr(
                identifier_begin, invocation_end - identifier_begin);
            const MacroExpander macro_expander;
            const std::string expanded_invocation =
                macro_expander.expand_line(invocation, macro_table_);
            index_ = invocation_end;

            ExpressionParser nested_parser(
                expanded_invocation, macro_table_, builtin_probe_evaluator_,
                current_file_path_, include_directories_,
                quote_include_directories_, system_include_directories_,
                dialect_manager_, depth_ + 1);
            return nested_parser.parse_complete_expression(value);
        }

        if (depth_ > 16) {
            return PassResult::Failure(
                "macro expansion too deep in #if expression");
        }

        ExpressionParser nested_parser(
            definition->get_replacement(), macro_table_,
            builtin_probe_evaluator_, current_file_path_, include_directories_,
            quote_include_directories_, system_include_directories_,
            dialect_manager_, depth_ + 1);
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

        if (index_ < expression_.size() &&
            (expression_[index_] == '\'' ||
             ((expression_[index_] == 'L' || expression_[index_] == 'u' ||
               expression_[index_] == 'U') &&
              index_ + 1 < expression_.size() &&
              expression_[index_ + 1] == '\''))) {
            return parse_character_literal(value);
        }

        const std::size_t identifier_begin = index_;
        std::string identifier;
        if (!parse_identifier(identifier)) {
            return PassResult::Failure("invalid token in #if expression");
        }

        return evaluate_macro_replacement(identifier, identifier_begin, value);
    }

    PassResult parse_unary(long long &value) {
        skip_spaces();
        bool handled = false;
        PassResult builtin_probe_result = builtin_probe_evaluator_.try_evaluate(
            expression_, index_, macro_table_, current_file_path_,
            include_directories_, quote_include_directories_,
            system_include_directories_, dialect_manager_, value, handled);
        if (!builtin_probe_result.ok) {
            return builtin_probe_result;
        }
        if (handled) {
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
                     const BuiltinProbeEvaluator &builtin_probe_evaluator,
                     const std::string &current_file_path,
                     const std::vector<std::string> &include_directories,
                     const std::vector<std::string> &quote_include_directories,
                     const std::vector<std::string> &system_include_directories,
                     const DialectManager &dialect_manager,
                     int depth)
        : expression_(expression), macro_table_(macro_table),
          builtin_probe_evaluator_(builtin_probe_evaluator),
          current_file_path_(current_file_path),
          include_directories_(include_directories),
          quote_include_directories_(quote_include_directories),
          system_include_directories_(system_include_directories),
          dialect_manager_(dialect_manager),
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
                                                 const std::vector<std::string> &quote_include_directories,
                                                 const std::vector<std::string> &system_include_directories,
                                                 const DialectManager &dialect_manager,
                                                 long long &value) const {
    ExpressionParser parser(expression, macro_table, builtin_probe_evaluator_,
                            current_file_path, include_directories,
                            quote_include_directories,
                            system_include_directories, dialect_manager, 0);
    return parser.parse_complete_expression(value);
}

} // namespace sysycc::preprocess::detail
