#include "frontend/ast/detail/ast_feature_validator.hpp"

namespace sysycc::detail {

namespace {

bool validate_node(const AstNode *node, const AstFeatureRegistry &feature_registry,
                   AstFeatureErrorInfo &error_info) {
    if (node == nullptr) {
        return true;
    }

    switch (node->get_kind()) {
    case AstKind::TranslationUnit: {
        const auto *translation_unit = static_cast<const TranslationUnit *>(node);
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (!validate_node(decl.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::FunctionDecl: {
        const auto *function_decl = static_cast<const FunctionDecl *>(node);
        if (!function_decl->get_attributes().empty() &&
            !feature_registry.has_feature(AstFeature::GnuAttributeLists)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: GNU attribute lists",
                function_decl->get_source_span());
            return false;
        }
        if (!function_decl->get_asm_label().empty() &&
            !feature_registry.has_feature(AstFeature::GnuAsmLabels)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: GNU asm labels",
                function_decl->get_source_span());
            return false;
        }
        if (!validate_node(function_decl->get_return_type(), feature_registry,
                           error_info)) {
            return false;
        }
        for (const auto &parameter : function_decl->get_parameters()) {
            if (!validate_node(parameter.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return validate_node(function_decl->get_body(), feature_registry,
                             error_info);
    }
    case AstKind::ParamDecl: {
        const auto *param_decl = static_cast<const ParamDecl *>(node);
        return validate_node(param_decl->get_declared_type(), feature_registry,
                             error_info);
    }
    case AstKind::FieldDecl: {
        const auto *field_decl = static_cast<const FieldDecl *>(node);
        if (!validate_node(field_decl->get_declared_type(), feature_registry,
                           error_info)) {
            return false;
        }
        if (field_decl->get_bit_width() != nullptr &&
            !feature_registry.has_feature(AstFeature::BitFieldWidths)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST node without enabled AST feature: bit-field widths",
                field_decl->get_source_span());
            return false;
        }
        return validate_node(field_decl->get_bit_width(), feature_registry,
                             error_info);
    }
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(node);
        if (!validate_node(var_decl->get_declared_type(), feature_registry,
                           error_info)) {
            return false;
        }
        return validate_node(var_decl->get_initializer(), feature_registry,
                             error_info);
    }
    case AstKind::ConstDecl: {
        const auto *const_decl = static_cast<const ConstDecl *>(node);
        if (!validate_node(const_decl->get_declared_type(), feature_registry,
                           error_info)) {
            return false;
        }
        return validate_node(const_decl->get_initializer(), feature_registry,
                             error_info);
    }
    case AstKind::StructDecl: {
        const auto *struct_decl = static_cast<const StructDecl *>(node);
        for (const auto &field : struct_decl->get_fields()) {
            if (!validate_node(field.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::UnionDecl: {
        if (!feature_registry.has_feature(AstFeature::UnionTypeNodes)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: union type nodes",
                node->get_source_span());
            return false;
        }
        const auto *union_decl = static_cast<const UnionDecl *>(node);
        for (const auto &field : union_decl->get_fields()) {
            if (!validate_node(field.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::EnumDecl: {
        const auto *enum_decl = static_cast<const EnumDecl *>(node);
        for (const auto &enumerator : enum_decl->get_enumerators()) {
            if (!validate_node(enumerator.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::EnumeratorDecl: {
        const auto *enumerator_decl = static_cast<const EnumeratorDecl *>(node);
        return validate_node(enumerator_decl->get_value(), feature_registry,
                             error_info);
    }
    case AstKind::TypedefDecl: {
        const auto *typedef_decl = static_cast<const TypedefDecl *>(node);
        return validate_node(typedef_decl->get_aliased_type(), feature_registry,
                             error_info);
    }
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(node);
        for (const auto &statement : block_stmt->get_statements()) {
            if (!validate_node(statement.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(node);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (!validate_node(decl.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(node);
        return validate_node(expr_stmt->get_expression(), feature_registry,
                             error_info);
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(node);
        return validate_node(if_stmt->get_condition(), feature_registry,
                             error_info) &&
               validate_node(if_stmt->get_then_branch(), feature_registry,
                             error_info) &&
               validate_node(if_stmt->get_else_branch(), feature_registry,
                             error_info);
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(node);
        return validate_node(while_stmt->get_condition(), feature_registry,
                             error_info) &&
               validate_node(while_stmt->get_body(), feature_registry,
                             error_info);
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(node);
        return validate_node(do_while_stmt->get_body(), feature_registry,
                             error_info) &&
               validate_node(do_while_stmt->get_condition(), feature_registry,
                             error_info);
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(node);
        return validate_node(for_stmt->get_init(), feature_registry, error_info) &&
               validate_node(for_stmt->get_condition(), feature_registry,
                             error_info) &&
               validate_node(for_stmt->get_step(), feature_registry, error_info) &&
               validate_node(for_stmt->get_body(), feature_registry, error_info);
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(node);
        return validate_node(switch_stmt->get_condition(), feature_registry,
                             error_info) &&
               validate_node(switch_stmt->get_body(), feature_registry,
                             error_info);
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(node);
        return validate_node(case_stmt->get_value(), feature_registry, error_info) &&
               validate_node(case_stmt->get_body(), feature_registry, error_info);
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(node);
        return validate_node(default_stmt->get_body(), feature_registry,
                             error_info);
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(node);
        return validate_node(label_stmt->get_body(), feature_registry, error_info);
    }
    case AstKind::GotoStmt:
        return true;
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(node);
        return validate_node(return_stmt->get_value(), feature_registry,
                             error_info);
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(node);
        return validate_node(assign_expr->get_target(), feature_registry,
                             error_info) &&
               validate_node(assign_expr->get_value(), feature_registry,
                             error_info);
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(node);
        return validate_node(unary_expr->get_operand(), feature_registry,
                             error_info);
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(node);
        return validate_node(prefix_expr->get_operand(), feature_registry,
                             error_info);
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(node);
        return validate_node(postfix_expr->get_operand(), feature_registry,
                             error_info);
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(node);
        return validate_node(binary_expr->get_lhs(), feature_registry, error_info) &&
               validate_node(binary_expr->get_rhs(), feature_registry, error_info);
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(node);
        return validate_node(conditional_expr->get_condition(), feature_registry,
                             error_info) &&
               validate_node(conditional_expr->get_true_expr(), feature_registry,
                             error_info) &&
               validate_node(conditional_expr->get_false_expr(), feature_registry,
                             error_info);
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(node);
        if (!validate_node(call_expr->get_callee(), feature_registry, error_info)) {
            return false;
        }
        for (const auto &argument : call_expr->get_arguments()) {
            if (!validate_node(argument.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(node);
        return validate_node(index_expr->get_base(), feature_registry, error_info) &&
               validate_node(index_expr->get_index(), feature_registry, error_info);
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(node);
        return validate_node(member_expr->get_base(), feature_registry, error_info);
    }
    case AstKind::InitListExpr: {
        const auto *init_list_expr = static_cast<const InitListExpr *>(node);
        for (const auto &element : init_list_expr->get_elements()) {
            if (!validate_node(element.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(node);
        return validate_node(pointer_type->get_pointee_type(), feature_registry,
                             error_info);
    }
    case AstKind::FunctionType: {
        const auto *function_type = static_cast<const FunctionTypeNode *>(node);
        if (!validate_node(function_type->get_return_type(), feature_registry,
                           error_info)) {
            return false;
        }
        for (const auto &parameter_type : function_type->get_parameter_types()) {
            if (!validate_node(parameter_type.get(), feature_registry,
                               error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::QualifiedType:
        if (!feature_registry.has_feature(AstFeature::QualifiedTypeNodes)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: qualified type nodes",
                node->get_source_span());
            return false;
        }
        return validate_node(
            static_cast<const QualifiedTypeNode *>(node)->get_base_type(),
            feature_registry, error_info);
    case AstKind::UnionType: {
        if (!feature_registry.has_feature(AstFeature::UnionTypeNodes)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: union type nodes",
                node->get_source_span());
            return false;
        }
        const auto *union_type = static_cast<const UnionTypeNode *>(node);
        for (const auto &field : union_type->get_fields()) {
            if (!validate_node(field.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::BuiltinType: {
        const auto *builtin_type = static_cast<const BuiltinTypeNode *>(node);
        if (builtin_type->get_name() == "_Float16" &&
            !feature_registry.has_feature(AstFeature::ExtendedBuiltinTypes)) {
            error_info = AstFeatureErrorInfo(
                "unsupported AST lowering without enabled AST feature: extended builtin types",
                node->get_source_span());
            return false;
        }
        return true;
    }
    case AstKind::NamedType:
        return true;
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(node);
        for (const auto &field : struct_type->get_fields()) {
            if (!validate_node(field.get(), feature_registry, error_info)) {
                return false;
            }
        }
        return true;
    }
    case AstKind::UnknownDecl:
    case AstKind::UnknownStmt:
    case AstKind::UnknownExpr:
    case AstKind::UnknownType:
    case AstKind::EnumType:
    case AstKind::CastExpr:
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
    case AstKind::IntegerLiteralExpr:
    case AstKind::FloatLiteralExpr:
    case AstKind::CharLiteralExpr:
    case AstKind::StringLiteralExpr:
    case AstKind::IdentifierExpr:
        return true;
    }

    return true;
}

} // namespace

bool AstFeatureValidator::validate(const AstNode *ast_root,
                                   const AstFeatureRegistry &feature_registry,
                                   AstFeatureErrorInfo &error_info) const {
    error_info = AstFeatureErrorInfo();
    return validate_node(ast_root, feature_registry, error_info);
}

} // namespace sysycc::detail
