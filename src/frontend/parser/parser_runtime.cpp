#include "frontend/parser/parser_runtime.hpp"

#include <unordered_set>

namespace sysycc {

namespace {

std::unique_ptr<ParseTreeNode> g_parse_tree_root;
ParserErrorInfo g_parser_error_info;
std::unordered_set<std::string> g_typedef_names;

ParseTreeNode *AsNode(void *node) { return static_cast<ParseTreeNode *>(node); }

SourceSpan merge_child_spans(std::initializer_list<void *> children) {
    SourceSpan merged_span;
    bool has_span = false;

    for (void *child_ptr : children) {
        if (child_ptr == nullptr) {
            continue;
        }
        const ParseTreeNode *child = AsNode(child_ptr);
        if (child == nullptr || child->source_span.empty()) {
            continue;
        }
        if (!has_span) {
            merged_span = child->source_span;
            has_span = true;
            continue;
        }
        merged_span.set_end(child->source_span.get_end());
    }

    return has_span ? merged_span : SourceSpan{};
}

} // namespace

void parser_runtime_reset() {
    g_parse_tree_root.reset();
    g_parser_error_info = ParserErrorInfo();
    g_typedef_names.clear();
}

// The `label` and `text` strings have distinct parser-runtime semantics.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void *make_terminal_node(const char *label, const char *text,
                         SourceSpan source_span) {
    std::string node_label = label == nullptr ? "token" : label;
    if (text != nullptr && text[0] != '\0') {
        node_label += " ";
        node_label += text;
    }
    return new ParseTreeNode(std::move(node_label), source_span);
}

void *make_nonterminal_node(const char *label,
                            std::initializer_list<void *> children) {
    auto *node = new ParseTreeNode(label == nullptr ? "node" : label,
                                   merge_child_spans(children));
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

void set_parser_error_info(ParserErrorInfo error_info) {
    if (g_parser_error_info.empty()) {
        g_parser_error_info = std::move(error_info);
    }
}

const ParserErrorInfo &get_parser_error_info() noexcept {
    return g_parser_error_info;
}

void register_typedef_names_from_declarator_list(const ParseTreeNode *node) {
    if (node == nullptr) {
        return;
    }
    if (node->label.rfind("IDENTIFIER ", 0) == 0) {
        g_typedef_names.insert(node->label.substr(std::string("IDENTIFIER ").size()));
        return;
    }
    for (const auto &child : node->children) {
        register_typedef_names_from_declarator_list(child.get());
    }
}

bool is_typedef_name_registered(const std::string &name) {
    return g_typedef_names.find(name) != g_typedef_names.end();
}

} // namespace sysycc
