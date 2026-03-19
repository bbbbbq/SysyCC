#pragma once

#include <string>
#include <vector>

#include "frontend/parser/parser_runtime.hpp"

namespace sysycc::detail {

// Provides small helpers for matching parse-tree nodes by label prefix.
class ParseTreeMatcher {
  public:
    static bool label_equals(const ParseTreeNode *node, const char *label);
    static bool label_starts_with(const ParseTreeNode *node, const char *label);
    static const ParseTreeNode *find_first_child_with_label(
        const ParseTreeNode *node, const char *label);
    static std::vector<const ParseTreeNode *> find_children_with_label(
        const ParseTreeNode *node, const char *label);
    static std::string extract_terminal_suffix(const ParseTreeNode *node,
                                               const char *label_prefix);
};

} // namespace sysycc::detail
