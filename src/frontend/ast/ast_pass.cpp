#include "frontend/ast/ast_pass.hpp"

#include <filesystem>
#include <fstream>

#include "common/diagnostic/diagnostic_engine.hpp"
#include "common/intermediate_results_path.hpp"
#include "frontend/ast/ast_dump.hpp"
#include "frontend/ast/detail/ast_builder.hpp"
#include "frontend/ast/detail/ast_feature_validator.hpp"

namespace sysycc {

namespace {

bool ast_contains_unknown_nodes(const AstNode *node) {
    if (node == nullptr) {
        return false;
    }

    switch (node->get_kind()) {
    case AstKind::UnknownDecl:
    case AstKind::UnknownStmt:
    case AstKind::UnknownExpr:
    case AstKind::UnknownType:
        return true;
    case AstKind::TranslationUnit: {
        const auto *translation_unit = static_cast<const TranslationUnit *>(node);
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (ast_contains_unknown_nodes(decl.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::FunctionDecl: {
        const auto *function_decl = static_cast<const FunctionDecl *>(node);
        if (ast_contains_unknown_nodes(function_decl->get_return_type())) {
            return true;
        }
        for (const auto &parameter : function_decl->get_parameters()) {
            if (ast_contains_unknown_nodes(parameter.get())) {
                return true;
            }
        }
        return ast_contains_unknown_nodes(function_decl->get_body());
    }
    case AstKind::ParamDecl: {
        const auto *param_decl = static_cast<const ParamDecl *>(node);
        if (ast_contains_unknown_nodes(param_decl->get_declared_type())) {
            return true;
        }
        for (const auto &dimension : param_decl->get_dimensions()) {
            if (dimension != nullptr && ast_contains_unknown_nodes(dimension.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::FieldDecl: {
        const auto *field_decl = static_cast<const FieldDecl *>(node);
        if (ast_contains_unknown_nodes(field_decl->get_declared_type())) {
            return true;
        }
        for (const auto &dimension : field_decl->get_dimensions()) {
            if (dimension != nullptr && ast_contains_unknown_nodes(dimension.get())) {
                return true;
            }
        }
        return ast_contains_unknown_nodes(field_decl->get_bit_width());
    }
    case AstKind::VarDecl: {
        const auto *var_decl = static_cast<const VarDecl *>(node);
        if (ast_contains_unknown_nodes(var_decl->get_declared_type())) {
            return true;
        }
        for (const auto &dimension : var_decl->get_dimensions()) {
            if (dimension != nullptr && ast_contains_unknown_nodes(dimension.get())) {
                return true;
            }
        }
        return ast_contains_unknown_nodes(var_decl->get_initializer());
    }
    case AstKind::ConstDecl: {
        const auto *const_decl = static_cast<const ConstDecl *>(node);
        if (ast_contains_unknown_nodes(const_decl->get_declared_type())) {
            return true;
        }
        for (const auto &dimension : const_decl->get_dimensions()) {
            if (dimension != nullptr && ast_contains_unknown_nodes(dimension.get())) {
                return true;
            }
        }
        return ast_contains_unknown_nodes(const_decl->get_initializer());
    }
    case AstKind::StructDecl: {
        const auto *struct_decl = static_cast<const StructDecl *>(node);
        for (const auto &field : struct_decl->get_fields()) {
            if (ast_contains_unknown_nodes(field.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::UnionDecl: {
        const auto *union_decl = static_cast<const UnionDecl *>(node);
        for (const auto &field : union_decl->get_fields()) {
            if (ast_contains_unknown_nodes(field.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::EnumeratorDecl: {
        const auto *enumerator_decl = static_cast<const EnumeratorDecl *>(node);
        return ast_contains_unknown_nodes(enumerator_decl->get_value());
    }
    case AstKind::EnumDecl: {
        const auto *enum_decl = static_cast<const EnumDecl *>(node);
        for (const auto &enumerator : enum_decl->get_enumerators()) {
            if (ast_contains_unknown_nodes(enumerator.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::TypedefDecl: {
        const auto *typedef_decl = static_cast<const TypedefDecl *>(node);
        if (ast_contains_unknown_nodes(typedef_decl->get_aliased_type())) {
            return true;
        }
        for (const auto &dimension : typedef_decl->get_dimensions()) {
            if (dimension != nullptr && ast_contains_unknown_nodes(dimension.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(node);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (ast_contains_unknown_nodes(decl.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(node);
        for (const auto &statement : block_stmt->get_statements()) {
            if (ast_contains_unknown_nodes(statement.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(node);
        return ast_contains_unknown_nodes(expr_stmt->get_expression());
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(node);
        return ast_contains_unknown_nodes(if_stmt->get_condition()) ||
               ast_contains_unknown_nodes(if_stmt->get_then_branch()) ||
               ast_contains_unknown_nodes(if_stmt->get_else_branch());
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(node);
        return ast_contains_unknown_nodes(while_stmt->get_condition()) ||
               ast_contains_unknown_nodes(while_stmt->get_body());
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(node);
        return ast_contains_unknown_nodes(do_while_stmt->get_body()) ||
               ast_contains_unknown_nodes(do_while_stmt->get_condition());
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(node);
        return ast_contains_unknown_nodes(for_stmt->get_init()) ||
               ast_contains_unknown_nodes(for_stmt->get_condition()) ||
               ast_contains_unknown_nodes(for_stmt->get_step()) ||
               ast_contains_unknown_nodes(for_stmt->get_body());
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(node);
        return ast_contains_unknown_nodes(switch_stmt->get_condition()) ||
               ast_contains_unknown_nodes(switch_stmt->get_body());
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(node);
        return ast_contains_unknown_nodes(case_stmt->get_value()) ||
               ast_contains_unknown_nodes(case_stmt->get_body());
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(node);
        return ast_contains_unknown_nodes(default_stmt->get_body());
    }
    case AstKind::LabelStmt: {
        const auto *label_stmt = static_cast<const LabelStmt *>(node);
        return ast_contains_unknown_nodes(label_stmt->get_body());
    }
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(node);
        return ast_contains_unknown_nodes(return_stmt->get_value());
    }
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(node);
        return ast_contains_unknown_nodes(pointer_type->get_pointee_type());
    }
    case AstKind::FunctionType: {
        const auto *function_type = static_cast<const FunctionTypeNode *>(node);
        if (ast_contains_unknown_nodes(function_type->get_return_type())) {
            return true;
        }
        for (const auto &parameter_type : function_type->get_parameter_types()) {
            if (ast_contains_unknown_nodes(parameter_type.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::QualifiedType: {
        const auto *qualified_type =
            static_cast<const QualifiedTypeNode *>(node);
        return ast_contains_unknown_nodes(qualified_type->get_base_type());
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(node);
        for (const auto &field : union_type->get_fields()) {
            if (ast_contains_unknown_nodes(field.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(node);
        return ast_contains_unknown_nodes(unary_expr->get_operand());
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(node);
        return ast_contains_unknown_nodes(prefix_expr->get_operand());
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(node);
        return ast_contains_unknown_nodes(postfix_expr->get_operand());
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(node);
        return ast_contains_unknown_nodes(binary_expr->get_lhs()) ||
               ast_contains_unknown_nodes(binary_expr->get_rhs());
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(node);
        return ast_contains_unknown_nodes(cast_expr->get_target_type()) ||
               ast_contains_unknown_nodes(cast_expr->get_operand());
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr =
            static_cast<const ConditionalExpr *>(node);
        return ast_contains_unknown_nodes(conditional_expr->get_condition()) ||
               ast_contains_unknown_nodes(conditional_expr->get_true_expr()) ||
               ast_contains_unknown_nodes(conditional_expr->get_false_expr());
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(node);
        return ast_contains_unknown_nodes(assign_expr->get_target()) ||
               ast_contains_unknown_nodes(assign_expr->get_value());
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(node);
        if (ast_contains_unknown_nodes(call_expr->get_callee())) {
            return true;
        }
        for (const auto &argument : call_expr->get_arguments()) {
            if (ast_contains_unknown_nodes(argument.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(node);
        return ast_contains_unknown_nodes(index_expr->get_base()) ||
               ast_contains_unknown_nodes(index_expr->get_index());
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(node);
        return ast_contains_unknown_nodes(member_expr->get_base());
    }
    case AstKind::InitListExpr: {
        const auto *init_list_expr = static_cast<const InitListExpr *>(node);
        for (const auto &element : init_list_expr->get_elements()) {
            if (ast_contains_unknown_nodes(element.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::BuiltinType:
    case AstKind::NamedType:
    case AstKind::EnumType:
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
    case AstKind::GotoStmt:
    case AstKind::IntegerLiteralExpr:
    case AstKind::FloatLiteralExpr:
    case AstKind::CharLiteralExpr:
    case AstKind::StringLiteralExpr:
    case AstKind::IdentifierExpr:
        return false;
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(node);
        for (const auto &field : struct_type->get_fields()) {
            if (ast_contains_unknown_nodes(field.get())) {
                return true;
            }
        }
        return false;
    }
    }

    return false;
}

} // namespace

PassKind AstPass::Kind() const { return PassKind::Ast; }

const char *AstPass::Name() const { return "AstPass"; }

PassResult AstPass::Run(CompilerContext &context) {
    context.clear_ast_root();

    if (context.get_parse_tree_root() == nullptr) {
        const std::string message = "failed to build ast: missing parse tree";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Ast,
                                                  message);
        return PassResult::Failure(message);
    }

    detail::AstBuilderContext builder_context(context.get_parse_tree_root());
    detail::AstBuilder builder;
    context.set_ast_root(builder.build(builder_context));

    detail::AstFeatureErrorInfo feature_error_info;
    detail::AstFeatureValidator feature_validator;
    if (!feature_validator.validate(
            context.get_ast_root(),
            context.get_dialect_manager().get_ast_feature_registry(),
            feature_error_info)) {
        context.set_ast_complete(false);
        context.get_diagnostic_engine().add_error(DiagnosticStage::Ast,
                                                  feature_error_info.get_message(),
                                                  feature_error_info.get_source_span());
        return PassResult::Failure(feature_error_info.get_message());
    }

    context.set_ast_complete(!ast_contains_unknown_nodes(context.get_ast_root()));

    if (context.get_dump_ast()) {
        const std::filesystem::path output_dir =
            sysycc::get_intermediate_results_dir();
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".ast.txt");
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            const std::string message = "failed to open ast dump file";
            context.get_diagnostic_engine().add_error(DiagnosticStage::Ast,
                                                      message);
            return PassResult::Failure(message);
        }

        AstDumper dumper;
        ofs << "input_file: " << context.get_input_file() << "\n";
        ofs << "ast:\n";
        dumper.dump_to_stream(context.get_ast_root(), ofs);
        context.set_ast_dump_file_path(output_file.string());
    } else {
        context.set_ast_dump_file_path("");
    }

    if (!context.get_ast_complete() && context.get_dump_ast()) {
        const std::string message =
            "failed to build ast: ast contains unknown nodes";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Ast,
                                                  message);
        return PassResult::Failure(message);
    }

    return PassResult::Success();
}

} // namespace sysycc
