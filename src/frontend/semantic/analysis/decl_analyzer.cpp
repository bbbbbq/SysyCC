#include "frontend/semantic/analysis/decl_analyzer.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/diagnostic/warning_options.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/analysis/expr_analyzer.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

std::vector<SemanticFieldInfo>
build_struct_semantic_fields(const StructDecl *struct_decl,
                             const TypeResolver &type_resolver,
                             const ConstantEvaluator &constant_evaluator,
                             const ConversionChecker &conversion_checker,
                             SemanticContext &semantic_context,
                             const ScopeStack &scope_stack) {
    std::vector<SemanticFieldInfo> fields;
    if (struct_decl == nullptr) {
        return fields;
    }
    fields.reserve(struct_decl->get_fields().size());
    for (const auto &field : struct_decl->get_fields()) {
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
        const SemanticType *field_type = type_resolver.apply_array_dimensions(
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context, &scope_stack),
            field_decl->get_dimensions(), semantic_context);
        fields.emplace_back(
            field_decl->get_name(),
            field_type,
            bit_width);
    }
    return fields;
}

std::vector<SemanticFieldInfo>
build_union_semantic_fields(const UnionDecl *union_decl,
                            const TypeResolver &type_resolver,
                            const ConstantEvaluator &constant_evaluator,
                            const ConversionChecker &conversion_checker,
                            SemanticContext &semantic_context,
                            const ScopeStack &scope_stack) {
    std::vector<SemanticFieldInfo> fields;
    if (union_decl == nullptr) {
        return fields;
    }
    fields.reserve(union_decl->get_fields().size());
    for (const auto &field : union_decl->get_fields()) {
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
        const SemanticType *field_type = type_resolver.apply_array_dimensions(
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context, &scope_stack),
            field_decl->get_dimensions(), semantic_context);
        fields.emplace_back(
            field_decl->get_name(),
            field_type,
            bit_width);
    }
    return fields;
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
    return name.empty() || name == "<anonymous>";
}

} // namespace

DeclAnalyzer::DeclAnalyzer(const TypeResolver &type_resolver,
                           const ConversionChecker &conversion_checker,
                           const ConstantEvaluator &constant_evaluator,
                           const ExprAnalyzer &expr_analyzer)
    : type_resolver_(type_resolver),
      conversion_checker_(conversion_checker),
      constant_evaluator_(constant_evaluator),
      expr_analyzer_(expr_analyzer) {}

void DeclAnalyzer::add_error(SemanticContext &semantic_context,
                             std::string message,
                             const SourceSpan &source_span) const {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
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
        for (const auto &dimension : param_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(semantic_context,
                          "array dimension must be an integer constant expression",
                          dimension->get_source_span());
            }
        }
        const SemanticType *declared_type = type_resolver_.apply_array_dimensions(
            type_resolver_.resolve_type(param_decl->get_declared_type(),
                                        semantic_context, &scope_stack),
            param_decl->get_dimensions(), semantic_context);
        declared_type =
            type_resolver_.adjust_parameter_type(declared_type, semantic_context);
        semantic_model.bind_node_type(param_decl, declared_type);
        if (param_decl->get_name().empty()) {
            return;
        }
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::Parameter,
                                             param_decl->get_name(),
                                             declared_type, param_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          param_decl->get_source_span())) {
            semantic_model.bind_symbol(param_decl, symbol);
            semantic_context.record_function_local_symbol(symbol);
        }
        return;
    }
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        for (const auto &dimension : var_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(semantic_context,
                          "array dimension must be an integer constant expression",
                          dimension->get_source_span());
            }
        }
        const SemanticType *declared_type = type_resolver_.apply_array_dimensions(
            type_resolver_.resolve_type(var_decl->get_declared_type(),
                                        semantic_context, &scope_stack),
            var_decl->get_dimensions(), semantic_context);
        const bool is_file_scope = semantic_context.get_current_function() == nullptr;
        const bool has_initializer = var_decl->get_initializer() != nullptr;
        const bool is_tentative_definition =
            is_file_scope && !var_decl->get_is_extern() && !has_initializer;
        const bool is_initialized_definition = is_file_scope && has_initializer;
        const bool has_internal_linkage =
            is_file_scope && var_decl->get_is_static();
        const bool has_external_linkage =
            is_file_scope && !var_decl->get_is_static();
        const bool is_global_storage = is_file_scope || var_decl->get_is_extern();

        const SemanticSymbol *symbol = nullptr;
        if (is_global_storage) {
            const SemanticSymbol *existing_symbol =
                is_file_scope ? scope_stack.lookup_local(var_decl->get_name())
                              : scope_stack.lookup(var_decl->get_name());
            if (existing_symbol != nullptr &&
                existing_symbol->get_kind() == SymbolKind::Variable) {
                if (!conversion_checker_.is_same_type(existing_symbol->get_type(),
                                                     declared_type)) {
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
                    true,
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
                semantic_model.bind_variable_info(existing_symbol, updated_info);
                symbol = existing_symbol;
            }
        }

        if (symbol == nullptr) {
            symbol = semantic_model.own_symbol(
                std::make_unique<SemanticSymbol>(SymbolKind::Variable,
                                                 var_decl->get_name(),
                                                 declared_type, var_decl));
            if (define_symbol(semantic_context, scope_stack, symbol,
                              var_decl->get_source_span())) {
                semantic_model.bind_variable_info(
                    symbol, VariableSemanticInfo(
                                is_global_storage, has_external_linkage,
                                has_internal_linkage,
                                is_tentative_definition,
                                is_initialized_definition));
            } else {
                break;
            }
        }

        semantic_model.bind_symbol(var_decl, symbol);
        semantic_model.bind_node_type(var_decl, declared_type);
        if (!is_global_storage) {
            semantic_context.record_function_local_symbol(symbol);
        }
        expr_analyzer_.analyze_expr(var_decl->get_initializer(), semantic_context,
                                    scope_stack);
        if (is_file_scope && var_decl->get_initializer() != nullptr &&
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
                    declared_type, initializer_type, var_decl->get_initializer(),
                    semantic_context, constant_evaluator_)) {
                add_error(semantic_context,
                          "initializer type does not match declared type",
                          var_decl->get_initializer()->get_source_span());
            } else if (initializer_type != nullptr &&
                       var_decl->get_initializer()->get_kind() !=
                           AstKind::CastExpr &&
                       conversion_checker_.should_warn_implicit_integer_narrowing(
                           declared_type, initializer_type,
                           constant_evaluator_.get_integer_constant_value(
                               var_decl->get_initializer(), semantic_context))) {
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
        for (const auto &dimension : const_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(semantic_context,
                          "array dimension must be an integer constant expression",
                          dimension->get_source_span());
            }
        }
        const SemanticType *declared_type = type_resolver_.apply_array_dimensions(
            type_resolver_.resolve_type(const_decl->get_declared_type(),
                                        semantic_context, &scope_stack),
            const_decl->get_dimensions(), semantic_context);
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::Constant,
                                             const_decl->get_name(),
                                             declared_type, const_decl));
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
            } else {
                add_error(semantic_context,
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
        for (const auto &dimension : field_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(semantic_context,
                          "array dimension must be an integer constant expression",
                          dimension->get_source_span());
            }
        }
        const SemanticType *declared_type = type_resolver_.apply_array_dimensions(
            type_resolver_.resolve_type(field_decl->get_declared_type(),
                                        semantic_context, &scope_stack),
            field_decl->get_dimensions(), semantic_context);
        if (field_decl->get_bit_width() != nullptr) {
            expr_analyzer_.analyze_expr(field_decl->get_bit_width(),
                                        semantic_context, scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    field_decl->get_bit_width(), semantic_context,
                    conversion_checker_)) {
                add_error(semantic_context,
                          "bit-field width must be an integer constant expression",
                          field_decl->get_bit_width()->get_source_span());
            }
            const auto width_value = constant_evaluator_.get_integer_constant_value(
                field_decl->get_bit_width(), semantic_context);
            if (!conversion_checker_.is_integer_like_type(declared_type)) {
                add_error(semantic_context,
                          "bit-field base type must be an integer type",
                          field_decl->get_source_span());
            } else if (!width_value.has_value() || *width_value < 0) {
                add_error(semantic_context, "bit-field width must be non-negative",
                          field_decl->get_bit_width()->get_source_span());
            } else {
                detail::IntegerConversionService integer_conversion_service;
                const auto integer_info =
                    integer_conversion_service.get_integer_type_info(declared_type);
                if (integer_info.has_value() &&
                    *width_value > integer_info->get_bit_width()) {
                    add_error(semantic_context,
                              "bit-field width exceeds base type width",
                              field_decl->get_bit_width()->get_source_span());
                }
            }
        }
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::Field,
                                             field_decl->get_name(),
                                             declared_type, field_decl));
        semantic_model.bind_symbol(field_decl, symbol);
        semantic_model.bind_node_type(field_decl, declared_type);
        return;
    }
    case AstKind::TypedefDecl: {
        const auto *typedef_decl = static_cast<const TypedefDecl *>(decl);
        for (const auto &dimension : typedef_decl->get_dimensions()) {
            if (dimension == nullptr) {
                continue;
            }
            expr_analyzer_.analyze_expr(dimension.get(), semantic_context,
                                        scope_stack);
            if (!constant_evaluator_.is_integer_constant_expr(
                    dimension.get(), semantic_context, conversion_checker_)) {
                add_error(semantic_context,
                          "array dimension must be an integer constant expression",
                          dimension->get_source_span());
            }
        }
        const SemanticType *aliased_type = type_resolver_.apply_array_dimensions(
            type_resolver_.resolve_type(typedef_decl->get_aliased_type(),
                                        semantic_context, &scope_stack),
            typedef_decl->get_dimensions(), semantic_context);
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
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::TypedefName,
                                             typedef_decl->get_name(),
                                             aliased_type, typedef_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          typedef_decl->get_source_span())) {
            semantic_model.bind_symbol(typedef_decl, symbol);
            semantic_model.bind_node_type(typedef_decl, aliased_type);
        }
        return;
    }
    case AstKind::StructDecl: {
        const auto *struct_decl = static_cast<const StructDecl *>(decl);
        const auto *struct_type = semantic_model.own_type(
            std::make_unique<StructSemanticType>(
                struct_decl->get_name(),
                build_struct_semantic_fields(struct_decl, type_resolver_,
                                             constant_evaluator_,
                                             conversion_checker_,
                                             semantic_context, scope_stack)));
        semantic_model.bind_node_type(struct_decl, struct_type);
        if (!is_anonymous_tag_name(struct_decl->get_name())) {
            const auto *symbol = semantic_model.own_symbol(
                std::make_unique<SemanticSymbol>(SymbolKind::StructName,
                                                 struct_decl->get_name(),
                                                 struct_type, struct_decl));
            if (define_symbol(semantic_context, scope_stack, symbol,
                              struct_decl->get_source_span())) {
                semantic_model.bind_symbol(struct_decl, symbol);
            }
        }
        for (const auto &field : struct_decl->get_fields()) {
            analyze_decl(field.get(), semantic_context, scope_stack);
        }
        return;
    }
    case AstKind::UnionDecl: {
        const auto *union_decl = static_cast<const UnionDecl *>(decl);
        const auto *union_type = semantic_model.own_type(
            std::make_unique<UnionSemanticType>(
                union_decl->get_name(),
                build_union_semantic_fields(union_decl, type_resolver_,
                                            constant_evaluator_,
                                            conversion_checker_,
                                            semantic_context, scope_stack)));
        semantic_model.bind_node_type(union_decl, union_type);
        if (!is_anonymous_tag_name(union_decl->get_name())) {
            const auto *symbol = semantic_model.own_symbol(
                std::make_unique<SemanticSymbol>(SymbolKind::UnionName,
                                                 union_decl->get_name(),
                                                 union_type, union_decl));
            if (define_symbol(semantic_context, scope_stack, symbol,
                              union_decl->get_source_span())) {
                semantic_model.bind_symbol(union_decl, symbol);
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
            std::make_unique<EnumSemanticType>(enum_decl->get_name()));
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::EnumName,
                                             enum_decl->get_name(), enum_type,
                                             enum_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          enum_decl->get_source_span())) {
            semantic_model.bind_symbol(enum_decl, symbol);
            semantic_model.bind_node_type(enum_decl, enum_type);
        }
        for (const auto &enumerator : enum_decl->get_enumerators()) {
            analyze_decl(enumerator.get(), semantic_context, scope_stack);
        }
        return;
    }
    case AstKind::EnumeratorDecl: {
        const auto *enumerator_decl = static_cast<const EnumeratorDecl *>(decl);
        const auto *int_type = semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>("int"));
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::Enumerator,
                                             enumerator_decl->get_name(), int_type,
                                             enumerator_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          enumerator_decl->get_source_span())) {
            semantic_model.bind_symbol(enumerator_decl, symbol);
            semantic_model.bind_node_type(enumerator_decl, int_type);
        }
        expr_analyzer_.analyze_expr(enumerator_decl->get_value(), semantic_context,
                                    scope_stack);
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
