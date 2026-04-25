#include "frontend/semantic/support/scope_stack.hpp"

#include "frontend/semantic/model/semantic_symbol.hpp"

namespace sysycc::detail {

namespace {

bool is_tag_symbol_kind(SymbolKind kind) noexcept {
    return kind == SymbolKind::StructName || kind == SymbolKind::UnionName ||
           kind == SymbolKind::EnumName;
}

} // namespace

bool Scope::define(const SemanticSymbol *symbol) {
    if (symbol == nullptr) {
        return false;
    }
    auto &namespace_symbols = is_tag_symbol_kind(symbol->get_kind())
                                  ? tag_symbols_
                                  : ordinary_symbols_;
    return namespace_symbols.emplace(symbol->get_name(), symbol).second;
}

const SemanticSymbol *Scope::lookup_local(const std::string &name) const
    noexcept {
    const auto it = ordinary_symbols_.find(name);
    if (it == ordinary_symbols_.end()) {
        return nullptr;
    }
    return it->second;
}

const SemanticSymbol *Scope::lookup_tag_local(const std::string &name) const
    noexcept {
    const auto it = tag_symbols_.find(name);
    if (it == tag_symbols_.end()) {
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

const SemanticSymbol *ScopeStack::lookup_local(const std::string &name) const
    noexcept {
    if (scopes_.empty()) {
        return nullptr;
    }
    return scopes_.back().lookup_local(name);
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

const SemanticSymbol *
ScopeStack::lookup_tag_local(const std::string &name) const noexcept {
    if (scopes_.empty()) {
        return nullptr;
    }
    return scopes_.back().lookup_tag_local(name);
}

const SemanticSymbol *ScopeStack::lookup_tag(const std::string &name) const
    noexcept {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const SemanticSymbol *symbol = it->lookup_tag_local(name);
        if (symbol != nullptr) {
            return symbol;
        }
    }
    return nullptr;
}

bool ScopeStack::empty() const noexcept { return scopes_.empty(); }

} // namespace sysycc::detail
