#include "frontend/semantic/model/semantic_model.hpp"

#include <utility>

#include "frontend/ast/ast_node.hpp"
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

void SemanticModel::mark_symbol_used(const SemanticSymbol *symbol) {
    if (symbol == nullptr) {
        return;
    }
    ++symbol_use_counts_[symbol];
}

std::size_t
SemanticModel::get_symbol_use_count(const SemanticSymbol *symbol) const noexcept {
    const auto it = symbol_use_counts_.find(symbol);
    if (it == symbol_use_counts_.end()) {
        return 0;
    }
    return it->second;
}

const std::vector<SemanticFunctionAttribute> *
SemanticModel::get_function_attributes(
    const FunctionDecl *function_decl) const noexcept {
    const auto it = function_attributes_.find(function_decl);
    if (it == function_attributes_.end()) {
        return nullptr;
    }
    return &it->second;
}

void SemanticModel::bind_function_attributes(
    const FunctionDecl *function_decl,
    std::vector<SemanticFunctionAttribute> attributes) {
    function_attributes_[function_decl] = std::move(attributes);
}

const VariableSemanticInfo *
SemanticModel::get_variable_info(const SemanticSymbol *symbol) const noexcept {
    const auto it = variable_infos_.find(symbol);
    if (it == variable_infos_.end()) {
        return nullptr;
    }
    return &it->second;
}

void SemanticModel::bind_variable_info(const SemanticSymbol *symbol,
                                       VariableSemanticInfo variable_info) {
    variable_infos_[symbol] = std::move(variable_info);
}

const SemanticType *SemanticModel::own_type(std::unique_ptr<SemanticType> type) {
    owned_types_.push_back(std::move(type));
    return owned_types_.back().get();
}

const SemanticSymbol *
SemanticModel::own_symbol(std::unique_ptr<SemanticSymbol> symbol) {
    owned_symbols_.push_back(std::move(symbol));
    return owned_symbols_.back().get();
}

} // namespace sysycc
