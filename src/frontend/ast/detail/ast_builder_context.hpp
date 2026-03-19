#pragma once

#include "frontend/parser/parser_runtime.hpp"

namespace sysycc::detail {

// Holds the parse tree root while one AST build is in progress.
class AstBuilderContext {
  private:
    const ParseTreeNode *parse_tree_root_ = nullptr;

  public:
    AstBuilderContext() = default;
    explicit AstBuilderContext(const ParseTreeNode *parse_tree_root);

    const ParseTreeNode *get_parse_tree_root() const noexcept;
};

} // namespace sysycc::detail
