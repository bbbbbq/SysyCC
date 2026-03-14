#include "frontend/driver/parser_runtime.hpp"

namespace sysycc {

namespace {

std::unique_ptr<ParseTreeNode> g_parse_tree_root;

ParseTreeNode *AsNode(void *node) {
    return static_cast<ParseTreeNode *>(node);
}

} // namespace

void parser_runtime_reset() { g_parse_tree_root.reset(); }

void *make_terminal_node(const char *label, const char *text) {
    std::string node_label = label == nullptr ? "token" : label;
    if (text != nullptr && text[0] != '\0') {
        node_label += " ";
        node_label += text;
    }
    return new ParseTreeNode(std::move(node_label));
}

void *make_nonterminal_node(const char *label, std::initializer_list<void *> children) {
    auto *node = new ParseTreeNode(label == nullptr ? "node" : label);
    for (void *child_ptr : children) {
        if (child_ptr == nullptr) {
            continue;
        }
        node->children.emplace_back(AsNode(child_ptr));
    }
    return node;
}

void set_parse_tree_root(void *root) { g_parse_tree_root.reset(AsNode(root)); }

std::unique_ptr<ParseTreeNode> take_parse_tree_root() {
    return std::move(g_parse_tree_root);
}

} // namespace sysycc
