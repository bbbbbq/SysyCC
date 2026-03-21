#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "frontend/semantic/model/semantic_function_attribute.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc {

class AstNode;
class FunctionDecl;

class VariableSemanticInfo {
  private:
    bool is_global_storage_ = false;
    bool has_external_linkage_ = false;
    bool has_tentative_definition_ = false;
    bool has_initialized_definition_ = false;

  public:
    VariableSemanticInfo() = default;

    VariableSemanticInfo(bool is_global_storage, bool has_external_linkage,
                         bool has_tentative_definition,
                         bool has_initialized_definition)
        : is_global_storage_(is_global_storage),
          has_external_linkage_(has_external_linkage),
          has_tentative_definition_(has_tentative_definition),
          has_initialized_definition_(has_initialized_definition) {}

    bool get_is_global_storage() const noexcept { return is_global_storage_; }
    bool get_has_external_linkage() const noexcept {
        return has_external_linkage_;
    }
    bool get_has_tentative_definition() const noexcept {
        return has_tentative_definition_;
    }
    bool get_has_initialized_definition() const noexcept {
        return has_initialized_definition_;
    }
};

// Stores the semantic analysis result associated with one AST.
class SemanticModel {
  private:
    bool success_ = false;
    std::vector<SemanticDiagnostic> diagnostics_;
    std::unordered_map<const AstNode *, const SemanticType *> node_types_;
    std::unordered_map<const AstNode *, const SemanticSymbol *> symbol_bindings_;
    std::unordered_map<const AstNode *, long long> integer_constant_values_;
    std::unordered_map<const FunctionDecl *,
                       std::vector<SemanticFunctionAttribute>>
        function_attributes_;
    std::unordered_map<const SemanticSymbol *, VariableSemanticInfo>
        variable_infos_;
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

    const std::vector<SemanticFunctionAttribute> *
    get_function_attributes(const FunctionDecl *function_decl) const noexcept;
    void bind_function_attributes(
        const FunctionDecl *function_decl,
        std::vector<SemanticFunctionAttribute> attributes);

    const VariableSemanticInfo *
    get_variable_info(const SemanticSymbol *symbol) const noexcept;
    void bind_variable_info(const SemanticSymbol *symbol,
                            VariableSemanticInfo variable_info);

    const SemanticType *own_type(std::unique_ptr<SemanticType> type);
    const SemanticSymbol *own_symbol(std::unique_ptr<SemanticSymbol> symbol);
};

} // namespace sysycc
