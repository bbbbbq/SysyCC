#include "frontend/ast/detail/ast_builder_context.hpp"

namespace sysycc::detail {

AstBuilderContext::AstBuilderContext(const ParseTreeNode *parse_tree_root)
    : parse_tree_root_(parse_tree_root) {}

const ParseTreeNode *AstBuilderContext::get_parse_tree_root() const noexcept {
    return parse_tree_root_;
}

} // namespace sysycc::detail
