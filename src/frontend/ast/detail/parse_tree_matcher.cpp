#include "frontend/ast/detail/parse_tree_matcher.hpp"

#include <cstring>

namespace sysycc::detail {

bool ParseTreeMatcher::label_equals(const ParseTreeNode *node,
                                    const char *label) {
    return node != nullptr && label != nullptr && node->label == label;
}

bool ParseTreeMatcher::label_starts_with(const ParseTreeNode *node,
                                         const char *label) {
    if (node == nullptr || label == nullptr) {
        return false;
    }
    const std::size_t prefix_length = std::strlen(label);
    return node->label.compare(0, prefix_length, label) == 0;
}

const ParseTreeNode *ParseTreeMatcher::find_first_child_with_label(
    const ParseTreeNode *node, const char *label) {
    if (node == nullptr || label == nullptr) {
        return nullptr;
    }
    for (const auto &child : node->children) {
        if (child != nullptr && label_equals(child.get(), label)) {
            return child.get();
        }
    }
    return nullptr;
}

std::vector<const ParseTreeNode *> ParseTreeMatcher::find_children_with_label(
    const ParseTreeNode *node, const char *label) {
    std::vector<const ParseTreeNode *> matches;
    if (node == nullptr || label == nullptr) {
        return matches;
    }
    for (const auto &child : node->children) {
        if (child != nullptr && label_equals(child.get(), label)) {
            matches.push_back(child.get());
        }
    }
    return matches;
}

std::string ParseTreeMatcher::extract_terminal_suffix(
    const ParseTreeNode *node, const char *label_prefix) {
    if (node == nullptr || label_prefix == nullptr) {
        return "";
    }

    const std::size_t prefix_length = std::strlen(label_prefix);
    if (node->label.size() <= prefix_length) {
        return "";
    }
    if (node->label.compare(0, prefix_length, label_prefix) != 0) {
        return "";
    }
    if (node->label.size() == prefix_length) {
        return "";
    }
    if (node->label[prefix_length] != ' ') {
        return "";
    }
    return node->label.substr(prefix_length + 1);
}

} // namespace sysycc::detail
