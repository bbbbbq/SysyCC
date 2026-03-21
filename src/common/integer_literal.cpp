#include "common/integer_literal.hpp"

#include <cctype>
#include <string>

namespace sysycc {

namespace {

bool is_integer_suffix_char(char ch) {
    return ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L';
}

} // namespace

std::optional<long long>
parse_integer_literal(const std::string &value_text) noexcept {
    if (value_text.empty()) {
        return std::nullopt;
    }

    std::size_t numeric_end = value_text.size();
    while (numeric_end > 0 &&
           is_integer_suffix_char(value_text[numeric_end - 1])) {
        --numeric_end;
    }
    if (numeric_end == 0) {
        return std::nullopt;
    }

    const std::string numeric_part = value_text.substr(0, numeric_end);
    int base = 10;
    if (numeric_part.size() > 2 && numeric_part[0] == '0' &&
        (numeric_part[1] == 'x' || numeric_part[1] == 'X')) {
        base = 16;
    } else if (numeric_part.size() > 1 && numeric_part[0] == '0') {
        base = 8;
    }

    try {
        std::size_t processed = 0;
        const unsigned long long parsed_value =
            std::stoull(numeric_part, &processed, base);
        if (processed != numeric_part.size()) {
            return std::nullopt;
        }
        return static_cast<long long>(parsed_value);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace sysycc
