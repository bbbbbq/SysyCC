#include "frontend/ast/ast_node.hpp"

#include <utility>

namespace sysycc {

AstNode::AstNode(AstKind kind, SourceSpan source_span)
    : kind_(kind), source_span_(source_span) {}

AstKind AstNode::get_kind() const noexcept { return kind_; }

const SourceSpan &AstNode::get_source_span() const noexcept {
    return source_span_;
}

Decl::Decl(AstKind kind, SourceSpan source_span)
    : AstNode(kind, source_span) {}

Stmt::Stmt(AstKind kind, SourceSpan source_span)
    : AstNode(kind, source_span) {}

Expr::Expr(AstKind kind, SourceSpan source_span)
    : AstNode(kind, source_span) {}

TypeNode::TypeNode(AstKind kind, SourceSpan source_span)
    : AstNode(kind, source_span) {}

TranslationUnit::TranslationUnit(SourceSpan source_span)
    : AstNode(AstKind::TranslationUnit, source_span) {}

std::vector<std::unique_ptr<Decl>> &TranslationUnit::get_top_level_decls()
    noexcept {
    return top_level_decls_;
}

const std::vector<std::unique_ptr<Decl>> &
TranslationUnit::get_top_level_decls() const noexcept {
    return top_level_decls_;
}

void TranslationUnit::add_top_level_decl(std::unique_ptr<Decl> decl) {
    if (decl == nullptr) {
        return;
    }
    top_level_decls_.push_back(std::move(decl));
}

BuiltinTypeNode::BuiltinTypeNode(std::string name, SourceSpan source_span)
    : TypeNode(AstKind::BuiltinType, source_span),
      name_(std::move(name)) {}

const std::string &BuiltinTypeNode::get_name() const noexcept { return name_; }

NamedTypeNode::NamedTypeNode(std::string name, SourceSpan source_span)
    : TypeNode(AstKind::NamedType, source_span), name_(std::move(name)) {}

const std::string &NamedTypeNode::get_name() const noexcept { return name_; }

QualifiedTypeNode::QualifiedTypeNode(bool is_const, bool is_volatile,
                                     std::unique_ptr<TypeNode> base_type,
                                     SourceSpan source_span)
    : TypeNode(AstKind::QualifiedType, source_span), is_const_(is_const),
      is_volatile_(is_volatile),
      base_type_(std::move(base_type)) {}

bool QualifiedTypeNode::get_is_const() const noexcept { return is_const_; }

bool QualifiedTypeNode::get_is_volatile() const noexcept {
    return is_volatile_;
}

const TypeNode *QualifiedTypeNode::get_base_type() const noexcept {
    return base_type_.get();
}

PointerTypeNode::PointerTypeNode(std::unique_ptr<TypeNode> pointee_type,
                                 SourceSpan source_span, bool is_const,
                                 bool is_volatile,
                                 bool is_restrict,
                                 PointerNullabilityKind nullability_kind)
    : TypeNode(AstKind::PointerType, source_span),
      pointee_type_(std::move(pointee_type)), is_const_(is_const),
      is_volatile_(is_volatile),
      is_restrict_(is_restrict), nullability_kind_(nullability_kind) {}

const TypeNode *PointerTypeNode::get_pointee_type() const noexcept {
    return pointee_type_.get();
}

bool PointerTypeNode::get_is_const() const noexcept { return is_const_; }

bool PointerTypeNode::get_is_volatile() const noexcept { return is_volatile_; }

bool PointerTypeNode::get_is_restrict() const noexcept { return is_restrict_; }

PointerNullabilityKind PointerTypeNode::get_nullability_kind() const noexcept {
    return nullability_kind_;
}

ArrayTypeNode::ArrayTypeNode(std::unique_ptr<TypeNode> element_type,
                             std::vector<std::unique_ptr<Expr>> dimensions,
                             SourceSpan source_span)
    : TypeNode(AstKind::ArrayType, source_span),
      element_type_(std::move(element_type)),
      dimensions_(std::move(dimensions)) {}

const TypeNode *ArrayTypeNode::get_element_type() const noexcept {
    return element_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &ArrayTypeNode::get_dimensions() const noexcept {
    return dimensions_;
}

FunctionTypeNode::FunctionTypeNode(
    std::unique_ptr<TypeNode> return_type,
    std::vector<std::unique_ptr<TypeNode>> parameter_types, bool is_variadic,
    SourceSpan source_span)
    : TypeNode(AstKind::FunctionType, source_span),
      return_type_(std::move(return_type)),
      parameter_types_(std::move(parameter_types)), is_variadic_(is_variadic) {}

const TypeNode *FunctionTypeNode::get_return_type() const noexcept {
    return return_type_.get();
}

const std::vector<std::unique_ptr<TypeNode>> &
FunctionTypeNode::get_parameter_types() const noexcept {
    return parameter_types_;
}

bool FunctionTypeNode::get_is_variadic() const noexcept { return is_variadic_; }

StructTypeNode::StructTypeNode(std::string name,
                               std::vector<std::unique_ptr<Decl>> fields,
                               SourceSpan source_span)
    : TypeNode(AstKind::StructType, source_span), name_(std::move(name)),
      fields_(std::move(fields)) {}

const std::string &StructTypeNode::get_name() const noexcept { return name_; }

const std::vector<std::unique_ptr<Decl>> &StructTypeNode::get_fields() const noexcept {
    return fields_;
}

UnionTypeNode::UnionTypeNode(std::string name,
                             std::vector<std::unique_ptr<Decl>> fields,
                             SourceSpan source_span)
    : TypeNode(AstKind::UnionType, source_span), name_(std::move(name)),
      fields_(std::move(fields)) {}

const std::string &UnionTypeNode::get_name() const noexcept { return name_; }

const std::vector<std::unique_ptr<Decl>> &UnionTypeNode::get_fields() const noexcept {
    return fields_;
}

EnumTypeNode::EnumTypeNode(std::string name, SourceSpan source_span)
    : TypeNode(AstKind::EnumType, source_span), name_(std::move(name)) {}

const std::string &EnumTypeNode::get_name() const noexcept { return name_; }

UnknownTypeNode::UnknownTypeNode(std::string summary, SourceSpan source_span)
    : TypeNode(AstKind::UnknownType, source_span),
      summary_(std::move(summary)) {}

const std::string &UnknownTypeNode::get_summary() const noexcept {
    return summary_;
}

FunctionDecl::FunctionDecl(std::string name,
                           std::unique_ptr<TypeNode> return_type,
                           std::vector<std::unique_ptr<Decl>> parameters,
                           bool is_static, bool is_variadic,
                           ParsedAttributeList attributes,
                           std::string asm_label,
                           std::unique_ptr<Stmt> body, SourceSpan source_span)
    : Decl(AstKind::FunctionDecl, source_span),
      name_(std::move(name)), return_type_(std::move(return_type)),
      parameters_(std::move(parameters)), is_static_(is_static),
      is_variadic_(is_variadic),
      attributes_(std::move(attributes)),
      asm_label_(std::move(asm_label)), body_(std::move(body)) {}

const std::string &FunctionDecl::get_name() const noexcept { return name_; }

const TypeNode *FunctionDecl::get_return_type() const noexcept {
    return return_type_.get();
}

const std::vector<std::unique_ptr<Decl>> &FunctionDecl::get_parameters() const
    noexcept {
    return parameters_;
}

bool FunctionDecl::get_is_static() const noexcept { return is_static_; }

bool FunctionDecl::get_is_variadic() const noexcept { return is_variadic_; }

const ParsedAttributeList &FunctionDecl::get_attributes() const noexcept {
    return attributes_;
}

const std::string &FunctionDecl::get_asm_label() const noexcept {
    return asm_label_;
}

const Stmt *FunctionDecl::get_body() const noexcept { return body_.get(); }

ParamDecl::ParamDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
                     std::vector<std::unique_ptr<Expr>> dimensions,
                     SourceSpan source_span)
    : Decl(AstKind::ParamDecl, source_span), name_(std::move(name)),
      declared_type_(std::move(declared_type)),
      dimensions_(std::move(dimensions)) {}

const std::string &ParamDecl::get_name() const noexcept { return name_; }

const TypeNode *ParamDecl::get_declared_type() const noexcept {
    return declared_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &ParamDecl::get_dimensions() const
    noexcept {
    return dimensions_;
}

FieldDecl::FieldDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
                     std::vector<std::unique_ptr<Expr>> dimensions,
                     std::unique_ptr<Expr> bit_width,
                     SourceSpan source_span)
    : Decl(AstKind::FieldDecl, source_span), name_(std::move(name)),
      declared_type_(std::move(declared_type)),
      dimensions_(std::move(dimensions)), bit_width_(std::move(bit_width)) {}

const std::string &FieldDecl::get_name() const noexcept { return name_; }

const TypeNode *FieldDecl::get_declared_type() const noexcept {
    return declared_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &FieldDecl::get_dimensions() const
    noexcept {
    return dimensions_;
}

const Expr *FieldDecl::get_bit_width() const noexcept { return bit_width_.get(); }

VarDecl::VarDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
                 std::vector<std::unique_ptr<Expr>> dimensions,
                 std::unique_ptr<Expr> initializer, bool is_extern,
                 bool is_static,
                 SourceSpan source_span)
    : Decl(AstKind::VarDecl, source_span), name_(std::move(name)),
      declared_type_(std::move(declared_type)),
      dimensions_(std::move(dimensions)), initializer_(std::move(initializer)),
      is_extern_(is_extern), is_static_(is_static) {
}

const std::string &VarDecl::get_name() const noexcept { return name_; }

const TypeNode *VarDecl::get_declared_type() const noexcept {
    return declared_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &VarDecl::get_dimensions() const
    noexcept {
    return dimensions_;
}

const Expr *VarDecl::get_initializer() const noexcept {
    return initializer_.get();
}

bool VarDecl::get_is_extern() const noexcept { return is_extern_; }

bool VarDecl::get_is_static() const noexcept { return is_static_; }

ConstDecl::ConstDecl(std::string name, std::unique_ptr<TypeNode> declared_type,
                     std::vector<std::unique_ptr<Expr>> dimensions,
                     std::unique_ptr<Expr> initializer, SourceSpan source_span)
    : Decl(AstKind::ConstDecl, source_span), name_(std::move(name)),
      declared_type_(std::move(declared_type)),
      dimensions_(std::move(dimensions)), initializer_(std::move(initializer)) {
}

const std::string &ConstDecl::get_name() const noexcept { return name_; }

const TypeNode *ConstDecl::get_declared_type() const noexcept {
    return declared_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &ConstDecl::get_dimensions() const
    noexcept {
    return dimensions_;
}

const Expr *ConstDecl::get_initializer() const noexcept {
    return initializer_.get();
}

StructDecl::StructDecl(std::string name, SourceSpan source_span)
    : Decl(AstKind::StructDecl, source_span), name_(std::move(name)) {}

const std::string &StructDecl::get_name() const noexcept { return name_; }

const std::vector<std::unique_ptr<Decl>> &StructDecl::get_fields() const noexcept {
    return fields_;
}

void StructDecl::add_field(std::unique_ptr<Decl> field) {
    if (field == nullptr) {
        return;
    }
    fields_.push_back(std::move(field));
}

UnionDecl::UnionDecl(std::string name, SourceSpan source_span)
    : Decl(AstKind::UnionDecl, source_span), name_(std::move(name)) {}

const std::string &UnionDecl::get_name() const noexcept { return name_; }

const std::vector<std::unique_ptr<Decl>> &UnionDecl::get_fields() const noexcept {
    return fields_;
}

void UnionDecl::add_field(std::unique_ptr<Decl> field) {
    if (field == nullptr) {
        return;
    }
    fields_.push_back(std::move(field));
}

EnumeratorDecl::EnumeratorDecl(std::string name, std::unique_ptr<Expr> value,
                               SourceSpan source_span)
    : Decl(AstKind::EnumeratorDecl, source_span),
      name_(std::move(name)), value_(std::move(value)) {}

const std::string &EnumeratorDecl::get_name() const noexcept { return name_; }

const Expr *EnumeratorDecl::get_value() const noexcept { return value_.get(); }

EnumDecl::EnumDecl(std::string name, SourceSpan source_span)
    : Decl(AstKind::EnumDecl, source_span), name_(std::move(name)) {}

const std::string &EnumDecl::get_name() const noexcept { return name_; }

const std::vector<std::unique_ptr<Decl>> &EnumDecl::get_enumerators() const noexcept {
    return enumerators_;
}

void EnumDecl::add_enumerator(std::unique_ptr<Decl> enumerator) {
    if (enumerator == nullptr) {
        return;
    }
    enumerators_.push_back(std::move(enumerator));
}

TypedefDecl::TypedefDecl(std::string name, std::unique_ptr<TypeNode> aliased_type,
                         std::vector<std::unique_ptr<Expr>> dimensions,
                         SourceSpan source_span)
    : Decl(AstKind::TypedefDecl, source_span),
      name_(std::move(name)), aliased_type_(std::move(aliased_type)),
      dimensions_(std::move(dimensions)) {}

const std::string &TypedefDecl::get_name() const noexcept { return name_; }

const TypeNode *TypedefDecl::get_aliased_type() const noexcept {
    return aliased_type_.get();
}

const std::vector<std::unique_ptr<Expr>> &TypedefDecl::get_dimensions() const noexcept {
    return dimensions_;
}

UnknownDecl::UnknownDecl(std::string summary, SourceSpan source_span)
    : Decl(AstKind::UnknownDecl, source_span),
      summary_(std::move(summary)) {}

const std::string &UnknownDecl::get_summary() const noexcept {
    return summary_;
}

BlockStmt::BlockStmt(SourceSpan source_span)
    : Stmt(AstKind::BlockStmt, source_span) {}

std::vector<std::unique_ptr<Stmt>> &BlockStmt::get_statements() noexcept {
    return statements_;
}

const std::vector<std::unique_ptr<Stmt>> &BlockStmt::get_statements() const
    noexcept {
    return statements_;
}

void BlockStmt::add_statement(std::unique_ptr<Stmt> statement) {
    if (statement == nullptr) {
        return;
    }
    statements_.push_back(std::move(statement));
}

DeclStmt::DeclStmt(SourceSpan source_span)
    : Stmt(AstKind::DeclStmt, source_span) {}

const std::vector<std::unique_ptr<Decl>> &DeclStmt::get_declarations() const
    noexcept {
    return declarations_;
}

void DeclStmt::add_declaration(std::unique_ptr<Decl> declaration) {
    if (declaration == nullptr) {
        return;
    }
    declarations_.push_back(std::move(declaration));
}

ExprStmt::ExprStmt(std::unique_ptr<Expr> expression, SourceSpan source_span)
    : Stmt(AstKind::ExprStmt, source_span),
      expression_(std::move(expression)) {}

const Expr *ExprStmt::get_expression() const noexcept {
    return expression_.get();
}

IfStmt::IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> then_branch,
               std::unique_ptr<Stmt> else_branch, SourceSpan source_span)
    : Stmt(AstKind::IfStmt, source_span),
      condition_(std::move(condition)), then_branch_(std::move(then_branch)),
      else_branch_(std::move(else_branch)) {}

const Expr *IfStmt::get_condition() const noexcept { return condition_.get(); }

const Stmt *IfStmt::get_then_branch() const noexcept {
    return then_branch_.get();
}

const Stmt *IfStmt::get_else_branch() const noexcept {
    return else_branch_.get();
}

WhileStmt::WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body,
                     SourceSpan source_span)
    : Stmt(AstKind::WhileStmt, source_span),
      condition_(std::move(condition)), body_(std::move(body)) {}

const Expr *WhileStmt::get_condition() const noexcept { return condition_.get(); }

const Stmt *WhileStmt::get_body() const noexcept { return body_.get(); }

DoWhileStmt::DoWhileStmt(std::unique_ptr<Stmt> body,
                         std::unique_ptr<Expr> condition, SourceSpan source_span)
    : Stmt(AstKind::DoWhileStmt, source_span),
      body_(std::move(body)), condition_(std::move(condition)) {}

const Stmt *DoWhileStmt::get_body() const noexcept { return body_.get(); }

const Expr *DoWhileStmt::get_condition() const noexcept {
    return condition_.get();
}

ForStmt::ForStmt(std::unique_ptr<Expr> init, std::unique_ptr<Expr> condition,
                 std::unique_ptr<Expr> step, std::unique_ptr<Stmt> body,
                 SourceSpan source_span)
    : Stmt(AstKind::ForStmt, source_span), init_(std::move(init)),
      condition_(std::move(condition)), step_(std::move(step)),
      body_(std::move(body)) {}

ForStmt::ForStmt(std::unique_ptr<DeclStmt> init_decl,
                 std::unique_ptr<Expr> condition, std::unique_ptr<Expr> step,
                 std::unique_ptr<Stmt> body, SourceSpan source_span)
    : Stmt(AstKind::ForStmt, source_span), init_decl_(std::move(init_decl)),
      condition_(std::move(condition)), step_(std::move(step)),
      body_(std::move(body)) {}

const Expr *ForStmt::get_init() const noexcept { return init_.get(); }

const DeclStmt *ForStmt::get_init_decl() const noexcept {
    return init_decl_.get();
}

const Expr *ForStmt::get_condition() const noexcept { return condition_.get(); }

const Expr *ForStmt::get_step() const noexcept { return step_.get(); }

const Stmt *ForStmt::get_body() const noexcept { return body_.get(); }

SwitchStmt::SwitchStmt(std::unique_ptr<Expr> condition,
                       std::unique_ptr<Stmt> body, SourceSpan source_span)
    : Stmt(AstKind::SwitchStmt, source_span),
      condition_(std::move(condition)), body_(std::move(body)) {}

const Expr *SwitchStmt::get_condition() const noexcept {
    return condition_.get();
}

const Stmt *SwitchStmt::get_body() const noexcept { return body_.get(); }

CaseStmt::CaseStmt(std::unique_ptr<Expr> value, std::unique_ptr<Stmt> body,
                   SourceSpan source_span)
    : Stmt(AstKind::CaseStmt, source_span), value_(std::move(value)),
      body_(std::move(body)) {}

const Expr *CaseStmt::get_value() const noexcept { return value_.get(); }

const Stmt *CaseStmt::get_body() const noexcept { return body_.get(); }

DefaultStmt::DefaultStmt(std::unique_ptr<Stmt> body, SourceSpan source_span)
    : Stmt(AstKind::DefaultStmt, source_span),
      body_(std::move(body)) {}

const Stmt *DefaultStmt::get_body() const noexcept { return body_.get(); }

LabelStmt::LabelStmt(std::string label_name, std::unique_ptr<Stmt> body,
                     SourceSpan source_span)
    : Stmt(AstKind::LabelStmt, source_span),
      label_name_(std::move(label_name)), body_(std::move(body)) {}

const std::string &LabelStmt::get_label_name() const noexcept {
    return label_name_;
}

const Stmt *LabelStmt::get_body() const noexcept { return body_.get(); }

BreakStmt::BreakStmt(SourceSpan source_span)
    : Stmt(AstKind::BreakStmt, source_span) {}

ContinueStmt::ContinueStmt(SourceSpan source_span)
    : Stmt(AstKind::ContinueStmt, source_span) {}

GotoStmt::GotoStmt(std::string target_label, SourceSpan source_span)
    : Stmt(AstKind::GotoStmt, source_span),
      target_label_(std::move(target_label)) {}

GotoStmt::GotoStmt(std::unique_ptr<Expr> indirect_target, SourceSpan source_span)
    : Stmt(AstKind::GotoStmt, source_span),
      indirect_target_(std::move(indirect_target)) {}

const std::string &GotoStmt::get_target_label() const noexcept {
    return target_label_;
}

const Expr *GotoStmt::get_indirect_target() const noexcept {
    return indirect_target_.get();
}

bool GotoStmt::get_is_indirect() const noexcept {
    return indirect_target_ != nullptr;
}

ReturnStmt::ReturnStmt(std::unique_ptr<Expr> value, SourceSpan source_span)
    : Stmt(AstKind::ReturnStmt, source_span),
      value_(std::move(value)) {}

const Expr *ReturnStmt::get_value() const noexcept { return value_.get(); }

UnknownStmt::UnknownStmt(std::string summary, SourceSpan source_span)
    : Stmt(AstKind::UnknownStmt, source_span),
      summary_(std::move(summary)) {}

const std::string &UnknownStmt::get_summary() const noexcept {
    return summary_;
}

IntegerLiteralExpr::IntegerLiteralExpr(std::string value_text,
                                       SourceSpan source_span)
    : Expr(AstKind::IntegerLiteralExpr, source_span),
      value_text_(std::move(value_text)) {}

const std::string &IntegerLiteralExpr::get_value_text() const noexcept {
    return value_text_;
}

FloatLiteralExpr::FloatLiteralExpr(std::string value_text,
                                   SourceSpan source_span)
    : Expr(AstKind::FloatLiteralExpr, source_span),
      value_text_(std::move(value_text)) {}

const std::string &FloatLiteralExpr::get_value_text() const noexcept {
    return value_text_;
}

CharLiteralExpr::CharLiteralExpr(std::string value_text, SourceSpan source_span)
    : Expr(AstKind::CharLiteralExpr, source_span),
      value_text_(std::move(value_text)) {}

const std::string &CharLiteralExpr::get_value_text() const noexcept {
    return value_text_;
}

StringLiteralExpr::StringLiteralExpr(std::string value_text,
                                     SourceSpan source_span)
    : Expr(AstKind::StringLiteralExpr, source_span),
      value_text_(std::move(value_text)) {}

const std::string &StringLiteralExpr::get_value_text() const noexcept {
    return value_text_;
}

IdentifierExpr::IdentifierExpr(std::string name, SourceSpan source_span)
    : Expr(AstKind::IdentifierExpr, source_span),
      name_(std::move(name)) {}

const std::string &IdentifierExpr::get_name() const noexcept { return name_; }

SizeofTypeExpr::SizeofTypeExpr(std::unique_ptr<TypeNode> target_type,
                               SourceSpan source_span)
    : Expr(AstKind::SizeofTypeExpr, source_span),
      target_type_(std::move(target_type)) {}

const TypeNode *SizeofTypeExpr::get_target_type() const noexcept {
    return target_type_.get();
}

BuiltinVaArgExpr::BuiltinVaArgExpr(std::unique_ptr<Expr> va_list_expr,
                                   std::unique_ptr<TypeNode> target_type,
                                   SourceSpan source_span)
    : Expr(AstKind::BuiltinVaArgExpr, source_span),
      va_list_expr_(std::move(va_list_expr)),
      target_type_(std::move(target_type)) {}

const Expr *BuiltinVaArgExpr::get_va_list_expr() const noexcept {
    return va_list_expr_.get();
}

const TypeNode *BuiltinVaArgExpr::get_target_type() const noexcept {
    return target_type_.get();
}

UnaryExpr::UnaryExpr(std::string operator_text, std::unique_ptr<Expr> operand,
                     SourceSpan source_span)
    : Expr(AstKind::UnaryExpr, source_span),
      operator_text_(std::move(operator_text)), operand_(std::move(operand)) {}

const std::string &UnaryExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *UnaryExpr::get_operand() const noexcept { return operand_.get(); }

PrefixExpr::PrefixExpr(std::string operator_text, std::unique_ptr<Expr> operand,
                       SourceSpan source_span)
    : Expr(AstKind::PrefixExpr, source_span),
      operator_text_(std::move(operator_text)), operand_(std::move(operand)) {}

const std::string &PrefixExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *PrefixExpr::get_operand() const noexcept { return operand_.get(); }

PostfixExpr::PostfixExpr(std::string operator_text,
                         std::unique_ptr<Expr> operand, SourceSpan source_span)
    : Expr(AstKind::PostfixExpr, source_span),
      operator_text_(std::move(operator_text)), operand_(std::move(operand)) {}

const std::string &PostfixExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *PostfixExpr::get_operand() const noexcept { return operand_.get(); }

BinaryExpr::BinaryExpr(std::string operator_text, std::unique_ptr<Expr> lhs,
                       std::unique_ptr<Expr> rhs, SourceSpan source_span)
    : Expr(AstKind::BinaryExpr, source_span),
      operator_text_(std::move(operator_text)), lhs_(std::move(lhs)),
      rhs_(std::move(rhs)) {}

const std::string &BinaryExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *BinaryExpr::get_lhs() const noexcept { return lhs_.get(); }

const Expr *BinaryExpr::get_rhs() const noexcept { return rhs_.get(); }

CastExpr::CastExpr(std::unique_ptr<TypeNode> target_type,
                   std::unique_ptr<Expr> operand, SourceSpan source_span)
    : Expr(AstKind::CastExpr, source_span),
      target_type_(std::move(target_type)), operand_(std::move(operand)) {}

const TypeNode *CastExpr::get_target_type() const noexcept {
    return target_type_.get();
}

const Expr *CastExpr::get_operand() const noexcept { return operand_.get(); }

ConditionalExpr::ConditionalExpr(std::unique_ptr<Expr> condition,
                                 std::unique_ptr<Expr> true_expr,
                                 std::unique_ptr<Expr> false_expr,
                                 SourceSpan source_span)
    : Expr(AstKind::ConditionalExpr, source_span),
      condition_(std::move(condition)), true_expr_(std::move(true_expr)),
      false_expr_(std::move(false_expr)) {}

const Expr *ConditionalExpr::get_condition() const noexcept {
    return condition_.get();
}

const Expr *ConditionalExpr::get_true_expr() const noexcept {
    return true_expr_.get();
}

const Expr *ConditionalExpr::get_false_expr() const noexcept {
    return false_expr_.get();
}

AssignExpr::AssignExpr(std::string operator_text, std::unique_ptr<Expr> target,
                       std::unique_ptr<Expr> value, SourceSpan source_span)
    : Expr(AstKind::AssignExpr, source_span),
      operator_text_(std::move(operator_text)), target_(std::move(target)),
      value_(std::move(value)) {}

const std::string &AssignExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *AssignExpr::get_target() const noexcept { return target_.get(); }

const Expr *AssignExpr::get_value() const noexcept { return value_.get(); }

CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   std::vector<std::unique_ptr<Expr>> arguments,
                   SourceSpan source_span)
    : Expr(AstKind::CallExpr, source_span),
      callee_(std::move(callee)), arguments_(std::move(arguments)) {}

const Expr *CallExpr::get_callee() const noexcept { return callee_.get(); }

const std::vector<std::unique_ptr<Expr>> &CallExpr::get_arguments() const
    noexcept {
    return arguments_;
}

IndexExpr::IndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> index,
                     SourceSpan source_span)
    : Expr(AstKind::IndexExpr, source_span),
      base_(std::move(base)), index_(std::move(index)) {}

const Expr *IndexExpr::get_base() const noexcept { return base_.get(); }

const Expr *IndexExpr::get_index() const noexcept { return index_.get(); }

MemberExpr::MemberExpr(std::string operator_text, std::unique_ptr<Expr> base,
                       std::string member_name, SourceSpan source_span)
    : Expr(AstKind::MemberExpr, source_span),
      operator_text_(std::move(operator_text)), base_(std::move(base)),
      member_name_(std::move(member_name)) {}

const std::string &MemberExpr::get_operator_text() const noexcept {
    return operator_text_;
}

const Expr *MemberExpr::get_base() const noexcept { return base_.get(); }

const std::string &MemberExpr::get_member_name() const noexcept {
    return member_name_;
}

StatementExpr::StatementExpr(std::unique_ptr<Stmt> body,
                             SourceSpan source_span)
    : Expr(AstKind::StatementExpr, source_span),
      body_(std::move(body)) {}

const Stmt *StatementExpr::get_body() const noexcept { return body_.get(); }

InitListExpr::InitListExpr(SourceSpan source_span)
    : Expr(AstKind::InitListExpr, source_span) {}

const std::vector<std::unique_ptr<Expr>> &InitListExpr::get_elements() const
    noexcept {
    return elements_;
}

void InitListExpr::add_element(std::unique_ptr<Expr> element) {
    if (element == nullptr) {
        return;
    }
    elements_.push_back(std::move(element));
}

UnknownExpr::UnknownExpr(std::string summary, SourceSpan source_span)
    : Expr(AstKind::UnknownExpr, source_span),
      summary_(std::move(summary)) {}

const std::string &UnknownExpr::get_summary() const noexcept {
    return summary_;
}

} // namespace sysycc
