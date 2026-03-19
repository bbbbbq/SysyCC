#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc {

class AstNode;

// Stores the semantic analysis result associated with one AST.
class SemanticModel {
  private:
    bool success_ = false;
    std::vector<SemanticDiagnostic> diagnostics_;
    std::unordered_map<const AstNode *, const SemanticType *> node_types_;
    std::unordered_map<const AstNode *, const SemanticSymbol *> symbol_bindings_;
    std::unordered_map<const AstNode *, long long> integer_constant_values_;
    std::vector<std::unique_ptr<SemanticType>> owned_types_;
    std::vector<std::unique_ptr<SemanticSymbol>> owned_symbols_;

  public:
    SemanticModel() = default;

    bool get_success() const noexcept;
    void set_success(bool success) noexcept;

    const std::vector<SemanticDiagnostic> &get_diagnostics() const noexcept;
    void add_diagnostic(SemanticDiagnostic diagnostic);

    const SemanticType *get_node_type(const AstNode *node) const noexcept;
    void bind_node_type(const AstNode *node, const SemanticType *type);

    const SemanticSymbol *get_symbol_binding(const AstNode *node) const noexcept;
    void bind_symbol(const AstNode *node, const SemanticSymbol *symbol);

    std::optional<long long>
    get_integer_constant_value(const AstNode *node) const noexcept;
    void bind_integer_constant_value(const AstNode *node, long long value);

    const SemanticType *own_type(std::unique_ptr<SemanticType> type);
    const SemanticSymbol *own_symbol(std::unique_ptr<SemanticSymbol> symbol);
};

} // namespace sysycc
