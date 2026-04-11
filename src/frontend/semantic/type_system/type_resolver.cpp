#include "frontend/semantic/type_system/type_resolver.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "common/diagnostic/warning_options.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
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
                      SemanticContext &semantic_context,
                      const ScopeStack *scope_stack) {
    std::vector<SemanticFieldInfo> semantic_fields;
    semantic_fields.reserve(fields.size());
    ConstantEvaluator constant_evaluator;
    for (const auto &field : fields) {
        if (field == nullptr || field->get_kind() != AstKind::FieldDecl) {
            continue;
        }
        const auto *field_decl = static_cast<const FieldDecl *>(field.get());
        std::optional<int> bit_width;
        if (field_decl->get_bit_width() != nullptr) {
            const auto width_value = constant_evaluator.get_integer_constant_value(
                field_decl->get_bit_width(), semantic_context);
            if (width_value.has_value()) {
                bit_width = static_cast<int>(*width_value);
            }
        }
        semantic_fields.emplace_back(
            field_decl->get_name(),
            type_resolver.apply_array_dimensions(
                type_resolver.resolve_type(field_decl->get_declared_type(),
                                           semantic_context, scope_stack),
                field_decl->get_dimensions(), semantic_context),
            bit_width);
    }
    return semantic_fields;
}

std::string get_semantic_tag_name(std::string name,
                                  const SourceSpan &source_span) {
    if (!name.empty() && name != "<anonymous>") {
        return name;
    }
    return "<anonymous@" + std::to_string(source_span.get_line_begin()) + ":" +
           std::to_string(source_span.get_col_begin()) + ">";
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
            qualified_type->get_is_volatile(),
            false,
            resolve_type(qualified_type->get_base_type(), semantic_context,
                         scope_stack)));
    }
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(type_node);
        const SemanticType *resolved_pointer =
            semantic_model.own_type(std::make_unique<PointerSemanticType>(
            resolve_type(pointer_type->get_pointee_type(), semantic_context,
                         scope_stack),
            pointer_type->get_nullability_kind()));
        if (pointer_type->get_is_const() || pointer_type->get_is_volatile() ||
            pointer_type->get_is_restrict()) {
            return semantic_model.own_type(std::make_unique<QualifiedSemanticType>(
                pointer_type->get_is_const(), pointer_type->get_is_volatile(),
                pointer_type->get_is_restrict(),
                resolved_pointer));
        }
        return resolved_pointer;
    }
    case AstKind::ArrayType: {
        const auto *array_type = static_cast<const ArrayTypeNode *>(type_node);
        return apply_array_dimensions(
            resolve_type(array_type->get_element_type(), semantic_context,
                         scope_stack),
            array_type->get_dimensions(), semantic_context);
    }
    case AstKind::FunctionType: {
        const auto *function_type =
            static_cast<const FunctionTypeNode *>(type_node);
        std::vector<const SemanticType *> parameter_types;
        parameter_types.reserve(function_type->get_parameter_types().size());
        for (const auto &parameter_type : function_type->get_parameter_types()) {
            parameter_types.push_back(resolve_type(parameter_type.get(),
                                                   semantic_context, scope_stack));
        }
        return semantic_model.own_type(std::make_unique<FunctionSemanticType>(
            resolve_type(function_type->get_return_type(), semantic_context,
                         scope_stack),
            std::move(parameter_types), function_type->get_is_variadic()));
    }
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(type_node);
        if (struct_type->get_fields().empty() && scope_stack != nullptr) {
            const SemanticSymbol *symbol =
                scope_stack->lookup(struct_type->get_name());
            if (symbol != nullptr && symbol->get_kind() == SymbolKind::StructName) {
                return symbol->get_type();
            }
        }
        return semantic_model.own_type(std::make_unique<StructSemanticType>(
            get_semantic_tag_name(struct_type->get_name(),
                                  struct_type->get_source_span()),
            build_semantic_fields(struct_type->get_fields(), *this,
                                  semantic_context, scope_stack)));
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
            get_semantic_tag_name(union_type->get_name(),
                                  union_type->get_source_span()),
            build_semantic_fields(union_type->get_fields(), *this,
                                  semantic_context, scope_stack)));
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
            type_node->get_source_span(), warning_options::kUnknownType));
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
    ConstantEvaluator constant_evaluator;
    const SemanticType *current_type = base_type;
    for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
        std::vector<int> semantic_dimensions(1, 0);
        if (it->get() != nullptr) {
            const std::optional<long long> dimension_value =
                constant_evaluator.get_integer_constant_value(it->get(),
                                                              semantic_context);
            if (dimension_value.has_value() && *dimension_value > 0) {
                semantic_dimensions[0] = static_cast<int>(*dimension_value);
            }
        }
        current_type = semantic_model.own_type(std::make_unique<ArraySemanticType>(
            current_type, std::move(semantic_dimensions)));
    }
    return current_type;
}

const SemanticType *TypeResolver::adjust_parameter_type(
    const SemanticType *type, SemanticContext &semantic_context) const {
    if (type == nullptr) {
        return nullptr;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    if (type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(type);
        return semantic_model.own_type(
            std::make_unique<PointerSemanticType>(array_type->get_element_type()));
    }
    if (type->get_kind() == SemanticTypeKind::Function) {
        return semantic_model.own_type(std::make_unique<PointerSemanticType>(type));
    }
    return type;
}

} // namespace sysycc::detail
