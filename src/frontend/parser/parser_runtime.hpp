#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace sysycc {

struct ParseTreeNode {
    std::string label;
    std::vector<std::unique_ptr<ParseTreeNode>> children;

    explicit ParseTreeNode(std::string label_text)
        : label(std::move(label_text)) {}
};

void parser_runtime_reset();
void *make_terminal_node(const char *label, const char *text);
void *make_nonterminal_node(const char *label,
                            std::initializer_list<void *> children);
void set_parse_tree_root(void *root);
std::unique_ptr<ParseTreeNode> take_parse_tree_root();

} // namespace sysycc
