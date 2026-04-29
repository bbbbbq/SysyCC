#include "frontend/semantic/analysis/decl_analyzer.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/diagnostic/warning_options.hpp"
#include "common/string_literal.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/analysis/expr_analyzer.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"

namespace sysycc::detail {

namespace {

const SemanticType *strip_qualifiers(const SemanticType *type);
bool is_incomplete_named_struct_semantic_type(const SemanticType *type,
                                              const std::string &name);
bool completes_incomplete_named_aggregate_type(
    const SemanticType *existing_type, const SemanticType *declared_type);
void analyze_aggregate_field_constant_expressions(
    const std::vector<std::unique_ptr<Decl>> &field_decls,
    const ExprAnalyzer &expr_analyzer, SemanticContext &semantic_context,
    ScopeStack &scope_stack);

void collect_inline_struct_type_nodes(
    const TypeNode *type_node, std::vector<const StructTypeNode *> &nodes) {
    if (type_node == nullptr) {
        return;
    }
    switch (type_node->get_kind()) {
    case AstKind::StructType: {
        const auto *struct_type =
            static_cast<const StructTypeNode *>(type_node);
        if (!struct_type->get_fields().empty()) {
            nodes.push_back(struct_type);
            for (const auto &field : struct_type->get_fields()) {
                if (field != nullptr &&
                    field->get_kind() == AstKind::FieldDecl) {
                    collect_inline_struct_type_nodes(
                        static_cast<const FieldDecl *>(field.get())
                            ->get_declared_type(),
                        nodes);
                }
            }
        }
        return;
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(type_node);
        for (const auto &field : union_type->get_fields()) {
            if (field != nullptr && field->get_kind() == AstKind::FieldDecl) {
                collect_inline_struct_type_nodes(
                    static_cast<const FieldDecl *>(field.get())
                        ->get_declared_type(),
                    nodes);
            }
        }
        return;
    }
    case AstKind::QualifiedType:
        collect_inline_struct_type_nodes(
            static_cast<const QualifiedTypeNode *>(type_node)->get_base_type(),
            nodes);
        return;
    case AstKind::PointerType:
        collect_inline_struct_type_nodes(
            static_cast<const PointerTypeNode *>(type_node)->get_pointee_type(),
            nodes);
        return;
    case AstKind::ArrayType:
        collect_inline_struct_type_nodes(
            static_cast<const ArrayTypeNode *>(type_node)->get_element_type(),
            nodes);
        return;
    case AstKind::FunctionType: {
        const auto *function_type =
            static_cast<const FunctionTypeNode *>(type_node);
        collect_inline_struct_type_nodes(function_type->get_return_type(),
                                         nodes);
        for (const auto &parameter_type :
             function_type->get_parameter_types()) {
            collect_inline_struct_type_nodes(parameter_type.get(), nodes);
        }
        return;
    }
    default:
        return;
    }
}

void analyze_type_operand_expressions(const TypeNode *type_node,
                                      const ExprAnalyzer &expr_analyzer,
                                      SemanticContext &semantic_context,
                                      ScopeStack &scope_stack) {
    if (type_node == nullptr) {
        return;
    }

    switch (type_node->get_kind()) {
    case AstKind::TypeofType: {
        const auto *typeof_type =
            static_cast<const TypeofTypeNode *>(type_node);
        expr_analyzer.analyze_expr(typeof_type->get_operand(), semantic_context,
                                   scope_stack);
        return;
    }
    case AstKind::QualifiedType:
        analyze_type_operand_expressions(
            static_cast<const QualifiedTypeNode *>(type_node)->get_base_type(),
            expr_analyzer, semantic_context, scope_stack);
        return;
    case AstKind::PointerType:
        analyze_type_operand_expressions(
            static_cast<const PointerTypeNode *>(type_node)->get_pointee_type(),
            expr_analyzer, semantic_context, scope_stack);
        return;
    case AstKind::ArrayType: {
        const auto *array_type = static_cast<const ArrayTypeNode *>(type_node);
        analyze_type_operand_expressions(array_type->get_element_type(),
                                         expr_analyzer, semantic_context,
                                         scope_stack);
        for (const auto &dimension : array_type->get_dimensions()) {
            expr_analyzer.analyze_expr(dimension.get(), semantic_context,
                                       scope_stack);
        }
        return;
    }
    case AstKind::FunctionType: {
        const auto *function_type =
            static_cast<const FunctionTypeNode *>(type_node);
        analyze_type_operand_expressions(function_type->get_return_type(),
                                         expr_analyzer, semantic_context,
                                         scope_stack);
        for (const auto &parameter_type :
             function_type->get_parameter_types()) {
            analyze_type_operand_expressions(parameter_type.get(),
                                             expr_analyzer, semantic_context,
                                             scope_stack);
        }
        return;
    }
    case AstKind::StructType: {
        const auto *struct_type =
            static_cast<const StructTypeNode *>(type_node);
        if (!struct_type->get_fields().empty()) {
            analyze_aggregate_field_constant_expressions(
                struct_type->get_fields(), expr_analyzer, semantic_context,
                scope_stack);
        }
        return;
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(type_node);
        if (!union_type->get_fields().empty()) {
            analyze_aggregate_field_constant_expressions(
                union_type->get_fields(), expr_analyzer, semantic_context,
                scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

void analyze_inline_type_declarations_impl(const TypeNode *type_node,
                                           const DeclAnalyzer &decl_analyzer,
                                           SemanticContext &semantic_context,
                                           ScopeStack &scope_stack) {
    if (type_node == nullptr) {
        return;
    }

    switch (type_node->get_kind()) {
    case AstKind::QualifiedType: {
        const auto *qualified_type =
            static_cast<const QualifiedTypeNode *>(type_node);
        analyze_inline_type_declarations_impl(qualified_type->get_base_type(),
                                              decl_analyzer, semantic_context,
                                              scope_stack);
        return;
    }
    case AstKind::PointerType: {
        const auto *pointer_type =
            static_cast<const PointerTypeNode *>(type_node);
        analyze_inline_type_declarations_impl(pointer_type->get_pointee_type(),
                                              decl_analyzer, semantic_context,
                                              scope_stack);
        return;
    }
    case AstKind::ArrayType: {
        const auto *array_type = static_cast<const ArrayTypeNode *>(type_node);
        analyze_inline_type_declarations_impl(array_type->get_element_type(),
                                              decl_analyzer, semantic_context,
                                              scope_stack);
        return;
    }
    case AstKind::FunctionType: {
        const auto *function_type =
            static_cast<const FunctionTypeNode *>(type_node);
        analyze_inline_type_declarations_impl(function_type->get_return_type(),
                                              decl_analyzer, semantic_context,
                                              scope_stack);
        for (const auto &parameter_type :
             function_type->get_parameter_types()) {
            analyze_inline_type_declarations_impl(
                parameter_type.get(), decl_analyzer, semantic_context,
                scope_stack);
        }
        return;
    }
    case AstKind::EnumType: {
        const auto *enum_type = static_cast<const EnumTypeNode *>(type_node);
        decl_analyzer.analyze_enum_enumerators(enum_type->get_enumerators(),
                                               semantic_context, scope_stack);
        return;
    }
    case AstKind::StructType: {
        const auto *struct_type =
            static_cast<const StructTypeNode *>(type_node);
        for (const auto &field : struct_type->get_fields()) {
            decl_analyzer.analyze_decl(field.get(), semantic_context,
                                       scope_stack);
        }
        return;
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(type_node);
        for (const auto &field : union_type->get_fields()) {
            decl_analyzer.analyze_decl(field.get(), semantic_context,
                                       scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

void register_inline_struct_tag(const FieldDecl *field_decl,
                                const SemanticType *field_type,
                                const TypeResolver &type_resolver,
                                SemanticContext &semantic_context,
                                ScopeStack &scope_stack) {
    (void)field_type;
    std::vector<const StructTypeNode *> struct_type_nodes;
    collect_inline_struct_type_nodes(
        field_decl != nullptr ? field_decl->get_declared_type() : nullptr,
        struct_type_nodes);
    if (struct_type_nodes.empty()) {
        return;
    }

    for (const StructTypeNode *struct_type_node : struct_type_nodes) {
        if (struct_type_node == nullptr ||
            struct_type_node->get_name().empty() ||
            struct_type_node->get_name() == "<anonymous>") {
            continue;
        }

        const auto *struct_type = dynamic_cast<const StructSemanticType *>(
            strip_qualifiers(type_resolver.resolve_type(
                struct_type_node, semantic_context, &scope_stack)));
        if (struct_type == nullptr) {
            continue;
        }

        if (const SemanticSymbol *tag_symbol =
                scope_stack.lookup_tag_local(struct_type_node->get_name());
            tag_symbol != nullptr &&
            tag_symbol->get_kind() == SymbolKind::StructName) {
            if (is_incomplete_named_struct_semantic_type(
                    tag_symbol->get_type(), struct_type_node->get_name())) {
                const_cast<SemanticSymbol *>(tag_symbol)->set_type(struct_type);
            }
            continue;
        }

        const auto *symbol = semantic_context.get_semantic_model().own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::StructName,
                                             struct_type_node->get_name(),
                                             struct_type, nullptr));
        scope_stack.define(symbol);
    }
}

std::vector<SemanticFieldInfo> build_aggregate_semantic_fields(
    const std::vector<std::unique_ptr<Decl>> &field_decls,
    const TypeResolver &type_resolver,
    const ConstantEvaluator &constant_evaluator,
    const ConversionChecker &conversion_checker,
    SemanticContext &semantic_context, ScopeStack &scope_stack) {
    std::vector<SemanticFieldInfo> fields;
    fields.reserve(field_decls.size());
    for (const auto &field : field_decls) {
        if (field == nullptr || field->get_kind() != AstKind::FieldDecl) {
            continue;
        }
        const auto *field_decl = static_cast<const FieldDecl *>(field.get());
        std::optional<int> bit_width;
        if (field_decl->get_bit_width() != nullptr) {
            const auto width_value =
                constant_evaluator.get_integer_constant_value(
                    field_decl->get_bit_width(), semantic_context);
            if (width_value.has_value()) {
                bit_width = static_cast<int>(*width_value);
            }
        }
        const SemanticType *field_type = type_resolver.apply_array_dimensions(
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context, &scope_stack),
            field_decl->get_dimensions(), semantic_context);
        register_inline_struct_tag(field_decl, field_type, type_resolver,
                                   semantic_context, scope_stack);
        fields.emplace_back(field_decl->get_name(), field_type, bit_width);
    }
    return fields;
}

std::vector<SemanticFieldInfo> build_struct_semantic_fields(
    const StructDecl *struct_decl, const TypeResolver &type_resolver,
    const ConstantEvaluator &constant_evaluator,
    const ConversionChecker &conversion_checker,
    SemanticContext &semantic_context, ScopeStack &scope_stack) {
    if (struct_decl == nullptr) {
        return {};
    }
    return build_aggregate_semantic_fields(
        struct_decl->get_fields(), type_resolver, constant_evaluator,
        conversion_checker, semantic_context, scope_stack);
}

std::vector<SemanticFieldInfo> build_union_semantic_fields(
    const UnionDecl *union_decl, const TypeResolver &type_resolver,
    const ConstantEvaluator &constant_evaluator,
    const ConversionChecker &conversion_checker,
    SemanticContext &semantic_context, ScopeStack &scope_stack) {
    if (union_decl == nullptr) {
        return {};
    }
    return build_aggregate_semantic_fields(
        union_decl->get_fields(), type_resolver, constant_evaluator,
        conversion_checker, semantic_context, scope_stack);
}

void analyze_aggregate_field_constant_expressions(
    const std::vector<std::unique_ptr<Decl>> &field_decls,
    const ExprAnalyzer &expr_analyzer, SemanticContext &semantic_context,
    ScopeStack &scope_stack) {
    auto analyze_type_constant_expressions = [&](const TypeNode *type_node,
                                                 const auto &self) -> void {
        if (type_node == nullptr) {
            return;
        }

        switch (type_node->get_kind()) {
        case AstKind::QualifiedType: {
            const auto *qualified_type =
                static_cast<const QualifiedTypeNode *>(type_node);
            self(qualified_type->get_base_type(), self);
            return;
        }
        case AstKind::PointerType: {
            const auto *pointer_type =
                static_cast<const PointerTypeNode *>(type_node);
            self(pointer_type->get_pointee_type(), self);
            return;
        }
        case AstKind::ArrayType: {
            const auto *array_type =
                static_cast<const ArrayTypeNode *>(type_node);
            self(array_type->get_element_type(), self);
            for (const auto &dimension : array_type->get_dimensions()) {
                if (dimension != nullptr) {
                    expr_analyzer.analyze_expr(dimension.get(),
                                               semantic_context, scope_stack);
                }
            }
            return;
        }
        case AstKind::FunctionType: {
            const auto *function_type =
                static_cast<const FunctionTypeNode *>(type_node);
            self(function_type->get_return_type(), self);
            for (const auto &parameter_type :
                 function_type->get_parameter_types()) {
                self(parameter_type.get(), self);
            }
            return;
        }
        case AstKind::StructType: {
            const auto *struct_type =
                static_cast<const StructTypeNode *>(type_node);
            if (!struct_type->get_fields().empty()) {
                analyze_aggregate_field_constant_expressions(
                    struct_type->get_fields(), expr_analyzer, semantic_context,
                    scope_stack);
            }
            return;
        }
        case AstKind::UnionType: {
            const auto *union_type =
                static_cast<const UnionTypeNode *>(type_node);
            if (!union_type->get_fields().empty()) {
                analyze_aggregate_field_constant_expressions(
                    union_type->get_fields(), expr_analyzer, semantic_context,
                    scope_stack);
            }
            return;
        }
        default:
            return;
        }
    };

    for (const auto &field : field_decls) {
        if (field == nullptr || field->get_kind() != AstKind::FieldDecl) {
            continue;
        }
        const auto *field_decl = static_cast<const FieldDecl *>(field.get());
        analyze_type_constant_expressions(field_decl->get_declared_type(),
                                          analyze_type_constant_expressions);
        for (const auto &dimension : field_decl->get_dimensions()) {
            if (dimension != nullptr) {
                expr_analyzer.analyze_expr(dimension.get(), semantic_context,
                                           scope_stack);
            }
        }
        if (field_decl->get_bit_width() != nullptr) {
            expr_analyzer.analyze_expr(field_decl->get_bit_width(),
                                       semantic_context, scope_stack);
        }
    }
}

bool is_global_variable_definition(const VarDecl *var_decl) {
    if (var_decl == nullptr) {
        return false;
    }
    return !var_decl->get_is_extern() || var_decl->get_initializer() != nullptr;
}

bool is_system_header_symbol(const SemanticSymbol *symbol,
                             const SemanticContext &semantic_context) {
    if (symbol == nullptr || symbol->get_decl_node() == nullptr) {
        return false;
    }
    return semantic_context.is_system_header_span(
        symbol->get_decl_node()->get_source_span());
}

bool is_anonymous_tag_name(const std::string &name) {
    return name.empty() || name.rfind("<anonymous", 0) == 0;
}

std::string get_semantic_tag_name(std::string name,
                                  const SourceSpan &source_span) {
    if (!is_anonymous_tag_name(name)) {
        return name;
    }
    return "<anonymous@" + std::to_string(source_span.get_line_begin()) + ":" +
           std::to_string(source_span.get_col_begin()) + ">";
}

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current = static_cast<const QualifiedSemanticType *>(current)
                      ->get_base_type();
    }
    return current;
}

bool is_character_semantic_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name =
        static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "char" || name == "signed char" || name == "unsigned char";
}

bool is_incomplete_array_semantic_type(const SemanticType *type) {
    const auto *array_type =
        dynamic_cast<const ArraySemanticType *>(strip_qualifiers(type));
    return array_type != nullptr && array_type->get_dimensions().size() == 1 &&
           array_type->get_dimensions().front() == 0;
}

bool is_incomplete_named_struct_semantic_type(const SemanticType *type,
                                              const std::string &name) {
    const auto *struct_type =
        dynamic_cast<const StructSemanticType *>(strip_qualifiers(type));
    return struct_type != nullptr && struct_type->get_name() == name &&
           struct_type->get_fields().empty();
}

bool is_incomplete_named_union_semantic_type(const SemanticType *type,
                                             const std::string &name) {
    const auto *union_type =
        dynamic_cast<const UnionSemanticType *>(strip_qualifiers(type));
    return union_type != nullptr && union_type->get_name() == name &&
           union_type->get_fields().empty();
}

bool completes_incomplete_named_aggregate_type(
    const SemanticType *existing_type, const SemanticType *declared_type) {
    const SemanticType *existing = strip_qualifiers(existing_type);
    const SemanticType *declared = strip_qualifiers(declared_type);
    if (const auto *existing_struct =
            dynamic_cast<const StructSemanticType *>(existing);
        existing_struct != nullptr && existing_struct->get_fields().empty()) {
        const auto *declared_struct =
            dynamic_cast<const StructSemanticType *>(declared);
        return declared_struct != nullptr &&
               declared_struct->get_name() == existing_struct->get_name() &&
               !declared_struct->get_fields().empty();
    }
    if (const auto *existing_union =
            dynamic_cast<const UnionSemanticType *>(existing);
        existing_union != nullptr && existing_union->get_fields().empty()) {
        const auto *declared_union =
            dynamic_cast<const UnionSemanticType *>(declared);
        return declared_union != nullptr &&
               declared_union->get_name() == existing_union->get_name() &&
               !declared_union->get_fields().empty();
    }
    return false;
}

const SemanticType *complete_incomplete_char_array_from_string_initializer(
    const SemanticType *declared_type, const Expr *initializer,
    SemanticModel &semantic_model) {
    if (declared_type == nullptr || initializer == nullptr ||
        initializer->get_kind() != AstKind::StringLiteralExpr) {
        return declared_type;
    }

    const auto *array_type = dynamic_cast<const ArraySemanticType *>(
        strip_qualifiers(declared_type));
    if (array_type == nullptr ||
        !is_character_semantic_type(array_type->get_element_type()) ||
        array_type->get_dimensions().size() != 1 ||
        array_type->get_dimensions().front() != 0) {
        return declared_type;
    }

    const auto *string_literal =
        static_cast<const StringLiteralExpr *>(initializer);
    const std::string decoded_text =
        decode_string_literal_token(string_literal->get_value_text());
    return semantic_model.own_type(std::make_unique<ArraySemanticType>(
        array_type->get_element_type(),
        std::vector<int>{static_cast<int>(decoded_text.size() + 1)}));
}

const SemanticType *complete_incomplete_array_from_initializer_list(
    const SemanticType *declared_type, const Expr *initializer,
    SemanticModel &semantic_model) {
    if (declared_type == nullptr || initializer == nullptr ||
        initializer->get_kind() != AstKind::InitListExpr) {
        return declared_type;
    }

    const auto *array_type = dynamic_cast<const ArraySemanticType *>(
        strip_qualifiers(declared_type));
    if (array_type == nullptr || array_type->get_dimensions().size() != 1 ||
        array_type->get_dimensions().front() != 0) {
        return declared_type;
    }

    const auto *init_list = static_cast<const InitListExpr *>(initializer);
    return semantic_model.own_type(std::make_unique<ArraySemanticType>(
        array_type->get_element_type(),
        std::vector<int>{static_cast<int>(init_list->get_elements().size())}));
}

} // namespace

DeclAnalyzer::DeclAnalyzer(const TypeResolver &type_resolver,
                           const ConversionChecker &conversion_checker,
                           const ConstantEvaluator &constant_evaluator,
                           const ExprAnalyzer &expr_analyzer)
    : type_resolver_(type_resolver), conversion_checker_(conversion_checker),
      constant_evaluator_(constant_evaluator), expr_analyzer_(expr_analyzer) {}

void DeclAnalyzer::add_error(SemanticContext &semantic_context,
                             std::string message,
                             const SourceSpan &source_span) const {
    semantic_context.get_semantic_model().add_diagnostic(SemanticDiagnostic(
        DiagnosticSeverity::Error, std::move(message), source_span));
}

bool DeclAnalyzer::define_symbol(SemanticContext &semantic_context,
                                 ScopeStack &scope_stack,
                                 const SemanticSymbol *symbol,
                                 const SourceSpan &source_span) const {
    if (symbol == nullptr) {
        return false;
    }
    if (scope_stack.define(symbol)) {
        return true;
    }
    add_error(semantic_context, "redefinition of symbol: " + symbol->get_name(),
              source_span);
    return false;
}

void DeclAnalyzer::analyze_inline_type_declarations(
    const TypeNode *type_node, SemanticContext &semantic_context,
    ScopeStack &scope_stack) const {
    analyze_inline_type_declarations_impl(type_node, *this, semantic_context,
                                          scope_stack);
}

void DeclAnalyzer::analyze_enum_enumerators(
    const std::vector<std::unique_ptr<Decl>> &enumerators,
    SemanticContext &semantic_context, ScopeStack &scope_stack) const {
    long long next_enumerator_value = 0;
    for (const auto &enumerator : enumerators) {
        analyze_decl(enumerator.get(), semantic_context, scope_stack);
        const auto *enumerator_decl =
            dynamic_cast<const EnumeratorDecl *>(enumerator.get());
        if (enumerator_decl == nullptr) {
            continue;
        }
        auto integer_constant_value =
            constant_evaluator_.get_integer_constant_value(enumerator.get(),
                                                           semantic_context);
        if (!integer_constant_value.has_value() &&
            enumerator_decl->get_value() == nullptr) {
            integer_constant_value = next_enumerator_value;
            constant_evaluator_.bind_integer_constant_value(
                enumerator.get(), *integer_constant_value, semantic_context);
        }
        if (integer_constant_value.has_value()) {
            next_enumerator_value = *integer_constant_value + 1;
        }
    }
}

void DeclAnalyzer::analyze_decl(const Decl *decl,
                                SemanticContext &semantic_context,
                                ScopeStack &scope_stack) const {
    if (decl == nullptr) {
        return;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();

    switch (decl->get_kind()) {
    case AstKind::ParamDecl: {
        const auto *param_decl = static_cast<const ParamDecl *>(decl);
        analyze_inline_type_declarations_impl(param_decl->get_declared_type(),
                                              *this, semantic_context,
                                              scope_stack);
        analyze_type_operand_expressions(param_decl->get_declared_type(),
                                         expr_analyzer_, semantic_context,
                                         scope_stack);
        for (const auto &dimension : param_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(
                    semantic_context,
                    "array dimension must be an integer constant expression",
                    dimension->get_source_span());
            }
        }
        const SemanticType *declared_type =
            type_resolver_.apply_array_dimensions(
                type_resolver_.resolve_type(param_decl->get_declared_type(),
                                            semantic_context, &scope_stack),
                param_decl->get_dimensions(), semantic_context);
        declared_type = type_resolver_.adjust_parameter_type(declared_type,
                                                             semantic_context);
        semantic_model.bind_node_type(param_decl, declared_type);
        if (param_decl->get_name().empty()) {
            return;
        }
        const auto *symbol =
            semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::Parameter, param_decl->get_name(), declared_type,
                param_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          param_decl->get_source_span())) {
            semantic_model.bind_symbol(param_decl, symbol);
            semantic_context.record_function_local_symbol(symbol);
        }
        return;
    }
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        analyze_inline_type_declarations_impl(var_decl->get_declared_type(),
                                              *this, semantic_context,
                                              scope_stack);
        analyze_type_operand_expressions(var_decl->get_declared_type(),
                                         expr_analyzer_, semantic_context,
                                         scope_stack);
        for (const auto &dimension : var_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(
                    semantic_context,
                    "array dimension must be an integer constant expression",
                    dimension->get_source_span());
            }
        }
        const SemanticType *declared_type =
            type_resolver_.apply_array_dimensions(
                type_resolver_.resolve_type(var_decl->get_declared_type(),
                                            semantic_context, &scope_stack),
                var_decl->get_dimensions(), semantic_context);
        declared_type = complete_incomplete_char_array_from_string_initializer(
            declared_type, var_decl->get_initializer(), semantic_model);
        declared_type = complete_incomplete_array_from_initializer_list(
            declared_type, var_decl->get_initializer(), semantic_model);
        const bool is_file_scope =
            semantic_context.get_current_function() == nullptr;
        const bool has_initializer = var_decl->get_initializer() != nullptr;
        const bool has_static_storage_duration =
            is_file_scope || var_decl->get_is_static();
        const bool is_tentative_definition =
            is_file_scope && !var_decl->get_is_extern() && !has_initializer;
        const bool is_initialized_definition = is_file_scope && has_initializer;
        const bool has_internal_linkage =
            is_file_scope && var_decl->get_is_static();
        const bool has_external_linkage =
            is_file_scope && !var_decl->get_is_static();
        const bool has_nonlocal_storage =
            has_static_storage_duration || var_decl->get_is_extern();
        const bool uses_linked_storage_identity =
            is_file_scope || var_decl->get_is_extern();

        const SemanticSymbol *symbol = nullptr;
        if (uses_linked_storage_identity) {
            const SemanticSymbol *existing_symbol =
                is_file_scope ? scope_stack.lookup_local(var_decl->get_name())
                              : scope_stack.lookup(var_decl->get_name());
            if (existing_symbol != nullptr &&
                existing_symbol->get_kind() == SymbolKind::Variable) {
                if (!conversion_checker_.is_same_type(
                        existing_symbol->get_type(), declared_type)) {
                    add_error(semantic_context,
                              "redefinition of symbol: " + var_decl->get_name(),
                              var_decl->get_source_span());
                    break;
                }
                const VariableSemanticInfo *existing_info =
                    semantic_model.get_variable_info(existing_symbol);
                if (existing_info != nullptr &&
                    existing_info->get_has_initialized_definition() &&
                    is_initialized_definition) {
                    add_error(semantic_context,
                              "redefinition of symbol: " + var_decl->get_name(),
                              var_decl->get_source_span());
                    break;
                }

                VariableSemanticInfo updated_info(
                    has_nonlocal_storage,
                    (existing_info != nullptr &&
                     existing_info->get_has_external_linkage()) ||
                        has_external_linkage,
                    (existing_info != nullptr &&
                     existing_info->get_has_internal_linkage()) ||
                        has_internal_linkage,
                    (existing_info != nullptr &&
                     existing_info->get_has_tentative_definition()) ||
                        is_tentative_definition,
                    (existing_info != nullptr &&
                     existing_info->get_has_initialized_definition()) ||
                        is_initialized_definition);
                semantic_model.bind_variable_info(existing_symbol,
                                                  updated_info);
                if (is_incomplete_array_semantic_type(
                        existing_symbol->get_type()) &&
                    !is_incomplete_array_semantic_type(declared_type)) {
                    const_cast<SemanticSymbol *>(existing_symbol)
                        ->set_type(declared_type);
                }
                if (completes_incomplete_named_aggregate_type(
                        existing_symbol->get_type(), declared_type)) {
                    const_cast<SemanticSymbol *>(existing_symbol)
                        ->set_type(declared_type);
                }
                symbol = existing_symbol;
            }
        }

        if (symbol == nullptr) {
            symbol = semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::Variable, var_decl->get_name(), declared_type,
                var_decl));
            if (define_symbol(semantic_context, scope_stack, symbol,
                              var_decl->get_source_span())) {
                semantic_model.bind_variable_info(
                    symbol, VariableSemanticInfo(
                                has_nonlocal_storage, has_external_linkage,
                                has_internal_linkage, is_tentative_definition,
                                is_initialized_definition));
            } else {
                break;
            }
        }

        semantic_model.bind_symbol(var_decl, symbol);
        semantic_model.bind_node_type(var_decl, declared_type);
        if (!is_file_scope) {
            semantic_context.record_function_local_symbol(symbol);
        }
        expr_analyzer_.analyze_expr(var_decl->get_initializer(),
                                    semantic_context, scope_stack);
        if (has_nonlocal_storage && var_decl->get_initializer() != nullptr &&
            !constant_evaluator_.is_static_storage_initializer(
                var_decl->get_initializer(), declared_type, semantic_model)) {
            add_error(semantic_context,
                      "initializer is not a valid static initializer",
                      var_decl->get_initializer()->get_source_span());
        }
        if (var_decl->get_initializer() != nullptr &&
            var_decl->get_initializer()->get_kind() != AstKind::InitListExpr) {
            const SemanticType *initializer_type =
                semantic_model.get_node_type(var_decl->get_initializer());
            if (initializer_type != nullptr &&
                !conversion_checker_.is_assignable_value(
                    declared_type, initializer_type,
                    var_decl->get_initializer(), semantic_context,
                    constant_evaluator_)) {
                add_error(semantic_context,
                          "initializer type does not match declared type",
                          var_decl->get_initializer()->get_source_span());
            } else if (initializer_type != nullptr &&
                       var_decl->get_initializer()->get_kind() !=
                           AstKind::CastExpr &&
                       conversion_checker_
                           .should_warn_implicit_integer_narrowing(
                               declared_type, initializer_type,
                               constant_evaluator_.get_integer_constant_value(
                                   var_decl->get_initializer(),
                                   semantic_context))) {
                semantic_context.get_semantic_model().add_diagnostic(
                    SemanticDiagnostic(
                        DiagnosticSeverity::Warning,
                        "implicit integer conversion may change value",
                        var_decl->get_initializer()->get_source_span(),
                        warning_options::kConversion));
            }
        }
        return;
    }
    case AstKind::ConstDecl: {
        const auto *const_decl = static_cast<const ConstDecl *>(decl);
        analyze_inline_type_declarations_impl(const_decl->get_declared_type(),
                                              *this, semantic_context,
                                              scope_stack);
        analyze_type_operand_expressions(const_decl->get_declared_type(),
                                         expr_analyzer_, semantic_context,
                                         scope_stack);
        for (const auto &dimension : const_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(
                    semantic_context,
                    "array dimension must be an integer constant expression",
                    dimension->get_source_span());
            }
        }
        const SemanticType *declared_type =
            type_resolver_.apply_array_dimensions(
                type_resolver_.resolve_type(const_decl->get_declared_type(),
                                            semantic_context, &scope_stack),
                const_decl->get_dimensions(), semantic_context);
        declared_type = complete_incomplete_char_array_from_string_initializer(
            declared_type, const_decl->get_initializer(), semantic_model);
        declared_type = complete_incomplete_array_from_initializer_list(
            declared_type, const_decl->get_initializer(), semantic_model);
        if (semantic_context.get_current_function() == nullptr) {
            const SemanticSymbol *existing_symbol =
                scope_stack.lookup_local(const_decl->get_name());
            if (existing_symbol != nullptr &&
                existing_symbol->get_kind() == SymbolKind::Variable &&
                conversion_checker_.is_same_type(existing_symbol->get_type(),
                                                 declared_type)) {
                const VariableSemanticInfo *existing_info =
                    semantic_model.get_variable_info(existing_symbol);
                if (existing_info != nullptr &&
                    existing_info->get_has_initialized_definition()) {
                    add_error(semantic_context,
                              "redefinition of symbol: " +
                                  const_decl->get_name(),
                              const_decl->get_source_span());
                    break;
                }
                semantic_model.bind_variable_info(
                    existing_symbol,
                    VariableSemanticInfo(
                        true,
                        existing_info == nullptr ||
                            existing_info->get_has_external_linkage(),
                        existing_info != nullptr &&
                            existing_info->get_has_internal_linkage(),
                        existing_info != nullptr &&
                            existing_info->get_has_tentative_definition(),
                        true));
                if (is_incomplete_array_semantic_type(
                        existing_symbol->get_type()) &&
                    !is_incomplete_array_semantic_type(declared_type)) {
                    const_cast<SemanticSymbol *>(existing_symbol)
                        ->set_type(declared_type);
                }
                semantic_model.bind_symbol(const_decl, existing_symbol);
                semantic_model.bind_node_type(const_decl, declared_type);
                expr_analyzer_.analyze_expr(const_decl->get_initializer(),
                                            semantic_context, scope_stack);
                return;
            }
        }
        const auto *symbol =
            semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::Constant, const_decl->get_name(), declared_type,
                const_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          const_decl->get_source_span())) {
            semantic_model.bind_symbol(const_decl, symbol);
            semantic_model.bind_node_type(const_decl, declared_type);
        }
        expr_analyzer_.analyze_expr(const_decl->get_initializer(),
                                    semantic_context, scope_stack);
        if (semantic_context.get_current_function() == nullptr &&
            const_decl->get_initializer() != nullptr &&
            !constant_evaluator_.is_static_storage_initializer(
                const_decl->get_initializer(), declared_type, semantic_model)) {
            add_error(semantic_context,
                      "initializer is not a valid static initializer",
                      const_decl->get_initializer()->get_source_span());
        }
        if (const_decl->get_initializer() != nullptr &&
            !constant_evaluator_.is_integer_constant_expr(
                const_decl->get_initializer(), semantic_context,
                conversion_checker_) &&
            conversion_checker_.is_integer_like_type(declared_type)) {
            const auto converted_constant =
                constant_evaluator_.get_scalar_constant_value_as_integer(
                    const_decl->get_initializer(), declared_type,
                    semantic_context);
            if (converted_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    const_decl, *converted_constant, semantic_context);
                constant_evaluator_.bind_integer_constant_value(
                    const_decl->get_initializer(), *converted_constant,
                    semantic_context);
            } else if (semantic_context.get_current_function() == nullptr) {
                add_error(
                    semantic_context,
                    "const initializer must be an integer constant expression",
                    const_decl->get_initializer()->get_source_span());
            }
        }
        if (conversion_checker_.is_integer_like_type(declared_type)) {
            const auto integer_constant_value =
                constant_evaluator_.get_integer_constant_value(
                    const_decl->get_initializer(), semantic_context);
            if (integer_constant_value.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    const_decl, *integer_constant_value, semantic_context);
            }
        }
        return;
    }
    case AstKind::FieldDecl: {
        const auto *field_decl = static_cast<const FieldDecl *>(decl);
        analyze_inline_type_declarations_impl(field_decl->get_declared_type(),
                                              *this, semantic_context,
                                              scope_stack);
        analyze_type_operand_expressions(field_decl->get_declared_type(),
                                         expr_analyzer_, semantic_context,
                                         scope_stack);
        for (const auto &dimension : field_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            if (semantic_context.get_semantic_model().get_node_type(
                    dimension.get()) == nullptr) {
                expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                            scope_stack);
            }
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(
                    semantic_context,
                    "array dimension must be an integer constant expression",
                    dimension->get_source_span());
            }
        }
        const SemanticType *declared_type =
            type_resolver_.apply_array_dimensions(
                type_resolver_.resolve_type(field_decl->get_declared_type(),
                                            semantic_context, &scope_stack),
                field_decl->get_dimensions(), semantic_context);
        if (field_decl->get_bit_width() != nullptr) {
            if (semantic_context.get_semantic_model().get_node_type(
                    field_decl->get_bit_width()) == nullptr) {
                expr_analyzer_.analyze_expr(field_decl->get_bit_width(),
                                            semantic_context, scope_stack);
            }
            if (!constant_evaluator_.is_integer_constant_expr(
                    field_decl->get_bit_width(), semantic_context,
                    conversion_checker_)) {
                add_error(
                    semantic_context,
                    "bit-field width must be an integer constant expression",
                    field_decl->get_bit_width()->get_source_span());
            }
            const auto width_value =
                constant_evaluator_.get_integer_constant_value(
                    field_decl->get_bit_width(), semantic_context);
            if (!conversion_checker_.is_integer_like_type(declared_type)) {
                add_error(semantic_context,
                          "bit-field base type must be an integer type",
                          field_decl->get_source_span());
            } else if (!width_value.has_value() || *width_value < 0) {
                add_error(semantic_context,
                          "bit-field width must be non-negative",
                          field_decl->get_bit_width()->get_source_span());
            } else {
                detail::IntegerConversionService integer_conversion_service;
                const auto integer_info =
                    integer_conversion_service.get_integer_type_info(
                        declared_type);
                if (integer_info.has_value() &&
                    *width_value > integer_info->get_bit_width()) {
                    add_error(semantic_context,
                              "bit-field width exceeds base type width",
                              field_decl->get_bit_width()->get_source_span());
                }
            }
        }
        const auto *symbol =
            semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::Field, field_decl->get_name(), declared_type,
                field_decl));
        semantic_model.bind_symbol(field_decl, symbol);
        semantic_model.bind_node_type(field_decl, declared_type);
        return;
    }
    case AstKind::TypedefDecl: {
        const auto *typedef_decl = static_cast<const TypedefDecl *>(decl);
        analyze_inline_type_declarations_impl(typedef_decl->get_aliased_type(),
                                              *this, semantic_context,
                                              scope_stack);
        analyze_type_operand_expressions(typedef_decl->get_aliased_type(),
                                         expr_analyzer_, semantic_context,
                                         scope_stack);
        for (const auto &dimension : typedef_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(
                    semantic_context,
                    "array dimension must be an integer constant expression",
                    dimension->get_source_span());
            }
        }
        const TypeNode *aliased_type_node = typedef_decl->get_aliased_type();
        const SemanticType *aliased_type = nullptr;
        if (aliased_type_node != nullptr &&
            aliased_type_node->get_kind() == AstKind::StructType) {
            const auto *struct_type_node =
                static_cast<const StructTypeNode *>(aliased_type_node);
            if (!struct_type_node->get_fields().empty()) {
                analyze_aggregate_field_constant_expressions(
                    struct_type_node->get_fields(), expr_analyzer_,
                    semantic_context, scope_stack);
            }
            if (is_anonymous_tag_name(struct_type_node->get_name()) &&
                !struct_type_node->get_fields().empty()) {
                aliased_type = semantic_model.own_type(
                    std::make_unique<StructSemanticType>(
                        get_semantic_tag_name(
                            struct_type_node->get_name(),
                            struct_type_node->get_source_span()),
                        build_aggregate_semantic_fields(
                            struct_type_node->get_fields(), type_resolver_,
                            constant_evaluator_, conversion_checker_,
                            semantic_context, scope_stack)));
            }
        } else if (aliased_type_node != nullptr &&
                   aliased_type_node->get_kind() == AstKind::UnionType) {
            const auto *union_type_node =
                static_cast<const UnionTypeNode *>(aliased_type_node);
            if (!union_type_node->get_fields().empty()) {
                analyze_aggregate_field_constant_expressions(
                    union_type_node->get_fields(), expr_analyzer_,
                    semantic_context, scope_stack);
            }
            if (is_anonymous_tag_name(union_type_node->get_name()) &&
                !union_type_node->get_fields().empty()) {
                aliased_type =
                    semantic_model.own_type(std::make_unique<UnionSemanticType>(
                        get_semantic_tag_name(
                            union_type_node->get_name(),
                            union_type_node->get_source_span()),
                        build_aggregate_semantic_fields(
                            union_type_node->get_fields(), type_resolver_,
                            constant_evaluator_, conversion_checker_,
                            semantic_context, scope_stack)));
            }
        }
        if (aliased_type == nullptr) {
            aliased_type = type_resolver_.apply_array_dimensions(
                type_resolver_.resolve_type(aliased_type_node, semantic_context,
                                            &scope_stack),
                typedef_decl->get_dimensions(), semantic_context);
        } else {
            aliased_type = type_resolver_.apply_array_dimensions(
                aliased_type, typedef_decl->get_dimensions(), semantic_context);
        }
        if (const SemanticSymbol *existing_symbol =
                scope_stack.lookup_local(typedef_decl->get_name());
            existing_symbol != nullptr &&
            existing_symbol->get_kind() == SymbolKind::TypedefName &&
            conversion_checker_.is_same_type(existing_symbol->get_type(),
                                             aliased_type) &&
            (existing_symbol->get_decl_node() == nullptr ||
             semantic_context.is_system_header_span(
                 typedef_decl->get_source_span()) ||
             is_system_header_symbol(existing_symbol, semantic_context))) {
            semantic_model.bind_symbol(typedef_decl, existing_symbol);
            semantic_model.bind_node_type(typedef_decl, aliased_type);
            return;
        }
        const auto *symbol =
            semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::TypedefName, typedef_decl->get_name(), aliased_type,
                typedef_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          typedef_decl->get_source_span())) {
            semantic_model.bind_symbol(typedef_decl, symbol);
            semantic_model.bind_node_type(typedef_decl, aliased_type);
        }
        return;
    }
    case AstKind::StructDecl: {
        const auto *struct_decl = static_cast<const StructDecl *>(decl);
        if (!is_anonymous_tag_name(struct_decl->get_name()) &&
            struct_decl->get_fields().empty()) {
            if (const SemanticSymbol *tag_symbol =
                    scope_stack.lookup_tag_local(struct_decl->get_name());
                tag_symbol != nullptr &&
                tag_symbol->get_kind() == SymbolKind::StructName) {
                semantic_model.bind_symbol(struct_decl, tag_symbol);
                semantic_model.bind_node_type(struct_decl,
                                              tag_symbol->get_type());
                return;
            }
        }
        analyze_aggregate_field_constant_expressions(
            struct_decl->get_fields(), expr_analyzer_, semantic_context,
            scope_stack);
        const auto *struct_type =
            semantic_model.own_type(std::make_unique<StructSemanticType>(
                get_semantic_tag_name(struct_decl->get_name(),
                                      struct_decl->get_source_span()),
                build_struct_semantic_fields(
                    struct_decl, type_resolver_, constant_evaluator_,
                    conversion_checker_, semantic_context, scope_stack)));
        semantic_model.bind_node_type(struct_decl, struct_type);
        if (!is_anonymous_tag_name(struct_decl->get_name())) {
            bool completed_existing_tag = false;
            if (const SemanticSymbol *tag_symbol =
                    scope_stack.lookup_tag_local(struct_decl->get_name());
                tag_symbol != nullptr &&
                tag_symbol->get_kind() == SymbolKind::StructName &&
                is_incomplete_named_struct_semantic_type(
                    tag_symbol->get_type(), struct_decl->get_name())) {
                const_cast<SemanticSymbol *>(tag_symbol)->set_type(struct_type);
                semantic_model.bind_symbol(struct_decl, tag_symbol);
                completed_existing_tag = true;
            }
            if (const SemanticSymbol *typedef_symbol =
                    scope_stack.lookup_local(struct_decl->get_name());
                typedef_symbol != nullptr &&
                typedef_symbol->get_kind() == SymbolKind::TypedefName &&
                is_incomplete_named_struct_semantic_type(
                    typedef_symbol->get_type(), struct_decl->get_name())) {
                const_cast<SemanticSymbol *>(typedef_symbol)
                    ->set_type(struct_type);
            }
            if (!completed_existing_tag) {
                const auto *symbol =
                    semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                        SymbolKind::StructName, struct_decl->get_name(),
                        struct_type, struct_decl));
                if (define_symbol(semantic_context, scope_stack, symbol,
                                  struct_decl->get_source_span())) {
                    semantic_model.bind_symbol(struct_decl, symbol);
                }
            }
        }
        for (const auto &field : struct_decl->get_fields()) {
            analyze_decl(field.get(), semantic_context, scope_stack);
        }
        return;
    }
    case AstKind::UnionDecl: {
        const auto *union_decl = static_cast<const UnionDecl *>(decl);
        if (!is_anonymous_tag_name(union_decl->get_name()) &&
            union_decl->get_fields().empty()) {
            if (const SemanticSymbol *tag_symbol =
                    scope_stack.lookup_tag_local(union_decl->get_name());
                tag_symbol != nullptr &&
                tag_symbol->get_kind() == SymbolKind::UnionName) {
                semantic_model.bind_symbol(union_decl, tag_symbol);
                semantic_model.bind_node_type(union_decl,
                                              tag_symbol->get_type());
                return;
            }
        }
        analyze_aggregate_field_constant_expressions(
            union_decl->get_fields(), expr_analyzer_, semantic_context,
            scope_stack);
        const auto *union_type =
            semantic_model.own_type(std::make_unique<UnionSemanticType>(
                get_semantic_tag_name(union_decl->get_name(),
                                      union_decl->get_source_span()),
                build_union_semantic_fields(
                    union_decl, type_resolver_, constant_evaluator_,
                    conversion_checker_, semantic_context, scope_stack)));
        semantic_model.bind_node_type(union_decl, union_type);
        if (!is_anonymous_tag_name(union_decl->get_name())) {
            bool completed_existing_tag = false;
            if (const SemanticSymbol *tag_symbol =
                    scope_stack.lookup_tag_local(union_decl->get_name());
                tag_symbol != nullptr &&
                tag_symbol->get_kind() == SymbolKind::UnionName &&
                is_incomplete_named_union_semantic_type(
                    tag_symbol->get_type(), union_decl->get_name())) {
                const_cast<SemanticSymbol *>(tag_symbol)->set_type(union_type);
                semantic_model.bind_symbol(union_decl, tag_symbol);
                completed_existing_tag = true;
            }
            if (const SemanticSymbol *typedef_symbol =
                    scope_stack.lookup_local(union_decl->get_name());
                typedef_symbol != nullptr &&
                typedef_symbol->get_kind() == SymbolKind::TypedefName &&
                is_incomplete_named_union_semantic_type(
                    typedef_symbol->get_type(), union_decl->get_name())) {
                const_cast<SemanticSymbol *>(typedef_symbol)
                    ->set_type(union_type);
            }
            if (!completed_existing_tag) {
                const auto *symbol =
                    semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                        SymbolKind::UnionName, union_decl->get_name(),
                        union_type, union_decl));
                if (define_symbol(semantic_context, scope_stack, symbol,
                                  union_decl->get_source_span())) {
                    semantic_model.bind_symbol(union_decl, symbol);
                }
            }
        }
        for (const auto &field : union_decl->get_fields()) {
            analyze_decl(field.get(), semantic_context, scope_stack);
        }
        return;
    }
    case AstKind::EnumDecl: {
        const auto *enum_decl = static_cast<const EnumDecl *>(decl);
        const auto *enum_type = semantic_model.own_type(
            std::make_unique<EnumSemanticType>(get_semantic_tag_name(
                enum_decl->get_name(), enum_decl->get_source_span())));
        semantic_model.bind_node_type(enum_decl, enum_type);
        if (!is_anonymous_tag_name(enum_decl->get_name())) {
            const auto *symbol =
                semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                    SymbolKind::EnumName, enum_decl->get_name(), enum_type,
                    enum_decl));
            if (define_symbol(semantic_context, scope_stack, symbol,
                              enum_decl->get_source_span())) {
                semantic_model.bind_symbol(enum_decl, symbol);
            }
        }
        analyze_enum_enumerators(enum_decl->get_enumerators(), semantic_context,
                                 scope_stack);
        return;
    }
    case AstKind::EnumeratorDecl: {
        const auto *enumerator_decl = static_cast<const EnumeratorDecl *>(decl);
        const auto *int_type = semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>("int"));
        const auto *symbol =
            semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
                SymbolKind::Enumerator, enumerator_decl->get_name(), int_type,
                enumerator_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          enumerator_decl->get_source_span())) {
            semantic_model.bind_symbol(enumerator_decl, symbol);
            semantic_model.bind_node_type(enumerator_decl, int_type);
        }
        expr_analyzer_.analyze_expr(enumerator_decl->get_value(),
                                    semantic_context, scope_stack);
        if (enumerator_decl->get_value() != nullptr &&
            !constant_evaluator_.is_integer_constant_expr(
                enumerator_decl->get_value(), semantic_context,
                conversion_checker_)) {
            add_error(semantic_context,
                      "enumerator value must be an integer constant expression",
                      enumerator_decl->get_value()->get_source_span());
        }
        const auto integer_constant_value =
            constant_evaluator_.get_integer_constant_value(
                enumerator_decl->get_value(), semantic_context);
        if (integer_constant_value.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                enumerator_decl, *integer_constant_value, semantic_context);
        }
        return;
    }
    default:
        return;
    }
}

} // namespace sysycc::detail
