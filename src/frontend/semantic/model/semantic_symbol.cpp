#include "frontend/semantic/model/semantic_symbol.hpp"

#include <utility>

namespace sysycc {

SemanticSymbol::SemanticSymbol(SymbolKind kind, std::string name,
                               const SemanticType *type,
                               const AstNode *decl_node)
    : kind_(kind), name_(std::move(name)), type_(type), decl_node_(decl_node) {}

SymbolKind SemanticSymbol::get_kind() const noexcept { return kind_; }

const std::string &SemanticSymbol::get_name() const noexcept { return name_; }

const SemanticType *SemanticSymbol::get_type() const noexcept { return type_; }

void SemanticSymbol::set_type(const SemanticType *type) noexcept { type_ = type; }

const AstNode *SemanticSymbol::get_decl_node() const noexcept {
    return decl_node_;
}

} // namespace sysycc
