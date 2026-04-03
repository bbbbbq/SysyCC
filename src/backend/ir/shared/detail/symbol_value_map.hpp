#pragma once

#include <string>
#include <unordered_map>

namespace sysycc {

class AstNode;

namespace detail {

// Records generated IR values for AST nodes that produce named results.
class SymbolValueMap {
  private:
    std::unordered_map<const AstNode *, std::string> values_;

  public:
    void clear();
    void bind_value(const AstNode *node, std::string value);
    const std::string *get_value(const AstNode *node) const noexcept;
};

} // namespace detail
} // namespace sysycc
