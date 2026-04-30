#include "frontend/parser/parser_runtime.hpp"

#include <unordered_map>
#include <unordered_set>

#include "frontend/support/builtin_typedef_inventory.hpp"

namespace sysycc {

namespace {

std::unique_ptr<ParseTreeNode> g_parse_tree_root;
ParserErrorInfo g_parser_error_info;
std::unordered_set<std::string> g_typedef_names;
std::unordered_map<std::string, std::size_t> g_hidden_typedef_name_counts;
std::vector<std::vector<std::string>> g_typedef_shadow_scope_stack;

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
    g_hidden_typedef_name_counts.clear();
    g_typedef_shadow_scope_stack.clear();
    for_each_builtin_typedef_inventory_entry(
        [](const BuiltinTypedefInventoryEntry &entry) {
            g_typedef_names.insert(std::string(entry.name));
        });
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

void register_typedef_name(const std::string &name) {
    if (!name.empty()) {
        g_typedef_names.insert(name);
    }
}

void register_typedef_names_from_declarator_list(const ParseTreeNode *node) {
    if (node == nullptr) {
        return;
    }
    if (node->label == "parameter_decl" ||
        node->label == "parameter_list" ||
        node->label == "function_parameter_list_opt") {
        return;
    }
    if (node->label == "declarator_identifier") {
        for (const auto &child : node->children) {
            if (child->label.rfind("IDENTIFIER ", 0) == 0) {
                register_typedef_name(
                    child->label.substr(std::string("IDENTIFIER ").size()));
                return;
            }
            if (child->label.rfind("TYPE_NAME ", 0) == 0) {
                register_typedef_name(
                    child->label.substr(std::string("TYPE_NAME ").size()));
                return;
            }
        }
    }
    for (const auto &child : node->children) {
        register_typedef_names_from_declarator_list(child.get());
    }
}

bool is_typedef_name_registered(const std::string &name) {
    const auto hidden_it = g_hidden_typedef_name_counts.find(name);
    return g_typedef_names.find(name) != g_typedef_names.end() &&
           (hidden_it == g_hidden_typedef_name_counts.end() ||
            hidden_it->second == 0);
}

void push_typedef_shadow_scope() {
    g_typedef_shadow_scope_stack.emplace_back();
}

void pop_typedef_shadow_scope() {
    if (g_typedef_shadow_scope_stack.empty()) {
        return;
    }
    for (const std::string &name : g_typedef_shadow_scope_stack.back()) {
        auto hidden_it = g_hidden_typedef_name_counts.find(name);
        if (hidden_it == g_hidden_typedef_name_counts.end()) {
            continue;
        }
        if (hidden_it->second <= 1) {
            g_hidden_typedef_name_counts.erase(hidden_it);
        } else {
            --hidden_it->second;
        }
    }
    g_typedef_shadow_scope_stack.pop_back();
}

void hide_typedef_names_from_declarator_list(const ParseTreeNode *node) {
    if (node == nullptr) {
        return;
    }
    if (node->label == "parameter_decl" ||
        node->label == "parameter_list" ||
        node->label == "function_parameter_list_opt") {
        return;
    }
    if (node->label == "declarator_identifier") {
        for (const auto &child : node->children) {
            std::string name;
            if (child->label.rfind("IDENTIFIER ", 0) == 0) {
                name = child->label.substr(std::string("IDENTIFIER ").size());
            } else if (child->label.rfind("TYPE_NAME ", 0) == 0) {
                name = child->label.substr(std::string("TYPE_NAME ").size());
            }
            if (!name.empty() &&
                g_typedef_names.find(name) != g_typedef_names.end()) {
                ++g_hidden_typedef_name_counts[name];
                if (!g_typedef_shadow_scope_stack.empty()) {
                    g_typedef_shadow_scope_stack.back().push_back(name);
                }
                return;
            }
        }
    }
    for (const auto &child : node->children) {
        hide_typedef_names_from_declarator_list(child.get());
    }
}

bool hide_first_typedef_declarator_identifier(const ParseTreeNode *node) {
    if (node == nullptr) {
        return false;
    }
    if (node->label == "declarator_identifier") {
        for (const auto &child : node->children) {
            std::string name;
            if (child->label.rfind("IDENTIFIER ", 0) == 0) {
                name = child->label.substr(std::string("IDENTIFIER ").size());
            } else if (child->label.rfind("TYPE_NAME ", 0) == 0) {
                name = child->label.substr(std::string("TYPE_NAME ").size());
            }
            if (!name.empty() &&
                g_typedef_names.find(name) != g_typedef_names.end()) {
                ++g_hidden_typedef_name_counts[name];
                if (!g_typedef_shadow_scope_stack.empty()) {
                    g_typedef_shadow_scope_stack.back().push_back(name);
                }
                return true;
            }
            if (!name.empty()) {
                return true;
            }
        }
    }
    for (const auto &child : node->children) {
        if (hide_first_typedef_declarator_identifier(child.get())) {
            return true;
        }
    }
    return false;
}

void hide_function_parameter_typedef_names(const ParseTreeNode *node) {
    if (node == nullptr) {
        return;
    }
    if (node->label == "parameter_decl") {
        (void)hide_first_typedef_declarator_identifier(node);
        return;
    }
    for (const auto &child : node->children) {
        hide_function_parameter_typedef_names(child.get());
    }
}

} // namespace sysycc
