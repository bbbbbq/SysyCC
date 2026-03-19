#include "frontend/semantic/model/semantic_model.hpp"

#include <utility>

#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc {

bool SemanticModel::get_success() const noexcept { return success_; }

void SemanticModel::set_success(bool success) noexcept { success_ = success; }

const std::vector<SemanticDiagnostic> &SemanticModel::get_diagnostics() const
    noexcept {
    return diagnostics_;
}

void SemanticModel::add_diagnostic(SemanticDiagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

const SemanticType *SemanticModel::get_node_type(const AstNode *node) const
    noexcept {
    const auto it = node_types_.find(node);
    if (it == node_types_.end()) {
        return nullptr;
    }
    return it->second;
}

void SemanticModel::bind_node_type(const AstNode *node,
                                   const SemanticType *type) {
    node_types_[node] = type;
}

const SemanticSymbol *
SemanticModel::get_symbol_binding(const AstNode *node) const noexcept {
    const auto it = symbol_bindings_.find(node);
    if (it == symbol_bindings_.end()) {
        return nullptr;
    }
    return it->second;
}

void SemanticModel::bind_symbol(const AstNode *node,
                                const SemanticSymbol *symbol) {
    symbol_bindings_[node] = symbol;
}

std::optional<long long>
SemanticModel::get_integer_constant_value(const AstNode *node) const noexcept {
    const auto it = integer_constant_values_.find(node);
    if (it == integer_constant_values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void SemanticModel::bind_integer_constant_value(const AstNode *node,
                                                long long value) {
    integer_constant_values_[node] = value;
}

const SemanticType *SemanticModel::own_type(std::unique_ptr<SemanticType> type) {
    const SemanticType *raw_type = type.get();
    owned_types_.push_back(std::move(type));
    return raw_type;
}

const SemanticSymbol *
SemanticModel::own_symbol(std::unique_ptr<SemanticSymbol> symbol) {
    const SemanticSymbol *raw_symbol = symbol.get();
    owned_symbols_.push_back(std::move(symbol));
    return raw_symbol;
}

} // namespace sysycc
