#include "frontend/semantic/type_system/type_resolver.hpp"

#include <memory>
#include <vector>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

std::vector<SemanticFieldInfo>
build_semantic_fields(const std::vector<std::unique_ptr<Decl>> &fields,
                      const TypeResolver &type_resolver,
                      SemanticContext &semantic_context) {
    std::vector<SemanticFieldInfo> semantic_fields;
    semantic_fields.reserve(fields.size());
    for (const auto &field : fields) {
        if (field == nullptr || field->get_kind() != AstKind::FieldDecl) {
            continue;
        }
        const auto *field_decl = static_cast<const FieldDecl *>(field.get());
        semantic_fields.emplace_back(
            field_decl->get_name(),
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context, nullptr));
    }
    return semantic_fields;
}

} // namespace

const SemanticType *TypeResolver::resolve_type(
    const TypeNode *type_node, SemanticContext &semantic_context,
    const ScopeStack *scope_stack) const {
    if (type_node == nullptr) {
        return nullptr;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();

    switch (type_node->get_kind()) {
    case AstKind::BuiltinType: {
        const auto *builtin_type =
            static_cast<const BuiltinTypeNode *>(type_node);
        return semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>(builtin_type->get_name()));
    }
    case AstKind::NamedType: {
        const auto *named_type = static_cast<const NamedTypeNode *>(type_node);
        if (scope_stack == nullptr) {
            semantic_model.add_diagnostic(SemanticDiagnostic(
                DiagnosticSeverity::Error,
                "unknown type name: " + named_type->get_name(),
                type_node->get_source_span()));
            return nullptr;
        }
        const SemanticSymbol *symbol =
            scope_stack->lookup(named_type->get_name());
        if (symbol == nullptr || symbol->get_kind() != SymbolKind::TypedefName) {
            semantic_model.add_diagnostic(SemanticDiagnostic(
                DiagnosticSeverity::Error,
                "unknown type name: " + named_type->get_name(),
                type_node->get_source_span()));
            return nullptr;
        }
        return symbol->get_type();
    }
    case AstKind::QualifiedType: {
        const auto *qualified_type =
            static_cast<const QualifiedTypeNode *>(type_node);
        return semantic_model.own_type(std::make_unique<QualifiedSemanticType>(
            qualified_type->get_is_const(),
            resolve_type(qualified_type->get_base_type(), semantic_context,
                         scope_stack)));
    }
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(type_node);
        return semantic_model.own_type(std::make_unique<PointerSemanticType>(
            resolve_type(pointer_type->get_pointee_type(), semantic_context,
                         scope_stack)));
    }
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(type_node);
        if (scope_stack != nullptr) {
            const SemanticSymbol *symbol =
                scope_stack->lookup(struct_type->get_name());
            if (symbol != nullptr && symbol->get_kind() == SymbolKind::StructName) {
                return symbol->get_type();
            }
        }
        return semantic_model.own_type(std::make_unique<StructSemanticType>(
            struct_type->get_name()));
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(type_node);
        if (union_type->get_fields().empty() && scope_stack != nullptr) {
            const SemanticSymbol *symbol =
                scope_stack->lookup(union_type->get_name());
            if (symbol != nullptr && symbol->get_kind() == SymbolKind::UnionName) {
                return symbol->get_type();
            }
        }
        return semantic_model.own_type(std::make_unique<UnionSemanticType>(
            union_type->get_name(),
            build_semantic_fields(union_type->get_fields(), *this,
                                  semantic_context)));
    }
    case AstKind::EnumType: {
        const auto *enum_type = static_cast<const EnumTypeNode *>(type_node);
        return semantic_model.own_type(
            std::make_unique<EnumSemanticType>(enum_type->get_name()));
    }
    default:
        semantic_model.add_diagnostic(SemanticDiagnostic(
            DiagnosticSeverity::Warning,
            "semantic skeleton encountered unknown type",
            type_node->get_source_span()));
        return nullptr;
    }
}

const SemanticType *TypeResolver::apply_array_dimensions(
    const SemanticType *base_type,
    const std::vector<std::unique_ptr<Expr>> &dimensions,
    SemanticContext &semantic_context) const {
    if (base_type == nullptr || dimensions.empty()) {
        return base_type;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    const SemanticType *current_type = base_type;
    for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
        std::vector<int> semantic_dimensions(1, 0);
        current_type = semantic_model.own_type(std::make_unique<ArraySemanticType>(
            current_type, std::move(semantic_dimensions)));
    }
    return current_type;
}

} // namespace sysycc::detail
