#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace sysycc {

class SemanticSymbol;

namespace detail {

// Represents one lexical scope.
class Scope {
  private:
    std::unordered_map<std::string, const SemanticSymbol *> symbols_;

  public:
    bool define(const SemanticSymbol *symbol);
    const SemanticSymbol *lookup_local(const std::string &name) const noexcept;
};

// Manages nested lexical scopes during semantic analysis.
class ScopeStack {
  private:
    std::vector<Scope> scopes_;

  public:
    void push_scope();
    void pop_scope();

    bool define(const SemanticSymbol *symbol);
    const SemanticSymbol *lookup(const std::string &name) const noexcept;
    bool empty() const noexcept;
};

} // namespace detail
} // namespace sysycc
