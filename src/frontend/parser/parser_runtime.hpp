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

class ParserErrorInfo {
  private:
    std::string message_;
    std::string token_text_;
    SourceSpan source_span_;

  public:
    ParserErrorInfo() = default;

    ParserErrorInfo(std::string message, std::string token_text,
                    SourceSpan source_span)
        : message_(std::move(message)), token_text_(std::move(token_text)),
          source_span_(source_span) {}

    const std::string &get_message() const noexcept { return message_; }
    const std::string &get_token_text() const noexcept { return token_text_; }
    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    bool empty() const noexcept { return message_.empty(); }
};

void parser_runtime_reset();
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void *make_terminal_node(const char *label, const char *text,
                         SourceSpan source_span);
void *make_nonterminal_node(const char *label,
                            std::initializer_list<void *> children);
void set_parse_tree_root(void *root);
std::unique_ptr<ParseTreeNode> take_parse_tree_root();
void set_parser_error_info(ParserErrorInfo error_info);
const ParserErrorInfo &get_parser_error_info() noexcept;
void register_typedef_name(const std::string &name);
void register_typedef_names_from_declarator_list(const ParseTreeNode *node);
void push_typedef_shadow_scope();
void pop_typedef_shadow_scope();
void hide_typedef_names_from_declarator_list(const ParseTreeNode *node);
void hide_function_parameter_typedef_names(const ParseTreeNode *node);
bool is_typedef_name_registered(const std::string &name);

} // namespace sysycc
