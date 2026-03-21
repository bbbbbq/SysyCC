#pragma once

#include <memory>

#include "frontend/ast/ast_node.hpp"
#include "frontend/ast/detail/ast_builder_context.hpp"
#include "frontend/attribute/attribute.hpp"

namespace sysycc::detail {

// Lowers the parser runtime tree into a cleaner AST tree.
class AstBuilder {
  public:
    std::unique_ptr<TranslationUnit> build(const AstBuilderContext &context) const;

  private:
    void collect_top_level_items(
        const ParseTreeNode *node,
        std::vector<const ParseTreeNode *> &items) const;
    void add_top_level_decls(const ParseTreeNode *node,
                             TranslationUnit &translation_unit) const;
    std::unique_ptr<FunctionDecl> build_function_decl(
        const ParseTreeNode *node) const;
    ParsedAttributeList build_decl_attributes(const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_parameters(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_decl_group(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_const_decls(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_var_decls(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_typedef_decls(
        const ParseTreeNode *node) const;
    std::unique_ptr<StructDecl> build_struct_decl(
        const ParseTreeNode *node) const;
    std::unique_ptr<UnionDecl> build_union_decl(
        const ParseTreeNode *node) const;
    std::unique_ptr<EnumDecl> build_enum_decl(const ParseTreeNode *node) const;
    std::unique_ptr<TypeNode> build_return_type(const ParseTreeNode *node) const;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::unique_ptr<TypeNode> build_declared_type(
        const ParseTreeNode *type_specifier, const ParseTreeNode *declarator,
        bool pointee_is_const = false) const;
    std::vector<std::unique_ptr<Decl>> build_struct_fields(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_union_fields(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Decl>> build_enumerators(
        const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Expr>> collect_declarator_dimensions(
        const ParseTreeNode *node) const;
    std::string extract_declarator_name(const ParseTreeNode *node) const;
    std::unique_ptr<BlockStmt> build_block_stmt(const ParseTreeNode *node) const;
    void collect_block_items(const ParseTreeNode *node,
                             std::vector<const ParseTreeNode *> &items) const;
    std::unique_ptr<Stmt> build_block_item(const ParseTreeNode *node) const;
    std::unique_ptr<Stmt> build_stmt(const ParseTreeNode *node) const;
    std::unique_ptr<Expr> build_expr(const ParseTreeNode *node) const;
    std::vector<std::unique_ptr<Expr>> build_argument_exprs(
        const ParseTreeNode *node) const;
    void collect_argument_expr_nodes(
        const ParseTreeNode *node,
        std::vector<const ParseTreeNode *> &expr_nodes) const;
    std::unique_ptr<InitListExpr> build_init_list_expr(
        const ParseTreeNode *node) const;
    std::unique_ptr<TypeNode> build_cast_target_type(
        const ParseTreeNode *node) const;
    void collect_direct_init_value_nodes(
        const ParseTreeNode *node,
        std::vector<const ParseTreeNode *> &nodes) const;
    int count_pointer_levels(const ParseTreeNode *node) const;
    SourceSpan get_node_source_span(const ParseTreeNode *node) const;
    bool is_binary_operator_label(const ParseTreeNode *node) const;
    std::string get_operator_text(const ParseTreeNode *node) const;
};

} // namespace sysycc::detail
