#include "common/string_literal.hpp"

namespace sysycc {

namespace {

int hex_digit_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool is_octal_digit(char ch) noexcept { return ch >= '0' && ch <= '7'; }

} // namespace

std::string decode_string_literal_token(std::string token_text) {
    if (token_text.size() >= 2 && token_text.front() == '"' &&
        token_text.back() == '"') {
        token_text = token_text.substr(1, token_text.size() - 2);
    }

    std::string decoded;
    decoded.reserve(token_text.size());
    for (std::size_t index = 0; index < token_text.size(); ++index) {
        char ch = token_text[index];
        if (ch == '\\' && index + 1 < token_text.size()) {
            const char escaped = token_text[++index];
            switch (escaped) {
            case '\\':
            case '"':
            case '\'':
                decoded.push_back(escaped);
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 'a':
                decoded.push_back('\a');
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            case 'v':
                decoded.push_back('\v');
                break;
            case 'x': {
                unsigned int value = 0;
                bool consumed_digit = false;
                while (index + 1 < token_text.size()) {
                    const int digit = hex_digit_value(token_text[index + 1]);
                    if (digit < 0) {
                        break;
                    }
                    consumed_digit = true;
                    value = (value << 4) | static_cast<unsigned int>(digit);
                    ++index;
                }
                decoded.push_back(consumed_digit
                                      ? static_cast<char>(value & 0xffU)
                                      : escaped);
                break;
            }
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7': {
                unsigned int value = static_cast<unsigned int>(escaped - '0');
                int digits = 1;
                while (digits < 3 && index + 1 < token_text.size() &&
                       is_octal_digit(token_text[index + 1])) {
                    value = (value << 3) | static_cast<unsigned int>(
                                               token_text[index + 1] - '0');
                    ++index;
                    ++digits;
                }
                decoded.push_back(static_cast<char>(value & 0xffU));
                break;
            }
            default:
                decoded.push_back(escaped);
                break;
            }
            continue;
        }
        decoded.push_back(ch);
    }
    return decoded;
}

} // namespace sysycc
