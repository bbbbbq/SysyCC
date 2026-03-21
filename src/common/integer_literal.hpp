#pragma once

#include <optional>
#include <string>

namespace sysycc {

std::optional<long long>
parse_integer_literal(const std::string &value_text) noexcept;

} // namespace sysycc
