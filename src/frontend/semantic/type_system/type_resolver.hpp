#pragma once

#include <memory>
#include <vector>

namespace sysycc {

class Expr;
class SemanticType;
class TypeNode;

namespace detail {

class SemanticContext;
class ScopeStack;

// Resolves AST type nodes into semantic types.
class TypeResolver {
  public:
    const SemanticType *resolve_type(const TypeNode *type_node,
                                     SemanticContext &semantic_context,
                                     const ScopeStack *scope_stack = nullptr) const;
    const SemanticType *apply_array_dimensions(
        const SemanticType *base_type,
        const std::vector<std::unique_ptr<Expr>> &dimensions,
        SemanticContext &semantic_context) const;
};

} // namespace detail
} // namespace sysycc
