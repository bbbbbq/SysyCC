#pragma once

#include <optional>
#include <string>

namespace sysycc {

struct ParsedIntegerLiteral {
    unsigned long long magnitude = 0;
    bool is_unsigned_suffix = false;
    int long_count = 0;
    int base = 10;
};

std::optional<ParsedIntegerLiteral>
parse_integer_literal_info(const std::string &value_text) noexcept;

std::optional<long long>
parse_integer_literal(const std::string &value_text) noexcept;

} // namespace sysycc
