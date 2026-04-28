#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/pointer_nullability_kind.hpp"
#include "common/source_span.hpp"
#include "frontend/ast/ast_kind.hpp"
#include "frontend/attribute/attribute.hpp"

namespace sysycc {

// Base class for all AST nodes.
class AstNode {
  private:
    AstKind kind_;
    SourceSpan source_span_;

  public:
    AstNode(AstKind kind, SourceSpan source_span = {});
    virtual ~AstNode() = default;

    AstKind get_kind() const noexcept;
    const SourceSpan &get_source_span() const noexcept;
};

// Base class for declaration nodes.
class Decl : public AstNode {
  public:
    explicit Decl(AstKind kind, SourceSpan source_span = {});
    ~Decl() override = default;
};

// Base class for statement nodes.
class Stmt : public AstNode {
  public:
    explicit Stmt(AstKind kind, SourceSpan source_span = {});
    ~Stmt() override = default;
};

// Base class for expression nodes.
class Expr : public AstNode {
  public:
    explicit Expr(AstKind kind, SourceSpan source_span = {});
    ~Expr() override = default;
};

// Base class for type nodes.
class TypeNode : public AstNode {
  public:
    explicit TypeNode(AstKind kind, SourceSpan source_span = {});
    ~TypeNode() override = default;
};

// Root node of one parsed source file.
class TranslationUnit : public AstNode {
  private:
    std::vector<std::unique_ptr<Decl>> top_level_decls_;

  public:
    explicit TranslationUnit(SourceSpan source_span = {});

    std::vector<std::unique_ptr<Decl>> &get_top_level_decls() noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_top_level_decls() const
        noexcept;
    void add_top_level_decl(std::unique_ptr<Decl> decl);
};

// Represents a builtin type such as int, float, or void.
class BuiltinTypeNode : public TypeNode {
  private:
    std::string name_;

  public:
    explicit BuiltinTypeNode(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
};

// Represents a named type reference that must be resolved semantically,
// such as a typedef-name.
class NamedTypeNode : public TypeNode {
  private:
    std::string name_;

  public:
    explicit NamedTypeNode(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
};

// Represents a qualified type such as const char.
class QualifiedTypeNode : public TypeNode {
  private:
    bool is_const_;
    bool is_volatile_;
    std::unique_ptr<TypeNode> base_type_;

  public:
    QualifiedTypeNode(bool is_const, bool is_volatile,
                      std::unique_ptr<TypeNode> base_type,
                      SourceSpan source_span = {});

    bool get_is_const() const noexcept;
    bool get_is_volatile() const noexcept;
    const TypeNode *get_base_type() const noexcept;
};

// Represents a pointer type such as int* or struct Node**.
class PointerTypeNode : public TypeNode {
  private:
    std::unique_ptr<TypeNode> pointee_type_;
    bool is_const_;
    bool is_volatile_;
    bool is_restrict_;
    PointerNullabilityKind nullability_kind_;

  public:
    explicit PointerTypeNode(std::unique_ptr<TypeNode> pointee_type,
                             SourceSpan source_span = {}, bool is_const = false,
                             bool is_volatile = false,
                             bool is_restrict = false,
                             PointerNullabilityKind nullability_kind =
                                 PointerNullabilityKind::None);

    const TypeNode *get_pointee_type() const noexcept;
    bool get_is_const() const noexcept;
    bool get_is_volatile() const noexcept;
    bool get_is_restrict() const noexcept;
    PointerNullabilityKind get_nullability_kind() const noexcept;
};

// Represents an abstract array type such as int[2] inside sizeof(type-name).
class ArrayTypeNode : public TypeNode {
  private:
    std::unique_ptr<TypeNode> element_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;

  public:
    ArrayTypeNode(std::unique_ptr<TypeNode> element_type,
                  std::vector<std::unique_ptr<Expr>> dimensions,
                  SourceSpan source_span = {});

    const TypeNode *get_element_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
};

// Represents a function type used under declarators such as function pointers.
class FunctionTypeNode : public TypeNode {
  private:
    std::unique_ptr<TypeNode> return_type_;
    std::vector<std::unique_ptr<TypeNode>> parameter_types_;
    bool is_variadic_;

  public:
    FunctionTypeNode(std::unique_ptr<TypeNode> return_type,
                     std::vector<std::unique_ptr<TypeNode>> parameter_types,
                     bool is_variadic,
                     SourceSpan source_span = {});

    const TypeNode *get_return_type() const noexcept;
    const std::vector<std::unique_ptr<TypeNode>> &get_parameter_types() const
        noexcept;
    bool get_is_variadic() const noexcept;
};

// Represents a named struct type.
class StructTypeNode : public TypeNode {
  private:
    std::string name_;
    std::vector<std::unique_ptr<Decl>> fields_;

  public:
    StructTypeNode(std::string name,
                   std::vector<std::unique_ptr<Decl>> fields = {},
                   SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_fields() const noexcept;
};

// Represents a union type, optionally with inline field declarations.
class UnionTypeNode : public TypeNode {
  private:
    std::string name_;
    std::vector<std::unique_ptr<Decl>> fields_;

  public:
    UnionTypeNode(std::string name, std::vector<std::unique_ptr<Decl>> fields = {},
                  SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_fields() const noexcept;
};

// Represents a named enum type.
class EnumTypeNode : public TypeNode {
  private:
    std::string name_;

  public:
    explicit EnumTypeNode(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
};

// Placeholder type node used until a richer AST builder is implemented.
class UnknownTypeNode : public TypeNode {
  private:
    std::string summary_;

  public:
    explicit UnknownTypeNode(std::string summary, SourceSpan source_span = {});

    const std::string &get_summary() const noexcept;
};

// Represents a function declaration/definition.
class FunctionDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> return_type_;
    std::vector<std::unique_ptr<Decl>> parameters_;
    bool is_static_;
    bool is_variadic_;
    ParsedAttributeList attributes_;
    std::string asm_label_;
    std::unique_ptr<Stmt> body_;

  public:
    FunctionDecl(std::string name, std::unique_ptr<TypeNode> return_type,
                 std::vector<std::unique_ptr<Decl>> parameters,
                 bool is_static, bool is_variadic, ParsedAttributeList attributes,
                 std::string asm_label,
                 std::unique_ptr<Stmt> body,
                 SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_return_type() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_parameters() const noexcept;
    bool get_is_static() const noexcept;
    bool get_is_variadic() const noexcept;
    const ParsedAttributeList &get_attributes() const noexcept;
    const std::string &get_asm_label() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a function parameter declaration.
class ParamDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> declared_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;

  public:
    ParamDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
              std::vector<std::unique_ptr<Expr>> dimensions,
              SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_declared_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
};

// Represents one field inside a struct declaration.
class FieldDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> declared_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;
    std::unique_ptr<Expr> bit_width_;

  public:
    FieldDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
              std::vector<std::unique_ptr<Expr>> dimensions,
              std::unique_ptr<Expr> bit_width,
              SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_declared_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
    const Expr *get_bit_width() const noexcept;
};

// Represents a variable declaration.
class VarDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> declared_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;
    std::unique_ptr<Expr> initializer_;
    bool is_extern_;
    bool is_static_;

  public:
    VarDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
            std::vector<std::unique_ptr<Expr>> dimensions,
            std::unique_ptr<Expr> initializer, bool is_extern, bool is_static,
            SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_declared_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
    const Expr *get_initializer() const noexcept;
    bool get_is_extern() const noexcept;
    bool get_is_static() const noexcept;
};

// Represents a const declaration.
class ConstDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> declared_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;
    std::unique_ptr<Expr> initializer_;

  public:
    ConstDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
              std::vector<std::unique_ptr<Expr>> dimensions,
              std::unique_ptr<Expr> initializer, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_declared_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
    const Expr *get_initializer() const noexcept;
};

// Represents a struct declaration/definition.
class StructDecl : public Decl {
  private:
    std::string name_;
    std::vector<std::unique_ptr<Decl>> fields_;

  public:
    explicit StructDecl(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_fields() const noexcept;
    void add_field(std::unique_ptr<Decl> field);
};

// Represents a union declaration/definition.
class UnionDecl : public Decl {
  private:
    std::string name_;
    std::vector<std::unique_ptr<Decl>> fields_;

  public:
    explicit UnionDecl(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_fields() const noexcept;
    void add_field(std::unique_ptr<Decl> field);
};

// Represents one enum constant inside an enum declaration.
class EnumeratorDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<Expr> value_;

  public:
    explicit EnumeratorDecl(std::string name, std::unique_ptr<Expr> value,
                            SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const Expr *get_value() const noexcept;
};

// Represents an enum declaration/definition.
class EnumDecl : public Decl {
  private:
    std::string name_;
    std::vector<std::unique_ptr<Decl>> enumerators_;

  public:
    explicit EnumDecl(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const std::vector<std::unique_ptr<Decl>> &get_enumerators() const noexcept;
    void add_enumerator(std::unique_ptr<Decl> enumerator);
};

// Represents a typedef declaration.
class TypedefDecl : public Decl {
  private:
    std::string name_;
    std::unique_ptr<TypeNode> aliased_type_;
    std::vector<std::unique_ptr<Expr>> dimensions_;

  public:
    TypedefDecl(std::string name, std::unique_ptr<TypeNode> aliased_type,
                std::vector<std::unique_ptr<Expr>> dimensions,
                SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
    const TypeNode *get_aliased_type() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_dimensions() const noexcept;
};

// Placeholder declaration node used for parse-tree constructs not lowered yet.
class UnknownDecl : public Decl {
  private:
    std::string summary_;

  public:
    explicit UnknownDecl(std::string summary, SourceSpan source_span = {});

    const std::string &get_summary() const noexcept;
};

// Represents a block statement.
class BlockStmt : public Stmt {
  private:
    std::vector<std::unique_ptr<Stmt>> statements_;

  public:
    explicit BlockStmt(SourceSpan source_span = {});

    std::vector<std::unique_ptr<Stmt>> &get_statements() noexcept;
    const std::vector<std::unique_ptr<Stmt>> &get_statements() const noexcept;
    void add_statement(std::unique_ptr<Stmt> statement);
};

// Represents a declaration statement inside a block.
class DeclStmt : public Stmt {
  private:
    std::vector<std::unique_ptr<Decl>> declarations_;

  public:
    explicit DeclStmt(SourceSpan source_span = {});

    const std::vector<std::unique_ptr<Decl>> &get_declarations() const noexcept;
    void add_declaration(std::unique_ptr<Decl> declaration);
};

// Represents an expression statement.
class ExprStmt : public Stmt {
  private:
    std::unique_ptr<Expr> expression_;

  public:
    explicit ExprStmt(std::unique_ptr<Expr> expression,
                      SourceSpan source_span = {});

    const Expr *get_expression() const noexcept;
};

// Represents an if statement.
class IfStmt : public Stmt {
  private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Stmt> then_branch_;
    std::unique_ptr<Stmt> else_branch_;

  public:
    IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> then_branch,
           std::unique_ptr<Stmt> else_branch = nullptr,
           SourceSpan source_span = {});

    const Expr *get_condition() const noexcept;
    const Stmt *get_then_branch() const noexcept;
    const Stmt *get_else_branch() const noexcept;
};

// Represents a while statement.
class WhileStmt : public Stmt {
  private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Stmt> body_;

  public:
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body,
              SourceSpan source_span = {});

    const Expr *get_condition() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a do-while statement.
class DoWhileStmt : public Stmt {
  private:
    std::unique_ptr<Stmt> body_;
    std::unique_ptr<Expr> condition_;

  public:
    DoWhileStmt(std::unique_ptr<Stmt> body, std::unique_ptr<Expr> condition,
                SourceSpan source_span = {});

    const Stmt *get_body() const noexcept;
    const Expr *get_condition() const noexcept;
};

// Represents a for statement.
class ForStmt : public Stmt {
  private:
    std::unique_ptr<Expr> init_;
    std::unique_ptr<DeclStmt> init_decl_;
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Expr> step_;
    std::unique_ptr<Stmt> body_;

  public:
    ForStmt(std::unique_ptr<Expr> init, std::unique_ptr<Expr> condition,
            std::unique_ptr<Expr> step, std::unique_ptr<Stmt> body,
            SourceSpan source_span = {});
    ForStmt(std::unique_ptr<DeclStmt> init_decl,
            std::unique_ptr<Expr> condition, std::unique_ptr<Expr> step,
            std::unique_ptr<Stmt> body, SourceSpan source_span = {});

    const Expr *get_init() const noexcept;
    const DeclStmt *get_init_decl() const noexcept;
    const Expr *get_condition() const noexcept;
    const Expr *get_step() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a switch statement.
class SwitchStmt : public Stmt {
  private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Stmt> body_;

  public:
    SwitchStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body,
               SourceSpan source_span = {});

    const Expr *get_condition() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a case label and its body.
class CaseStmt : public Stmt {
  private:
    std::unique_ptr<Expr> value_;
    std::unique_ptr<Stmt> body_;

  public:
    CaseStmt(std::unique_ptr<Expr> value, std::unique_ptr<Stmt> body,
             SourceSpan source_span = {});

    const Expr *get_value() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a default label and its body.
class DefaultStmt : public Stmt {
  private:
    std::unique_ptr<Stmt> body_;

  public:
    explicit DefaultStmt(std::unique_ptr<Stmt> body, SourceSpan source_span = {});

    const Stmt *get_body() const noexcept;
};

class LabelStmt : public Stmt {
  private:
    std::string label_name_;
    std::unique_ptr<Stmt> body_;

  public:
    LabelStmt(std::string label_name, std::unique_ptr<Stmt> body,
              SourceSpan source_span = {});

    const std::string &get_label_name() const noexcept;
    const Stmt *get_body() const noexcept;
};

// Represents a break statement.
class BreakStmt : public Stmt {
  public:
    explicit BreakStmt(SourceSpan source_span = {});
};

// Represents a continue statement.
class ContinueStmt : public Stmt {
  public:
    explicit ContinueStmt(SourceSpan source_span = {});
};

class GotoStmt : public Stmt {
  private:
    std::string target_label_;
    std::unique_ptr<Expr> indirect_target_;

  public:
    explicit GotoStmt(std::string target_label, SourceSpan source_span = {});
    explicit GotoStmt(std::unique_ptr<Expr> indirect_target,
                      SourceSpan source_span = {});

    const std::string &get_target_label() const noexcept;
    const Expr *get_indirect_target() const noexcept;
    bool get_is_indirect() const noexcept;
};

// Represents a return statement.
class ReturnStmt : public Stmt {
  private:
    std::unique_ptr<Expr> value_;

  public:
    explicit ReturnStmt(std::unique_ptr<Expr> value, SourceSpan source_span = {});

    const Expr *get_value() const noexcept;
};

// Placeholder statement node used until lowering is implemented.
class UnknownStmt : public Stmt {
  private:
    std::string summary_;

  public:
    explicit UnknownStmt(std::string summary, SourceSpan source_span = {});

    const std::string &get_summary() const noexcept;
};

// Represents an integer literal.
class IntegerLiteralExpr : public Expr {
  private:
    std::string value_text_;

  public:
    explicit IntegerLiteralExpr(std::string value_text,
                                SourceSpan source_span = {});

    const std::string &get_value_text() const noexcept;
};

// Represents a floating-point literal.
class FloatLiteralExpr : public Expr {
  private:
    std::string value_text_;

  public:
    explicit FloatLiteralExpr(std::string value_text, SourceSpan source_span = {});

    const std::string &get_value_text() const noexcept;
};

// Represents a character literal.
class CharLiteralExpr : public Expr {
  private:
    std::string value_text_;

  public:
    explicit CharLiteralExpr(std::string value_text, SourceSpan source_span = {});

    const std::string &get_value_text() const noexcept;
};

// Represents a string literal.
class StringLiteralExpr : public Expr {
  private:
    std::string value_text_;

  public:
    explicit StringLiteralExpr(std::string value_text, SourceSpan source_span = {});

    const std::string &get_value_text() const noexcept;
};

// Represents an identifier reference.
class IdentifierExpr : public Expr {
  private:
    std::string name_;

  public:
    explicit IdentifierExpr(std::string name, SourceSpan source_span = {});

    const std::string &get_name() const noexcept;
};

// Represents sizeof(type-name).
class SizeofTypeExpr : public Expr {
  private:
    std::unique_ptr<TypeNode> target_type_;

  public:
    explicit SizeofTypeExpr(std::unique_ptr<TypeNode> target_type,
                            SourceSpan source_span = {});

    const TypeNode *get_target_type() const noexcept;
};

// Represents __builtin_va_arg(ap, type-name).
class BuiltinVaArgExpr : public Expr {
  private:
    std::unique_ptr<Expr> va_list_expr_;
    std::unique_ptr<TypeNode> target_type_;

  public:
    BuiltinVaArgExpr(std::unique_ptr<Expr> va_list_expr,
                     std::unique_ptr<TypeNode> target_type,
                     SourceSpan source_span = {});

    const Expr *get_va_list_expr() const noexcept;
    const TypeNode *get_target_type() const noexcept;
};

// Represents a unary operator expression such as -x, !x, &x, or *p.
class UnaryExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> operand_;

  public:
    UnaryExpr(std::string operator_text, std::unique_ptr<Expr> operand,
              SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_operand() const noexcept;
};

// Represents a prefix increment/decrement expression.
class PrefixExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> operand_;

  public:
    PrefixExpr(std::string operator_text, std::unique_ptr<Expr> operand,
               SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_operand() const noexcept;
};

// Represents a postfix increment/decrement expression.
class PostfixExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> operand_;

  public:
    PostfixExpr(std::string operator_text, std::unique_ptr<Expr> operand,
                SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_operand() const noexcept;
};

// Represents a binary expression.
class BinaryExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> lhs_;
    std::unique_ptr<Expr> rhs_;

  public:
    BinaryExpr(std::string operator_text, std::unique_ptr<Expr> lhs,
               std::unique_ptr<Expr> rhs, SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_lhs() const noexcept;
    const Expr *get_rhs() const noexcept;
};

// Represents a C-style cast expression such as (int)value.
class CastExpr : public Expr {
  private:
    std::unique_ptr<TypeNode> target_type_;
    std::unique_ptr<Expr> operand_;

  public:
    CastExpr(std::unique_ptr<TypeNode> target_type, std::unique_ptr<Expr> operand,
             SourceSpan source_span = {});

    const TypeNode *get_target_type() const noexcept;
    const Expr *get_operand() const noexcept;
};

// Represents a conditional operator expression such as cond ? lhs : rhs.
class ConditionalExpr : public Expr {
  private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Expr> true_expr_;
    std::unique_ptr<Expr> false_expr_;

  public:
    ConditionalExpr(std::unique_ptr<Expr> condition,
                    std::unique_ptr<Expr> true_expr,
                    std::unique_ptr<Expr> false_expr,
                    SourceSpan source_span = {});

    const Expr *get_condition() const noexcept;
    const Expr *get_true_expr() const noexcept;
    const Expr *get_false_expr() const noexcept;
};

// Represents an assignment expression.
class AssignExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> target_;
    std::unique_ptr<Expr> value_;

  public:
    AssignExpr(std::string operator_text, std::unique_ptr<Expr> target,
               std::unique_ptr<Expr> value,
               SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_target() const noexcept;
    const Expr *get_value() const noexcept;
};

// Represents a function call expression.
class CallExpr : public Expr {
  private:
    std::unique_ptr<Expr> callee_;
    std::vector<std::unique_ptr<Expr>> arguments_;

  public:
    CallExpr(std::unique_ptr<Expr> callee,
             std::vector<std::unique_ptr<Expr>> arguments,
             SourceSpan source_span = {});

    const Expr *get_callee() const noexcept;
    const std::vector<std::unique_ptr<Expr>> &get_arguments() const noexcept;
};

// Represents an array indexing expression.
class IndexExpr : public Expr {
  private:
    std::unique_ptr<Expr> base_;
    std::unique_ptr<Expr> index_;

  public:
    IndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> index,
              SourceSpan source_span = {});

    const Expr *get_base() const noexcept;
    const Expr *get_index() const noexcept;
};

// Represents a member access expression such as ptr->value.
class MemberExpr : public Expr {
  private:
    std::string operator_text_;
    std::unique_ptr<Expr> base_;
    std::string member_name_;

  public:
    MemberExpr(std::string operator_text, std::unique_ptr<Expr> base,
               std::string member_name, SourceSpan source_span = {});

    const std::string &get_operator_text() const noexcept;
    const Expr *get_base() const noexcept;
    const std::string &get_member_name() const noexcept;
};

// Represents a GNU statement expression such as ({ ... }).
class StatementExpr : public Expr {
  private:
    std::unique_ptr<Stmt> body_;

  public:
    explicit StatementExpr(std::unique_ptr<Stmt> body,
                           SourceSpan source_span = {});

    const Stmt *get_body() const noexcept;
};

// Represents an initializer list expression.
class InitListExpr : public Expr {
  private:
    std::vector<std::unique_ptr<Expr>> elements_;

  public:
    explicit InitListExpr(SourceSpan source_span = {});

    const std::vector<std::unique_ptr<Expr>> &get_elements() const noexcept;
    void add_element(std::unique_ptr<Expr> element);
};

// Placeholder expression node used until lowering is implemented.
class UnknownExpr : public Expr {
  private:
    std::string summary_;

  public:
    explicit UnknownExpr(std::string summary, SourceSpan source_span = {});

    const std::string &get_summary() const noexcept;
};

} // namespace sysycc
