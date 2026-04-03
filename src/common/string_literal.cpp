#include "common/string_literal.hpp"

namespace sysycc {

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
            case '0':
                decoded.push_back('\0');
                break;
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
