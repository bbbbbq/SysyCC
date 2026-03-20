#include "backend/ir/detail/symbol_value_map.hpp"

#include <string>
#include <utility>

namespace sysycc::detail {

void SymbolValueMap::clear() { values_.clear(); }

void SymbolValueMap::bind_value(const AstNode *node, std::string value) {
    values_[node] = std::move(value);
}

const std::string *SymbolValueMap::get_value(const AstNode *node) const noexcept {
    auto iter = values_.find(node);
    if (iter == values_.end()) {
        return nullptr;
    }
    return &iter->second;
}

} // namespace sysycc::detail
