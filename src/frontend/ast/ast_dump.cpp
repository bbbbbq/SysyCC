#include "frontend/ast/ast_dump.hpp"

#include <cstddef>
#include <ostream>
#include <string>

namespace sysycc {

namespace {

void write_indent(std::ostream &os, int indent) {
    os << std::string(indent, ' ');
}

} // namespace

void AstDumper::dump_to_stream(const AstNode *node, std::ostream &os) const {
    dump_node(node, os, 0);
}

void AstDumper::dump_source_span(const AstNode *node, std::ostream &os,
                                 int indent) const {
    if (node == nullptr || node->get_source_span().empty()) {
        return;
    }
    write_indent(os, indent);
    os << "SourceSpan ";
    if (node->get_source_span().get_file() != nullptr &&
        !node->get_source_span().get_file()->empty()) {
        os << node->get_source_span().get_file()->get_path() << ":";
    }
    os << node->get_source_span().get_line_begin() << ":"
       << node->get_source_span().get_col_begin() << "-"
       << node->get_source_span().get_line_end() << ":"
       << node->get_source_span().get_col_end() << "\n";
}

void AstDumper::dump_node(const AstNode *node, std::ostream &os,
                          int indent) const {
    if (node == nullptr) {
        write_indent(os, indent);
        os << "<null>\n";
        return;
    }

    switch (node->get_kind()) {
    case AstKind::TranslationUnit:
        dump_translation_unit(static_cast<const TranslationUnit *>(node), os,
                              indent);
        return;
    case AstKind::FunctionDecl:
        dump_function_decl(static_cast<const FunctionDecl *>(node), os, indent);
        return;
    case AstKind::StructDecl:
        dump_struct_decl(static_cast<const StructDecl *>(node), os, indent);
        return;
    case AstKind::UnionDecl:
        dump_union_decl(static_cast<const UnionDecl *>(node), os, indent);
        return;
    case AstKind::EnumDecl:
        dump_enum_decl(static_cast<const EnumDecl *>(node), os, indent);
        return;
    case AstKind::TypedefDecl:
        dump_typedef_decl(static_cast<const TypedefDecl *>(node), os, indent);
        return;
    case AstKind::ParamDecl:
        dump_param_decl(static_cast<const ParamDecl *>(node), os, indent);
        return;
    case AstKind::FieldDecl:
        dump_field_decl(static_cast<const FieldDecl *>(node), os, indent);
        return;
    case AstKind::EnumeratorDecl:
        dump_enumerator_decl(static_cast<const EnumeratorDecl *>(node), os, indent);
        return;
    case AstKind::VarDecl:
        dump_var_decl(static_cast<const VarDecl *>(node), os, indent);
        return;
    case AstKind::ConstDecl:
        dump_const_decl(static_cast<const ConstDecl *>(node), os, indent);
        return;
    case AstKind::BlockStmt:
        dump_block_stmt(static_cast<const BlockStmt *>(node), os, indent);
        return;
    case AstKind::DeclStmt:
        dump_decl_stmt(static_cast<const DeclStmt *>(node), os, indent);
        return;
    case AstKind::ExprStmt:
        dump_expr_stmt(static_cast<const ExprStmt *>(node), os, indent);
        return;
    case AstKind::IfStmt:
        dump_if_stmt(static_cast<const IfStmt *>(node), os, indent);
        return;
    case AstKind::WhileStmt:
        dump_while_stmt(static_cast<const WhileStmt *>(node), os, indent);
        return;
    case AstKind::DoWhileStmt:
        dump_do_while_stmt(static_cast<const DoWhileStmt *>(node), os, indent);
        return;
    case AstKind::ForStmt:
        dump_for_stmt(static_cast<const ForStmt *>(node), os, indent);
        return;
    case AstKind::SwitchStmt:
        dump_switch_stmt(static_cast<const SwitchStmt *>(node), os, indent);
        return;
    case AstKind::CaseStmt:
        dump_case_stmt(static_cast<const CaseStmt *>(node), os, indent);
        return;
    case AstKind::DefaultStmt:
        dump_default_stmt(static_cast<const DefaultStmt *>(node), os, indent);
        return;
    case AstKind::LabelStmt:
        dump_label_stmt(static_cast<const LabelStmt *>(node), os, indent);
        return;
    case AstKind::BreakStmt:
        write_indent(os, indent);
        os << "BreakStmt\n";
        dump_source_span(node, os, indent + 2);
        return;
    case AstKind::ContinueStmt:
        write_indent(os, indent);
        os << "ContinueStmt\n";
        dump_source_span(node, os, indent + 2);
        return;
    case AstKind::GotoStmt:
        dump_goto_stmt(static_cast<const GotoStmt *>(node), os, indent);
        return;
    case AstKind::ReturnStmt:
        dump_return_stmt(static_cast<const ReturnStmt *>(node), os, indent);
        return;
    case AstKind::BuiltinType: {
        const auto *builtin_type =
            static_cast<const BuiltinTypeNode *>(node);
        write_indent(os, indent);
        os << "BuiltinType " << builtin_type->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::NamedType: {
        const auto *named_type = static_cast<const NamedTypeNode *>(node);
        write_indent(os, indent);
        os << "NamedType " << named_type->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::QualifiedType: {
        const auto *qualified_type =
            static_cast<const QualifiedTypeNode *>(node);
        write_indent(os, indent);
        os << "QualifiedType";
        if (qualified_type->get_is_const()) {
            os << " const";
        }
        if (qualified_type->get_is_volatile()) {
            os << " volatile";
        }
        if (!qualified_type->get_is_const() &&
            !qualified_type->get_is_volatile()) {
            os << " <none>";
        }
        os << "\n";
        dump_source_span(node, os, indent + 2);
        dump_node(qualified_type->get_base_type(), os, indent + 2);
        return;
    }
    case AstKind::PointerType: {
        const auto *pointer_type = static_cast<const PointerTypeNode *>(node);
        write_indent(os, indent);
        os << "PointerType\n";
        dump_source_span(node, os, indent + 2);
        if (pointer_type->get_is_const() || pointer_type->get_is_volatile() ||
            pointer_type->get_is_restrict()) {
            write_indent(os, indent + 2);
            os << "PointerQualifiers";
            if (pointer_type->get_is_const()) {
                os << " const";
            }
            if (pointer_type->get_is_volatile()) {
                os << " volatile";
            }
            if (pointer_type->get_is_restrict()) {
                os << " restrict";
            }
            os << "\n";
        }
        if (pointer_type->get_nullability_kind() !=
            PointerNullabilityKind::None) {
            write_indent(os, indent + 2);
            os << "PointerNullability ";
            switch (pointer_type->get_nullability_kind()) {
            case PointerNullabilityKind::Nullable:
                os << "nullable";
                break;
            case PointerNullabilityKind::Nonnull:
                os << "nonnull";
                break;
            case PointerNullabilityKind::NullUnspecified:
                os << "null_unspecified";
                break;
            case PointerNullabilityKind::None:
                break;
            }
            os << "\n";
        }
        dump_node(pointer_type->get_pointee_type(), os, indent + 2);
        return;
    }
    case AstKind::ArrayType: {
        const auto *array_type = static_cast<const ArrayTypeNode *>(node);
        write_indent(os, indent);
        os << "ArrayType\n";
        dump_source_span(node, os, indent + 2);
        dump_node(array_type->get_element_type(), os, indent + 2);
        for (const auto &dimension : array_type->get_dimensions()) {
            write_indent(os, indent + 2);
            os << "Dimension\n";
            dump_node(dimension.get(), os, indent + 4);
        }
        return;
    }
    case AstKind::FunctionType: {
        const auto *function_type = static_cast<const FunctionTypeNode *>(node);
        write_indent(os, indent);
        os << "FunctionType\n";
        dump_source_span(node, os, indent + 2);
        if (function_type->get_is_variadic()) {
            write_indent(os, indent + 2);
            os << "Variadic\n";
        }
        dump_node(function_type->get_return_type(), os, indent + 2);
        for (const auto &parameter_type : function_type->get_parameter_types()) {
            dump_node(parameter_type.get(), os, indent + 2);
        }
        return;
    }
    case AstKind::StructType: {
        const auto *struct_type = static_cast<const StructTypeNode *>(node);
        write_indent(os, indent);
        os << "StructType " << struct_type->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        for (const auto &field : struct_type->get_fields()) {
            dump_node(field.get(), os, indent + 2);
        }
        return;
    }
    case AstKind::UnionType: {
        const auto *union_type = static_cast<const UnionTypeNode *>(node);
        write_indent(os, indent);
        os << "UnionType " << union_type->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        for (const auto &field : union_type->get_fields()) {
            dump_node(field.get(), os, indent + 2);
        }
        return;
    }
    case AstKind::EnumType: {
        const auto *enum_type = static_cast<const EnumTypeNode *>(node);
        write_indent(os, indent);
        os << "EnumType " << enum_type->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::IntegerLiteralExpr: {
        const auto *integer_literal =
            static_cast<const IntegerLiteralExpr *>(node);
        write_indent(os, indent);
        os << "IntegerLiteralExpr " << integer_literal->get_value_text()
           << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::FloatLiteralExpr: {
        const auto *float_literal = static_cast<const FloatLiteralExpr *>(node);
        write_indent(os, indent);
        os << "FloatLiteralExpr " << float_literal->get_value_text() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::CharLiteralExpr: {
        const auto *char_literal = static_cast<const CharLiteralExpr *>(node);
        write_indent(os, indent);
        os << "CharLiteralExpr " << char_literal->get_value_text() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::StringLiteralExpr: {
        const auto *string_literal =
            static_cast<const StringLiteralExpr *>(node);
        write_indent(os, indent);
        os << "StringLiteralExpr " << string_literal->get_value_text() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::IdentifierExpr: {
        const auto *identifier_expr = static_cast<const IdentifierExpr *>(node);
        write_indent(os, indent);
        os << "IdentifierExpr " << identifier_expr->get_name() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::SizeofTypeExpr:
        dump_sizeof_type_expr(static_cast<const SizeofTypeExpr *>(node), os,
                              indent);
        return;
    case AstKind::BuiltinVaArgExpr: {
        const auto *va_arg_expr = static_cast<const BuiltinVaArgExpr *>(node);
        write_indent(os, indent);
        os << "BuiltinVaArgExpr\n";
        dump_source_span(node, os, indent + 2);
        write_indent(os, indent + 2);
        os << "VaList\n";
        dump_node(va_arg_expr->get_va_list_expr(), os, indent + 4);
        write_indent(os, indent + 2);
        os << "TargetType\n";
        dump_node(va_arg_expr->get_target_type(), os, indent + 4);
        return;
    }
    case AstKind::UnaryExpr:
        dump_unary_expr(static_cast<const UnaryExpr *>(node), os, indent);
        return;
    case AstKind::PrefixExpr:
        dump_prefix_expr(static_cast<const PrefixExpr *>(node), os, indent);
        return;
    case AstKind::PostfixExpr:
        dump_postfix_expr(static_cast<const PostfixExpr *>(node), os, indent);
        return;
    case AstKind::BinaryExpr:
        dump_binary_expr(static_cast<const BinaryExpr *>(node), os, indent);
        return;
    case AstKind::CastExpr:
        dump_cast_expr(static_cast<const CastExpr *>(node), os, indent);
        return;
    case AstKind::ConditionalExpr:
        dump_conditional_expr(static_cast<const ConditionalExpr *>(node), os,
                              indent);
        return;
    case AstKind::AssignExpr:
        dump_assign_expr(static_cast<const AssignExpr *>(node), os, indent);
        return;
    case AstKind::CallExpr:
        dump_call_expr(static_cast<const CallExpr *>(node), os, indent);
        return;
    case AstKind::IndexExpr:
        dump_index_expr(static_cast<const IndexExpr *>(node), os, indent);
        return;
    case AstKind::MemberExpr:
        dump_member_expr(static_cast<const MemberExpr *>(node), os, indent);
        return;
    case AstKind::StatementExpr: {
        const auto *statement_expr = static_cast<const StatementExpr *>(node);
        write_indent(os, indent);
        os << "StatementExpr\n";
        dump_source_span(node, os, indent + 2);
        dump_node(statement_expr->get_body(), os, indent + 2);
        return;
    }
    case AstKind::InitListExpr:
        dump_init_list_expr(static_cast<const InitListExpr *>(node), os, indent);
        return;
    case AstKind::UnknownDecl: {
        const auto *unknown_decl = static_cast<const UnknownDecl *>(node);
        write_indent(os, indent);
        os << "UnknownDecl " << unknown_decl->get_summary() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::UnknownStmt: {
        const auto *unknown_stmt = static_cast<const UnknownStmt *>(node);
        write_indent(os, indent);
        os << "UnknownStmt " << unknown_stmt->get_summary() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::UnknownExpr: {
        const auto *unknown_expr = static_cast<const UnknownExpr *>(node);
        write_indent(os, indent);
        os << "UnknownExpr " << unknown_expr->get_summary() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    case AstKind::UnknownType: {
        const auto *unknown_type = static_cast<const UnknownTypeNode *>(node);
        write_indent(os, indent);
        os << "UnknownType " << unknown_type->get_summary() << "\n";
        dump_source_span(node, os, indent + 2);
        return;
    }
    }
}

void AstDumper::dump_translation_unit(const TranslationUnit *node,
                                      std::ostream &os, int indent) const {
    write_indent(os, indent);
    os << "TranslationUnit\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &decl : node->get_top_level_decls()) {
        dump_node(decl.get(), os, indent + 2);
    }
}

void AstDumper::dump_function_decl(const FunctionDecl *node, std::ostream &os,
                                   int indent) const {
    write_indent(os, indent);
    os << "FunctionDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_return_type(), os, indent + 2);
    if (node->get_is_static()) {
        write_indent(os, indent + 2);
        os << "Static\n";
    }
    if (node->get_is_variadic()) {
        write_indent(os, indent + 2);
        os << "Variadic\n";
    }
    dump_attribute_list(node->get_attributes(), os, indent + 2);
    if (!node->get_asm_label().empty()) {
        write_indent(os, indent + 2);
        os << "AsmLabel " << node->get_asm_label() << "\n";
    }
    for (const auto &parameter : node->get_parameters()) {
        dump_node(parameter.get(), os, indent + 2);
    }
    dump_node(node->get_body(), os, indent + 2);
}

void AstDumper::dump_attribute_list(const ParsedAttributeList &attribute_list,
                                    std::ostream &os, int indent) const {
    if (attribute_list.empty()) {
        return;
    }

    write_indent(os, indent);
    os << "Attributes\n";
    for (const auto &attribute : attribute_list.get_attributes()) {
        write_indent(os, indent + 2);
        os << "Attribute " << attribute.get_name() << "\n";
        for (const auto &argument : attribute.get_arguments()) {
            write_indent(os, indent + 4);
            os << "Argument " << argument.get_raw_text() << "\n";
        }
    }
}

void AstDumper::dump_struct_decl(const StructDecl *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "StructDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &field : node->get_fields()) {
        dump_node(field.get(), os, indent + 2);
    }
}

void AstDumper::dump_union_decl(const UnionDecl *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "UnionDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &field : node->get_fields()) {
        dump_node(field.get(), os, indent + 2);
    }
}

void AstDumper::dump_enum_decl(const EnumDecl *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    os << "EnumDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &enumerator : node->get_enumerators()) {
        dump_node(enumerator.get(), os, indent + 2);
    }
}

void AstDumper::dump_typedef_decl(const TypedefDecl *node, std::ostream &os,
                                  int indent) const {
    write_indent(os, indent);
    os << "TypedefDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_aliased_type(), os, indent + 2);
    for (const auto &dimension : node->get_dimensions()) {
        write_indent(os, indent + 2);
        os << (dimension == nullptr ? "Dimension <unspecified>\n" : "Dimension\n");
        if (dimension != nullptr) {
            dump_node(dimension.get(), os, indent + 4);
        }
    }
}

void AstDumper::dump_param_decl(const ParamDecl *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "ParamDecl "
       << (node->get_name().empty() ? "<unnamed>" : node->get_name()) << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_declared_type(), os, indent + 2);
    for (const auto &dimension : node->get_dimensions()) {
        write_indent(os, indent + 2);
        os << (dimension == nullptr ? "Dimension <unspecified>\n" : "Dimension\n");
        if (dimension != nullptr) {
            dump_node(dimension.get(), os, indent + 4);
        }
    }
}

void AstDumper::dump_field_decl(const FieldDecl *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "FieldDecl " << node->get_name();
    if (node->get_bit_width() != nullptr &&
        node->get_bit_width()->get_kind() == AstKind::IntegerLiteralExpr) {
        const auto *integer_literal =
            static_cast<const IntegerLiteralExpr *>(node->get_bit_width());
        os << " : " << integer_literal->get_value_text();
    }
    os << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_declared_type(), os, indent + 2);
    for (const auto &dimension : node->get_dimensions()) {
        write_indent(os, indent + 2);
        os << (dimension == nullptr ? "Dimension <unspecified>\n" : "Dimension\n");
        if (dimension != nullptr) {
            dump_node(dimension.get(), os, indent + 4);
        }
    }
    if (node->get_bit_width() != nullptr) {
        write_indent(os, indent + 2);
        os << "BitWidth\n";
        dump_node(node->get_bit_width(), os, indent + 4);
    }
}

void AstDumper::dump_enumerator_decl(const EnumeratorDecl *node, std::ostream &os,
                                     int indent) const {
    write_indent(os, indent);
    os << "EnumeratorDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    if (node->get_value() != nullptr) {
        write_indent(os, indent + 2);
        os << "Value\n";
        dump_node(node->get_value(), os, indent + 4);
    }
}

void AstDumper::dump_var_decl(const VarDecl *node, std::ostream &os,
                              int indent) const {
    write_indent(os, indent);
    os << "VarDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    if (node->get_is_extern()) {
        write_indent(os, indent + 2);
        os << "Extern\n";
    }
    if (node->get_is_static()) {
        write_indent(os, indent + 2);
        os << "Static\n";
    }
    dump_node(node->get_declared_type(), os, indent + 2);
    for (const auto &dimension : node->get_dimensions()) {
        write_indent(os, indent + 2);
        os << (dimension == nullptr ? "Dimension <unspecified>\n" : "Dimension\n");
        if (dimension != nullptr) {
            dump_node(dimension.get(), os, indent + 4);
        }
    }
    if (node->get_initializer() != nullptr) {
        write_indent(os, indent + 2);
        os << "Initializer\n";
        dump_node(node->get_initializer(), os, indent + 4);
    }
}

void AstDumper::dump_const_decl(const ConstDecl *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "ConstDecl " << node->get_name() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_declared_type(), os, indent + 2);
    for (const auto &dimension : node->get_dimensions()) {
        write_indent(os, indent + 2);
        os << (dimension == nullptr ? "Dimension <unspecified>\n" : "Dimension\n");
        if (dimension != nullptr) {
            dump_node(dimension.get(), os, indent + 4);
        }
    }
    if (node->get_initializer() != nullptr) {
        write_indent(os, indent + 2);
        os << "Initializer\n";
        dump_node(node->get_initializer(), os, indent + 4);
    }
}

void AstDumper::dump_block_stmt(const BlockStmt *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "BlockStmt\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &statement : node->get_statements()) {
        dump_node(statement.get(), os, indent + 2);
    }
}

void AstDumper::dump_decl_stmt(const DeclStmt *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    os << "DeclStmt\n";
    dump_source_span(node, os, indent + 2);
    for (const auto &declaration : node->get_declarations()) {
        dump_node(declaration.get(), os, indent + 2);
    }
}

void AstDumper::dump_expr_stmt(const ExprStmt *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    os << "ExprStmt\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_expression(), os, indent + 2);
}

void AstDumper::dump_if_stmt(const IfStmt *node, std::ostream &os,
                             int indent) const {
    write_indent(os, indent);
    os << "IfStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Condition\n";
    dump_node(node->get_condition(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Then\n";
    dump_node(node->get_then_branch(), os, indent + 4);
    if (node->get_else_branch() != nullptr) {
        write_indent(os, indent + 2);
        os << "Else\n";
        dump_node(node->get_else_branch(), os, indent + 4);
    }
}

void AstDumper::dump_while_stmt(const WhileStmt *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "WhileStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Condition\n";
    dump_node(node->get_condition(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_do_while_stmt(const DoWhileStmt *node, std::ostream &os,
                                   int indent) const {
    write_indent(os, indent);
    os << "DoWhileStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Condition\n";
    dump_node(node->get_condition(), os, indent + 4);
}

void AstDumper::dump_for_stmt(const ForStmt *node, std::ostream &os,
                              int indent) const {
    write_indent(os, indent);
    os << "ForStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Init\n";
    dump_node(node->get_init(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "InitDecl\n";
    dump_node(node->get_init_decl(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Condition\n";
    dump_node(node->get_condition(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Step\n";
    dump_node(node->get_step(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_switch_stmt(const SwitchStmt *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "SwitchStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Condition\n";
    dump_node(node->get_condition(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_case_stmt(const CaseStmt *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    os << "CaseStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Value\n";
    dump_node(node->get_value(), os, indent + 4);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_default_stmt(const DefaultStmt *node, std::ostream &os,
                                  int indent) const {
    write_indent(os, indent);
    os << "DefaultStmt\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_label_stmt(const LabelStmt *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "LabelStmt " << node->get_label_name() << "\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Body\n";
    dump_node(node->get_body(), os, indent + 4);
}

void AstDumper::dump_goto_stmt(const GotoStmt *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    if (node->get_is_indirect()) {
        os << "GotoStmt indirect\n";
        dump_source_span(node, os, indent + 2);
        write_indent(os, indent + 2);
        os << "Target\n";
        dump_node(node->get_indirect_target(), os, indent + 4);
        return;
    }
    os << "GotoStmt " << node->get_target_label() << "\n";
    dump_source_span(node, os, indent + 2);
}

void AstDumper::dump_return_stmt(const ReturnStmt *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "ReturnStmt\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_value(), os, indent + 2);
}

void AstDumper::dump_sizeof_type_expr(const SizeofTypeExpr *node,
                                      std::ostream &os, int indent) const {
    write_indent(os, indent);
    os << "SizeofTypeExpr\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_target_type(), os, indent + 2);
}

void AstDumper::dump_unary_expr(const UnaryExpr *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "UnaryExpr " << node->get_operator_text() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_operand(), os, indent + 2);
}

void AstDumper::dump_prefix_expr(const PrefixExpr *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "PrefixExpr " << node->get_operator_text() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_operand(), os, indent + 2);
}

void AstDumper::dump_postfix_expr(const PostfixExpr *node, std::ostream &os,
                                  int indent) const {
    write_indent(os, indent);
    os << "PostfixExpr " << node->get_operator_text() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_operand(), os, indent + 2);
}

void AstDumper::dump_binary_expr(const BinaryExpr *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "BinaryExpr " << node->get_operator_text() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_lhs(), os, indent + 2);
    dump_node(node->get_rhs(), os, indent + 2);
}

void AstDumper::dump_cast_expr(const CastExpr *node, std::ostream &os,
                               int indent) const {
    os << std::string(indent, ' ') << "CastExpr\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_target_type(), os, indent + 2);
    dump_node(node->get_operand(), os, indent + 2);
}

void AstDumper::dump_conditional_expr(const ConditionalExpr *node,
                                      std::ostream &os, int indent) const {
    write_indent(os, indent);
    os << "ConditionalExpr\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_condition(), os, indent + 2);
    dump_node(node->get_true_expr(), os, indent + 2);
    dump_node(node->get_false_expr(), os, indent + 2);
}

void AstDumper::dump_assign_expr(const AssignExpr *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "AssignExpr " << node->get_operator_text() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_target(), os, indent + 2);
    dump_node(node->get_value(), os, indent + 2);
}

void AstDumper::dump_call_expr(const CallExpr *node, std::ostream &os,
                               int indent) const {
    write_indent(os, indent);
    os << "CallExpr\n";
    dump_source_span(node, os, indent + 2);
    write_indent(os, indent + 2);
    os << "Callee\n";
    dump_node(node->get_callee(), os, indent + 4);
    for (const auto &argument : node->get_arguments()) {
        write_indent(os, indent + 2);
        os << "Argument\n";
        dump_node(argument.get(), os, indent + 4);
    }
}

void AstDumper::dump_index_expr(const IndexExpr *node, std::ostream &os,
                                int indent) const {
    write_indent(os, indent);
    os << "IndexExpr\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_base(), os, indent + 2);
    dump_node(node->get_index(), os, indent + 2);
}

void AstDumper::dump_member_expr(const MemberExpr *node, std::ostream &os,
                                 int indent) const {
    write_indent(os, indent);
    os << "MemberExpr " << node->get_operator_text() << " "
       << node->get_member_name() << "\n";
    dump_source_span(node, os, indent + 2);
    dump_node(node->get_base(), os, indent + 2);
}

void AstDumper::dump_init_list_expr(const InitListExpr *node, std::ostream &os,
                                    int indent) const {
    write_indent(os, indent);
    os << "InitListExpr\n";
    dump_source_span(node, os, indent + 2);
    const auto &elements = node->get_elements();
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto &designator = node->get_element_designator(index);
        if (designator.has_value()) {
            write_indent(os, indent + 2);
            os << "Designator";
            for (const auto &part : *designator) {
                os << (part.kind == InitListExpr::Designator::Kind::Field ? " ."
                                                                           : " [")
                   << part.text;
                if (part.kind == InitListExpr::Designator::Kind::Index) {
                    os << "]";
                }
            }
            os << "\n";
        }
        dump_node(elements[index].get(), os, indent + 2);
    }
}

} // namespace sysycc
