#include "frontend/semantic/support/scope_stack.hpp"

#include "frontend/semantic/model/semantic_symbol.hpp"

namespace sysycc::detail {

bool Scope::define(const SemanticSymbol *symbol) {
    if (symbol == nullptr) {
        return false;
    }
    return symbols_.emplace(symbol->get_name(), symbol).second;
}

const SemanticSymbol *Scope::lookup_local(const std::string &name) const
    noexcept {
    const auto it = symbols_.find(name);
    if (it == symbols_.end()) {
        return nullptr;
    }
    return it->second;
}

void ScopeStack::push_scope() { scopes_.emplace_back(); }

void ScopeStack::pop_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

bool ScopeStack::define(const SemanticSymbol *symbol) {
    if (scopes_.empty()) {
        push_scope();
    }
    return scopes_.back().define(symbol);
}

const SemanticSymbol *ScopeStack::lookup(const std::string &name) const
    noexcept {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const SemanticSymbol *symbol = it->lookup_local(name);
        if (symbol != nullptr) {
            return symbol;
        }
    }
    return nullptr;
}

bool ScopeStack::empty() const noexcept { return scopes_.empty(); }

} // namespace sysycc::detail
