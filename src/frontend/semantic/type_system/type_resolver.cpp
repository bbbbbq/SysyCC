#include "frontend/semantic/type_system/type_resolver.hpp"

#include <memory>
#include <vector>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

const SemanticType *TypeResolver::resolve_type(
    const TypeNode *type_node, SemanticContext &semantic_context) const {
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
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(type_node);
        return semantic_model.own_type(std::make_unique<PointerSemanticType>(
            resolve_type(pointer_type->get_pointee_type(), semantic_context)));
    }
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(type_node);
        return semantic_model.own_type(
            std::make_unique<StructSemanticType>(struct_type->get_name()));
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
