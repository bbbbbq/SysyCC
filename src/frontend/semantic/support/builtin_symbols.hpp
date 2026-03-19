#pragma once

namespace sysycc {

class SemanticModel;

namespace detail {

class ScopeStack;

// Registers builtin runtime-library functions into the semantic model/scope.
class BuiltinSymbols {
  public:
    void install(SemanticModel &semantic_model, ScopeStack &scope_stack) const;
};

} // namespace detail
} // namespace sysycc
