#include "frontend/semantic/analysis/decl_analyzer.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
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
                             SemanticContext &semantic_context) {
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
        fields.emplace_back(
            field_decl->get_name(),
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context));
    }
    return fields;
}

std::vector<SemanticFieldInfo>
build_union_semantic_fields(const UnionDecl *union_decl,
                            const TypeResolver &type_resolver,
                            SemanticContext &semantic_context) {
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
        fields.emplace_back(
            field_decl->get_name(),
            type_resolver.resolve_type(field_decl->get_declared_type(),
                                       semantic_context));
    }
    return fields;
}

bool is_global_variable_definition(const VarDecl *var_decl) {
    if (var_decl == nullptr) {
        return false;
    }
    return !var_decl->get_is_extern() || var_decl->get_initializer() != nullptr;
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
        }
        return;
    }
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        for (const auto &dimension : var_decl->get_dimensions()) {
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
                        var_decl->get_is_extern(),
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
                                is_global_storage, var_decl->get_is_extern(),
                                is_tentative_definition,
                                is_initialized_definition));
            } else {
                break;
            }
        }

        semantic_model.bind_symbol(var_decl, symbol);
        semantic_model.bind_node_type(var_decl, declared_type);
        expr_analyzer_.analyze_expr(var_decl->get_initializer(), semantic_context,
                                    scope_stack);
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
            }
        }
        return;
    }
    case AstKind::ConstDecl: {
        const auto *const_decl = static_cast<const ConstDecl *>(decl);
        for (const auto &dimension : const_decl->get_dimensions()) {
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
        if (const_decl->get_initializer() != nullptr &&
            !constant_evaluator_.is_integer_constant_expr(
                const_decl->get_initializer(), semantic_context,
                conversion_checker_) &&
            conversion_checker_.is_integer_like_type(declared_type)) {
            add_error(semantic_context,
                      "const initializer must be an integer constant expression",
                      const_decl->get_initializer()->get_source_span());
        }
        const auto integer_constant_value =
            constant_evaluator_.get_integer_constant_value(
                const_decl->get_initializer(), semantic_context);
        if (integer_constant_value.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                const_decl, *integer_constant_value, semantic_context);
        }
        return;
    }
    case AstKind::FieldDecl: {
        const auto *field_decl = static_cast<const FieldDecl *>(decl);
        for (const auto &dimension : field_decl->get_dimensions()) {
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
                                             semantic_context)));
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::StructName,
                                             struct_decl->get_name(),
                                             struct_type, struct_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          struct_decl->get_source_span())) {
            semantic_model.bind_symbol(struct_decl, symbol);
            semantic_model.bind_node_type(struct_decl, struct_type);
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
                                            semantic_context)));
        const auto *symbol = semantic_model.own_symbol(
            std::make_unique<SemanticSymbol>(SymbolKind::UnionName,
                                             union_decl->get_name(),
                                             union_type, union_decl));
        if (define_symbol(semantic_context, scope_stack, symbol,
                          union_decl->get_source_span())) {
            semantic_model.bind_symbol(union_decl, symbol);
            semantic_model.bind_node_type(union_decl, union_type);
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
