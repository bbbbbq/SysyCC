#pragma once

#include <iosfwd>

#include "frontend/ast/ast_node.hpp"

namespace sysycc {

// Dumps AST nodes in a human-readable tree form.
class AstDumper {
  public:
    void dump_to_stream(const AstNode *node, std::ostream &os) const;

  private:
    void dump_source_span(const AstNode *node, std::ostream &os,
                          int indent) const;
    void dump_node(const AstNode *node, std::ostream &os, int indent) const;
    void dump_translation_unit(const TranslationUnit *node, std::ostream &os,
                               int indent) const;
    void dump_function_decl(const FunctionDecl *node, std::ostream &os,
                            int indent) const;
    void dump_struct_decl(const StructDecl *node, std::ostream &os,
                          int indent) const;
    void dump_enum_decl(const EnumDecl *node, std::ostream &os, int indent) const;
    void dump_typedef_decl(const TypedefDecl *node, std::ostream &os,
                           int indent) const;
    void dump_field_decl(const FieldDecl *node, std::ostream &os, int indent) const;
    void dump_enumerator_decl(const EnumeratorDecl *node, std::ostream &os,
                              int indent) const;
    void dump_var_decl(const VarDecl *node, std::ostream &os, int indent) const;
    void dump_const_decl(const ConstDecl *node, std::ostream &os,
                         int indent) const;
    void dump_param_decl(const ParamDecl *node, std::ostream &os, int indent) const;
    void dump_block_stmt(const BlockStmt *node, std::ostream &os,
                         int indent) const;
    void dump_decl_stmt(const DeclStmt *node, std::ostream &os, int indent) const;
    void dump_expr_stmt(const ExprStmt *node, std::ostream &os, int indent) const;
    void dump_if_stmt(const IfStmt *node, std::ostream &os, int indent) const;
    void dump_while_stmt(const WhileStmt *node, std::ostream &os, int indent) const;
    void dump_do_while_stmt(const DoWhileStmt *node, std::ostream &os,
                            int indent) const;
    void dump_for_stmt(const ForStmt *node, std::ostream &os, int indent) const;
    void dump_switch_stmt(const SwitchStmt *node, std::ostream &os,
                          int indent) const;
    void dump_case_stmt(const CaseStmt *node, std::ostream &os, int indent) const;
    void dump_default_stmt(const DefaultStmt *node, std::ostream &os,
                           int indent) const;
    void dump_return_stmt(const ReturnStmt *node, std::ostream &os,
                          int indent) const;
    void dump_unary_expr(const UnaryExpr *node, std::ostream &os, int indent) const;
    void dump_prefix_expr(const PrefixExpr *node, std::ostream &os, int indent) const;
    void dump_postfix_expr(const PostfixExpr *node, std::ostream &os,
                           int indent) const;
    void dump_binary_expr(const BinaryExpr *node, std::ostream &os,
                          int indent) const;
    void dump_assign_expr(const AssignExpr *node, std::ostream &os,
                          int indent) const;
    void dump_call_expr(const CallExpr *node, std::ostream &os, int indent) const;
    void dump_index_expr(const IndexExpr *node, std::ostream &os,
                         int indent) const;
    void dump_member_expr(const MemberExpr *node, std::ostream &os,
                          int indent) const;
    void dump_init_list_expr(const InitListExpr *node, std::ostream &os,
                             int indent) const;
};

} // namespace sysycc
