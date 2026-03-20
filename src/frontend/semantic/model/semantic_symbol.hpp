#pragma once

#include <stdint.h>
#include <string>

namespace sysycc {

class AstNode;
class SemanticType;

enum class SymbolKind : uint8_t {
    Variable,
    Constant,
    Function,
    Parameter,
    Field,
    TypedefName,
    StructName,
    EnumName,
    Enumerator,
};

// Represents one resolved symbol in semantic analysis.
class SemanticSymbol {
  private:
    SymbolKind kind_;
    std::string name_;
    const SemanticType *type_;
    const AstNode *decl_node_;

  public:
    SemanticSymbol(SymbolKind kind, std::string name, const SemanticType *type,
                   const AstNode *decl_node = nullptr);

    SymbolKind get_kind() const noexcept;
    const std::string &get_name() const noexcept;
    const SemanticType *get_type() const noexcept;
    const AstNode *get_decl_node() const noexcept;
};

} // namespace sysycc
