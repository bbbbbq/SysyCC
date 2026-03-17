#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

// Expands object-like and function-like macros inside ordinary source lines.
class MacroExpander {
  private:
    std::string stringify_argument(const std::string &argument) const;
    std::size_t find_parameter_index(
        const std::string &identifier,
        const std::vector<std::string> &parameters) const;
    std::string apply_stringification(
        const std::string &replacement,
        const std::vector<std::string> &parameters,
        const std::vector<std::string> &raw_arguments) const;
    std::string apply_token_pasting(
        const std::string &replacement,
        const std::vector<std::string> &parameters,
        const std::vector<std::string> &raw_arguments) const;
    std::string expand_text(const std::string &line,
                            const MacroTable &macro_table,
                            std::unordered_set<std::string> &active_macros,
                            int depth) const;
    bool parse_macro_arguments(const std::string &line,
                               std::size_t open_paren_index,
                               std::vector<std::string> &arguments,
                               std::size_t &end_index) const;
    std::string
    substitute_parameters(const std::string &replacement,
                          const std::vector<std::string> &parameters,
                          const std::vector<std::string> &raw_arguments,
                          const std::vector<std::string> &expanded_arguments) const;

  public:
    std::string expand_line(const std::string &line,
                            const MacroTable &macro_table) const;
};

} // namespace sysycc::preprocess::detail
