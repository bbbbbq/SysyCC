#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "common/source_span.hpp"

namespace sysycc {

struct ParseTreeNode {
    std::string label;
    SourceSpan source_span;
    std::vector<std::unique_ptr<ParseTreeNode>> children;

    explicit ParseTreeNode(std::string label_text, SourceSpan source_span_value = {})
        : label(std::move(label_text)), source_span(source_span_value) {}
};

void parser_runtime_reset();
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void *make_terminal_node(const char *label, const char *text,
                         SourceSpan source_span);
void *make_nonterminal_node(const char *label,
                            std::initializer_list<void *> children);
void set_parse_tree_root(void *root);
std::unique_ptr<ParseTreeNode> take_parse_tree_root();

} // namespace sysycc
