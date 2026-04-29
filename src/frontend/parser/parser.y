%{
#include <cstdio>

#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser_runtime.hpp"

#ifndef YYINITDEPTH
#define YYINITDEPTH 4096
#endif

#ifndef YYMAXDEPTH
#define YYMAXDEPTH 1000000
#endif

typedef union YYSTYPE YYSTYPE;
int yylex(YYSTYPE *yylval_param, void *scanner);
void yyerror(void *scanner, const char *message);
void *yyget_extra(void *yyscanner);
char *yyget_text(void *yyscanner);
%}

%union {
    void *node;
}

%glr-parser
%pure-parser
%parse-param { void *scanner }
%lex-param { void *scanner }

%token <node> INVALID
%token <node> CONST VOLATILE EXTERN STATIC REGISTER ATTRIBUTE ASM EXTENSION INLINE RESTRICT NULLABILITY LONG SIGNED SHORT UNSIGNED INT CHAR VOID FLOAT DOUBLE FLOAT16 IF ELSE WHILE FOR DO SWITCH CASE DEFAULT BREAK CONTINUE GOTO RETURN STRUCT UNION ENUM TYPEDEF SIZEOF TYPEOF ALIGNAS TYPE_NAME
%token <node> IDENTIFIER ANNOTATION_IDENT INT_LITERAL FLOAT_LITERAL CHAR_LITERAL STRING_LITERAL
%token <node> PLUS MINUS MUL DIV MOD
%token <node> INC DEC BITAND BITOR BITXOR BITNOT SHL SHR ARROW
%token <node> ASSIGN ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN SHL_ASSIGN SHR_ASSIGN AND_ASSIGN XOR_ASSIGN OR_ASSIGN EQ NE LT LE GT GE
%token <node> NOT AND OR ELLIPSIS
%token <node> QUESTION SEMICOLON COMMA COLON DOT
%token <node> LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE

%type <node> comp_unit comp_unit_items comp_unit_item extension_prefix_seq
%type <node> decl const_decl const_init_declarator_list const_init_declarator
%type <node> var_decl init_declarator_list init_declarator declarator pointer pointer_level
%type <node> typedef_declarator_list typedef_declarator
%type <node> alignment_specifier_seq_opt alignment_specifier_seq alignment_specifier
%type <node> pointer_qualifier_seq_opt pointer_qualifier_seq pointer_qualifier
%type <node> type_qualifier_seq_opt type_qualifier_seq type_qualifier
%type <node> tag_identifier
%type <node> direct_declarator declarator_identifier expr_opt annotation_argument annotation_invocation_opt
%type <node> typedef_decl struct_decl union_decl enum_decl
%type <node> type_specifier object_type_specifier nonvoid_type_specifier basic_type struct_specifier union_specifier enum_specifier gnu_typeof_type_specifier
%type <node> struct_field_list_opt struct_field_list struct_field_decl struct_field_declarator_list struct_field_declarator field_bit_width_opt
%type <node> union_field_list_opt union_field_list union_field_decl union_field_declarator_list union_field_declarator
%type <node> enumerator_list_opt enumerator_list enumerator
%type <node> func_def func_decl function_declarator storage_specifier storage_specifier_opt parameter_list function_parameter_list_opt parameter_decl variadic_marker asm_label_opt asm_label string_literal_seq
%type <node> attribute_specifier_seq_opt attribute_specifier_seq attribute_specifier
%type <node> attribute_list_opt attribute_list attribute attribute_name attribute_argument_list_opt
%type <node> attribute_argument_list attribute_argument
%type <node> block block_scope_enter block_items block_item stmt
%type <node> expr const_expr cond argument_expr_list
%type <node> assignment_expr conditional_expr logical_or_expr logical_and_expr bit_or_expr
%type <node> bit_xor_expr bit_and_expr eq_expr rel_expr shift_expr add_expr
%type <node> mul_expr cast_expr unary_expr postfix_expr primary_expr statement_expr builtin_va_arg_expr gnu_builtin_types_compatible_expr
%type <node> cast_target_type sizeof_type_name sizeof_type_suffix_opt abstract_array_suffix_list
%type <node> init_val init_val_list designated_init_val designator designator_seq

%start comp_unit
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%nonassoc ANNOTATION_BARE
%nonassoc LPAREN

%%

comp_unit
    : comp_unit_items
      {
          void *root = sysycc::make_nonterminal_node("comp_unit", {$1});
          sysycc::set_parse_tree_root(root);
          $$ = root;
      }
    ;

comp_unit_items
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("comp_unit_items", {}); }
    | comp_unit_items comp_unit_item
      { $$ = sysycc::make_nonterminal_node("comp_unit_items", {$1, $2}); }
    ;

comp_unit_item
    : decl
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    | SEMICOLON
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {sysycc::make_nonterminal_node("empty_decl", {$1})}); }
    | func_decl %dprec 3
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    | func_def %dprec 3
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    | extension_prefix_seq decl
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1, $2}); }
    | extension_prefix_seq func_decl %dprec 3
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1, $2}); }
    | extension_prefix_seq func_def %dprec 3
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1, $2}); }
    ;

extension_prefix_seq
    : EXTENSION
      { $$ = sysycc::make_nonterminal_node("extension_prefix_seq", {$1}); }
    | extension_prefix_seq EXTENSION
      { $$ = sysycc::make_nonterminal_node("extension_prefix_seq", {$1, $2}); }
    ;

storage_specifier
    : EXTERN
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1}); }
    | STATIC
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1}); }
    | REGISTER
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1}); }
    | INLINE
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1}); }
    | EXTERN INLINE
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1, $2}); }
    | INLINE EXTERN
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1, $2}); }
    | STATIC INLINE
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1, $2}); }
    | INLINE STATIC
      { $$ = sysycc::make_nonterminal_node("storage_specifier", {$1, $2}); }
    ;

storage_specifier_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {}); }
    | storage_specifier
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {$1}); }
    ;

decl
    : const_decl %dprec 2
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | var_decl %dprec 1
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | attribute_specifier_seq var_decl %dprec 1
      { $$ = sysycc::make_nonterminal_node("decl", {$1, $2}); }
    | typedef_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | struct_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | union_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | enum_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    ;

type_qualifier_seq_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("type_qualifier_seq_opt", {}); }
    | type_qualifier_seq
      { $$ = sysycc::make_nonterminal_node("type_qualifier_seq_opt", {$1}); }
    ;

type_qualifier_seq
    : type_qualifier
      { $$ = sysycc::make_nonterminal_node("type_qualifier_seq", {$1}); }
    | type_qualifier_seq type_qualifier
      { $$ = sysycc::make_nonterminal_node("type_qualifier_seq", {$1, $2}); }
    ;

type_qualifier
    : CONST
      { $$ = sysycc::make_nonterminal_node("type_qualifier", {$1}); }
    | VOLATILE
      { $$ = sysycc::make_nonterminal_node("type_qualifier", {$1}); }
    ;

const_decl
    : alignment_specifier_seq_opt CONST type_qualifier_seq_opt basic_type alignment_specifier_seq_opt type_qualifier_seq_opt const_init_declarator_list SEMICOLON %dprec 2
      {
          void *type_specifier =
              sysycc::make_nonterminal_node("type_specifier", {$4});
          $$ = sysycc::make_nonterminal_node("const_decl",
                                             {$1, $2, $3, type_specifier, $5, $6, $7, $8});
      }
    | alignment_specifier_seq_opt VOLATILE CONST type_qualifier_seq_opt basic_type alignment_specifier_seq_opt type_qualifier_seq_opt const_init_declarator_list SEMICOLON %dprec 2
      {
          void *type_specifier =
              sysycc::make_nonterminal_node("type_specifier", {$5});
          $$ = sysycc::make_nonterminal_node("const_decl",
                                             {$1, $2, $3, $4, type_specifier, $6, $7, $8, $9});
      }
    ;

const_init_declarator_list
    : const_init_declarator
      { $$ = sysycc::make_nonterminal_node("const_init_declarator_list", {$1}); }
    | const_init_declarator_list COMMA const_init_declarator
      { $$ = sysycc::make_nonterminal_node("const_init_declarator_list", {$1, $2, $3}); }
    ;

const_init_declarator
    : direct_declarator ASSIGN init_val
      {
          void *declarator = sysycc::make_nonterminal_node("declarator", {$1});
          $$ = sysycc::make_nonterminal_node("const_init_declarator",
                                             {declarator, $2, $3});
      }
    ;

var_decl
    : storage_specifier_opt alignment_specifier_seq_opt type_qualifier_seq_opt object_type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt init_declarator_list SEMICOLON %dprec 1
      {
          $$ = sysycc::make_nonterminal_node("var_decl",
                                             {$1, $2, $3, $4, $5, $6, $7, $8});
          sysycc::hide_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($7));
      }
    | storage_specifier_opt alignment_specifier_seq_opt type_qualifier_seq_opt struct_specifier alignment_specifier_seq_opt type_qualifier_seq_opt init_declarator_list SEMICOLON %dprec 1
      {
          void *type_specifier =
              sysycc::make_nonterminal_node("type_specifier", {$4});
          $$ = sysycc::make_nonterminal_node("var_decl",
                                             {$1, $2, $3, type_specifier, $5, $6, $7, $8});
          sysycc::hide_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($7));
      }
    | storage_specifier_opt alignment_specifier_seq_opt type_qualifier_seq_opt union_specifier alignment_specifier_seq_opt type_qualifier_seq_opt init_declarator_list SEMICOLON %dprec 1
      {
          void *type_specifier =
              sysycc::make_nonterminal_node("type_specifier", {$4});
          $$ = sysycc::make_nonterminal_node("var_decl",
                                             {$1, $2, $3, type_specifier, $5, $6, $7, $8});
          sysycc::hide_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($7));
      }
    | storage_specifier_opt alignment_specifier_seq_opt type_qualifier_seq_opt enum_specifier alignment_specifier_seq_opt type_qualifier_seq_opt init_declarator_list SEMICOLON %dprec 1
      {
          void *type_specifier =
              sysycc::make_nonterminal_node("type_specifier", {$4});
          $$ = sysycc::make_nonterminal_node("var_decl",
                                             {$1, $2, $3, type_specifier, $5, $6, $7, $8});
          sysycc::hide_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($7));
      }
    ;

alignment_specifier_seq_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("alignment_specifier_seq_opt", {}); }
    | alignment_specifier_seq
      { $$ = sysycc::make_nonterminal_node("alignment_specifier_seq_opt", {$1}); }
    ;

alignment_specifier_seq
    : alignment_specifier
      { $$ = sysycc::make_nonterminal_node("alignment_specifier_seq", {$1}); }
    | alignment_specifier_seq alignment_specifier
      { $$ = sysycc::make_nonterminal_node("alignment_specifier_seq", {$1, $2}); }
    ;

alignment_specifier
    : ALIGNAS LPAREN const_expr RPAREN
      { $$ = sysycc::make_nonterminal_node("alignment_specifier", {$1, $2, $3, $4}); }
    ;

typedef_decl
    : TYPEDEF type_specifier type_qualifier_seq_opt typedef_declarator_list SEMICOLON
      {
          $$ = sysycc::make_nonterminal_node("typedef_decl", {$1, $2, $3, $4, $5});
          sysycc::register_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($4));
      }
    | TYPEDEF type_qualifier_seq type_specifier type_qualifier_seq_opt typedef_declarator_list SEMICOLON
      {
          $$ = sysycc::make_nonterminal_node("typedef_decl", {$1, $2, $3, $4, $5, $6});
          sysycc::register_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($5));
      }
    | TYPEDEF type_specifier type_qualifier_seq_opt function_declarator attribute_specifier_seq_opt SEMICOLON
      {
          $$ = sysycc::make_nonterminal_node("typedef_decl", {$1, $2, $3, $4, $5, $6});
          sysycc::register_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($4));
      }
    | TYPEDEF type_qualifier_seq type_specifier type_qualifier_seq_opt function_declarator attribute_specifier_seq_opt SEMICOLON
      {
          $$ = sysycc::make_nonterminal_node("typedef_decl", {$1, $2, $3, $4, $5, $6, $7});
          sysycc::register_typedef_names_from_declarator_list(
              static_cast<const sysycc::ParseTreeNode *>($5));
      }
    ;

typedef_declarator_list
    : typedef_declarator
      { $$ = sysycc::make_nonterminal_node("typedef_declarator_list", {$1}); }
    | typedef_declarator_list COMMA typedef_declarator
      { $$ = sysycc::make_nonterminal_node("typedef_declarator_list", {$1, $2, $3}); }
    ;

typedef_declarator
    : declarator
      {
          void *attribute_opt =
              sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {});
          $$ = sysycc::make_nonterminal_node("typedef_declarator", {$1, attribute_opt});
      }
    | declarator attribute_specifier_seq
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node("typedef_declarator", {$1, attribute_opt});
      }
    ;

tag_identifier
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("tag_identifier", {$1}); }
    | TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("tag_identifier", {$1}); }
    ;

struct_decl
    : STRUCT tag_identifier SEMICOLON
      {
          void *specifier =
              sysycc::make_nonterminal_node("struct_specifier", {$1, $2});
          void *suffix_attributes =
              sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {});
          $$ = sysycc::make_nonterminal_node("struct_decl",
                                             {specifier, suffix_attributes, $3});
      }
    | STRUCT tag_identifier LBRACE struct_field_list_opt RBRACE attribute_specifier_seq_opt SEMICOLON
      {
          void *specifier = sysycc::make_nonterminal_node(
              "struct_specifier", {$1, $2, $3, $4, $5});
          $$ = sysycc::make_nonterminal_node("struct_decl", {specifier, $6, $7});
      }
    | STRUCT LBRACE struct_field_list_opt RBRACE attribute_specifier_seq_opt SEMICOLON
      {
          void *specifier =
              sysycc::make_nonterminal_node("struct_specifier", {$1, $2, $3, $4});
          $$ = sysycc::make_nonterminal_node("struct_decl", {specifier, $5, $6});
      }
    ;

union_decl
    : UNION tag_identifier SEMICOLON
      {
          void *specifier =
              sysycc::make_nonterminal_node("union_specifier", {$1, $2});
          void *suffix_attributes =
              sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {});
          $$ = sysycc::make_nonterminal_node("union_decl",
                                             {specifier, suffix_attributes, $3});
      }
    | UNION tag_identifier LBRACE union_field_list_opt RBRACE attribute_specifier_seq_opt SEMICOLON
      {
          void *specifier = sysycc::make_nonterminal_node(
              "union_specifier", {$1, $2, $3, $4, $5});
          $$ = sysycc::make_nonterminal_node("union_decl", {specifier, $6, $7});
      }
    | UNION LBRACE union_field_list_opt RBRACE attribute_specifier_seq_opt SEMICOLON
      {
          void *specifier =
              sysycc::make_nonterminal_node("union_specifier", {$1, $2, $3, $4});
          $$ = sysycc::make_nonterminal_node("union_decl", {specifier, $5, $6});
      }
    ;

enum_decl
    : ENUM tag_identifier LBRACE enumerator_list_opt RBRACE SEMICOLON
      {
          void *specifier = sysycc::make_nonterminal_node(
              "enum_specifier", {$1, $2, $3, $4, $5});
          $$ = sysycc::make_nonterminal_node("enum_decl", {specifier, $6});
      }
    | ENUM LBRACE enumerator_list_opt RBRACE SEMICOLON
      {
          void *specifier =
              sysycc::make_nonterminal_node("enum_specifier", {$1, $2, $3, $4});
          $$ = sysycc::make_nonterminal_node("enum_decl", {specifier, $5});
      }
    ;

type_specifier
    : basic_type
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | gnu_typeof_type_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | struct_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | union_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | enum_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    ;

object_type_specifier
    : basic_type
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | gnu_typeof_type_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    ;

nonvoid_type_specifier
    : LONG DOUBLE
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | LONG
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | LONG LONG
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | LONG LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED LONG
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED LONG LONG
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED LONG LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED CHAR
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SHORT
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SHORT INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED SHORT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | SIGNED SHORT INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED CHAR
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED SHORT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED SHORT INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED LONG
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED LONG LONG
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | UNSIGNED LONG LONG INT
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | INT
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | CHAR
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | FLOAT
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | DOUBLE
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | FLOAT16
      {
          void *basic_type = sysycc::make_nonterminal_node("basic_type", {$1});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | struct_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | union_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | enum_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    ;

basic_type
    : LONG DOUBLE
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | SIGNED INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | SIGNED LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | SIGNED LONG LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | SIGNED LONG LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4}); }
    | SIGNED LONG LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4}); }
    | SIGNED CHAR
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SHORT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | SHORT INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SHORT TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED SHORT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | SIGNED SHORT INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | SIGNED SHORT TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | LONG LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | LONG LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | LONG LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | UNSIGNED CHAR
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | UNSIGNED type_qualifier_seq CHAR
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED SHORT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | UNSIGNED SHORT INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED SHORT TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | UNSIGNED TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | UNSIGNED LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | UNSIGNED LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED LONG LONG
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3}); }
    | UNSIGNED LONG LONG INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4}); }
    | UNSIGNED LONG LONG TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2, $3, $4}); }
    | INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | CHAR
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | VOID
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | FLOAT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | DOUBLE
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | FLOAT16
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    ;

gnu_typeof_type_specifier
    : TYPEOF LPAREN expr RPAREN %dprec 2
      { $$ = sysycc::make_nonterminal_node("gnu_typeof_type_specifier", {$1, $2, $3, $4}); }
    ;

struct_specifier
    : STRUCT tag_identifier
      { $$ = sysycc::make_nonterminal_node("struct_specifier", {$1, $2}); }
    | STRUCT tag_identifier LBRACE struct_field_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("struct_specifier", {$1, $2, $3, $4, $5}); }
    | STRUCT LBRACE struct_field_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("struct_specifier", {$1, $2, $3, $4}); }
    ;

struct_field_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("struct_field_list_opt", {}); }
    | struct_field_list
      { $$ = sysycc::make_nonterminal_node("struct_field_list_opt", {$1}); }
    ;

struct_field_list
    : struct_field_decl
      { $$ = sysycc::make_nonterminal_node("struct_field_list", {$1}); }
    | struct_field_list struct_field_decl
      { $$ = sysycc::make_nonterminal_node("struct_field_list", {$1, $2}); }
    ;

struct_field_decl
    : alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt struct_field_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_field_decl", {$1, $2, $3, $4, $5, $6, $7}); }
    | alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_field_decl", {$1, $2, $3, $4, $5, $6}); }
    | extension_prefix_seq alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt struct_field_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_field_decl", {$1, $2, $3, $4, $5, $6, $7, $8}); }
    | extension_prefix_seq alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_field_decl", {$1, $2, $3, $4, $5, $6, $7}); }
    ;

struct_field_declarator_list
    : struct_field_declarator
      { $$ = sysycc::make_nonterminal_node("struct_field_declarator_list", {$1}); }
    | struct_field_declarator_list COMMA struct_field_declarator
      { $$ = sysycc::make_nonterminal_node("struct_field_declarator_list", {$1, $2, $3}); }
    ;

struct_field_declarator
    : declarator field_bit_width_opt
      { $$ = sysycc::make_nonterminal_node("struct_field_declarator", {$1, $2}); }
    | declarator attribute_specifier_seq field_bit_width_opt
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node(
              "struct_field_declarator", {$1, attribute_opt, $3});
      }
    | COLON const_expr
      { $$ = sysycc::make_nonterminal_node("struct_field_declarator", {$1, $2}); }
    ;

field_bit_width_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("field_bit_width_opt", {}); }
    | COLON const_expr
      { $$ = sysycc::make_nonterminal_node("field_bit_width_opt", {$1, $2}); }
    ;

union_specifier
    : UNION tag_identifier
      { $$ = sysycc::make_nonterminal_node("union_specifier", {$1, $2}); }
    | UNION tag_identifier LBRACE union_field_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("union_specifier", {$1, $2, $3, $4, $5}); }
    | UNION LBRACE union_field_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("union_specifier", {$1, $2, $3, $4}); }
    ;

union_field_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("union_field_list_opt", {}); }
    | union_field_list
      { $$ = sysycc::make_nonterminal_node("union_field_list_opt", {$1}); }
    ;

union_field_list
    : union_field_decl
      { $$ = sysycc::make_nonterminal_node("union_field_list", {$1}); }
    | union_field_list union_field_decl
      { $$ = sysycc::make_nonterminal_node("union_field_list", {$1, $2}); }
    ;

union_field_decl
    : alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt union_field_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("union_field_decl", {$1, $2, $3, $4, $5, $6, $7}); }
    | alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("union_field_decl", {$1, $2, $3, $4, $5, $6}); }
    | extension_prefix_seq alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt union_field_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("union_field_decl", {$1, $2, $3, $4, $5, $6, $7, $8}); }
    | extension_prefix_seq alignment_specifier_seq_opt type_qualifier_seq_opt type_specifier alignment_specifier_seq_opt type_qualifier_seq_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("union_field_decl", {$1, $2, $3, $4, $5, $6, $7}); }
    ;

union_field_declarator_list
    : union_field_declarator
      { $$ = sysycc::make_nonterminal_node("union_field_declarator_list", {$1}); }
    | union_field_declarator_list COMMA union_field_declarator
      { $$ = sysycc::make_nonterminal_node("union_field_declarator_list", {$1, $2, $3}); }
    ;

union_field_declarator
    : declarator field_bit_width_opt
      { $$ = sysycc::make_nonterminal_node("union_field_declarator", {$1, $2}); }
    | declarator attribute_specifier_seq field_bit_width_opt
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node(
              "union_field_declarator", {$1, attribute_opt, $3});
      }
    | COLON const_expr
      { $$ = sysycc::make_nonterminal_node("union_field_declarator", {$1, $2}); }
    ;

enum_specifier
    : ENUM tag_identifier
      { $$ = sysycc::make_nonterminal_node("enum_specifier", {$1, $2}); }
    | ENUM tag_identifier LBRACE enumerator_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("enum_specifier", {$1, $2, $3, $4, $5}); }
    | ENUM LBRACE enumerator_list_opt RBRACE
      { $$ = sysycc::make_nonterminal_node("enum_specifier", {$1, $2, $3, $4}); }
    ;

enumerator_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("enumerator_list_opt", {}); }
    | enumerator_list
      { $$ = sysycc::make_nonterminal_node("enumerator_list_opt", {$1}); }
    ;

enumerator_list
    : enumerator
      { $$ = sysycc::make_nonterminal_node("enumerator_list", {$1}); }
    | enumerator_list COMMA enumerator
      { $$ = sysycc::make_nonterminal_node("enumerator_list", {$1, $2, $3}); }
    | enumerator_list COMMA
      { $$ = sysycc::make_nonterminal_node("enumerator_list", {$1, $2}); }
    ;

enumerator
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("enumerator", {$1}); }
    | IDENTIFIER ASSIGN const_expr
      { $$ = sysycc::make_nonterminal_node("enumerator", {$1, $2, $3}); }
    ;

init_declarator_list
    : init_declarator
      { $$ = sysycc::make_nonterminal_node("init_declarator_list", {$1}); }
    | init_declarator_list COMMA init_declarator
      { $$ = sysycc::make_nonterminal_node("init_declarator_list", {$1, $2, $3}); }
    ;

init_declarator
    : declarator asm_label_opt
      { $$ = sysycc::make_nonterminal_node("init_declarator", {$1, $2}); }
    | declarator asm_label_opt attribute_specifier_seq
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$3});
          $$ = sysycc::make_nonterminal_node("init_declarator", {$1, $2, attribute_opt});
      }
    | declarator asm_label_opt ASSIGN init_val
      { $$ = sysycc::make_nonterminal_node("init_declarator", {$1, $2, $3, $4}); }
    | declarator asm_label_opt attribute_specifier_seq ASSIGN init_val
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$3});
          $$ = sysycc::make_nonterminal_node("init_declarator", {$1, $2, attribute_opt, $4, $5});
      }
    ;

declarator
    : direct_declarator
      { $$ = sysycc::make_nonterminal_node("declarator", {$1}); }
    | pointer direct_declarator
      { $$ = sysycc::make_nonterminal_node("declarator", {$1, $2}); }
    ;

pointer
    : pointer_level
      { $$ = sysycc::make_nonterminal_node("pointer", {$1}); }
    | pointer_level pointer
      { $$ = sysycc::make_nonterminal_node("pointer", {$1, $2}); }
    ;

pointer_level
    : MUL pointer_qualifier_seq_opt
      { $$ = sysycc::make_nonterminal_node("pointer_level", {$1, $2}); }
    ;

pointer_qualifier_seq_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier_seq_opt", {}); }
    | pointer_qualifier_seq
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier_seq_opt", {$1}); }
    ;

pointer_qualifier_seq
    : pointer_qualifier
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier_seq", {$1}); }
    | pointer_qualifier_seq pointer_qualifier
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier_seq", {$1, $2}); }
    ;

pointer_qualifier
    : CONST
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier", {$1}); }
    | VOLATILE
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier", {$1}); }
    | RESTRICT
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier", {$1}); }
    | NULLABILITY
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier", {$1}); }
    | ANNOTATION_IDENT annotation_invocation_opt
      { $$ = sysycc::make_nonterminal_node("pointer_qualifier", {$1, $2}); }
    ;

annotation_invocation_opt
    : /* empty */
      %prec ANNOTATION_BARE
      { $$ = sysycc::make_nonterminal_node("annotation_invocation_opt", {}); }
    | LPAREN annotation_argument RPAREN
      { $$ = sysycc::make_nonterminal_node("annotation_invocation_opt", {$1, $2, $3}); }
    ;

annotation_argument
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("annotation_argument", {$1}); }
    | INT_LITERAL
      { $$ = sysycc::make_nonterminal_node("annotation_argument", {$1}); }
    ;

direct_declarator
    : declarator_identifier
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1}); }
    | LPAREN declarator RPAREN
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3}); }
    | LPAREN pointer direct_declarator RPAREN LPAREN function_parameter_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4, $5, $6, $7}); }
    | direct_declarator LBRACKET expr_opt RBRACKET
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4}); }
    | direct_declarator LBRACKET pointer_qualifier_seq expr_opt RBRACKET
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4, $5}); }
    | direct_declarator LBRACKET STATIC pointer_qualifier_seq_opt expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4, $5, $6}); }
    | direct_declarator LBRACKET pointer_qualifier_seq STATIC expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4, $5, $6}); }
    ;

declarator_identifier
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("declarator_identifier", {$1}); }
    | TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("declarator_identifier", {$1}); }
    ;

func_def
    : storage_specifier_opt type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator block
      {
          void *attribute_opt =
              sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {$1, attribute_opt, $2, $3, $4, $5, $6});
      }
    | attribute_specifier_seq storage_specifier_opt type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator block
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {$2, attribute_opt, $3, $4, $5, $6, $7});
      }
    | attribute_specifier_seq storage_specifier_opt type_qualifier_seq_opt type_specifier attribute_specifier_seq type_qualifier_seq_opt function_declarator block
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1, $5});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {$2, attribute_opt, $3, $4, $6, $7, $8});
      }
    | attribute_specifier_seq storage_specifier attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator block
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$2});
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1, $3});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {storage_opt, attribute_opt, $4, $5, $6, $7, $8});
      }
    | storage_specifier_opt type_qualifier_seq_opt type_specifier attribute_specifier_seq type_qualifier_seq_opt function_declarator block
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$4});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {$1, attribute_opt, $2, $3, $5, $6, $7});
      }
    | storage_specifier attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator block
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node("func_def",
                                             {storage_opt, attribute_opt, $3, $4, $5, $6, $7});
      }
    | storage_specifier attribute_specifier_seq storage_specifier type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator block
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1, $3});
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node(
              "func_def",
              {storage_opt, attribute_opt, $4, $5, $6, $7, $8});
      }
    ;

func_decl
    : storage_specifier_opt type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *leading_attribute_opt =
              sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {$1, leading_attribute_opt, $2, $3, $4, $5, $6, $7, $8});
      }
    | attribute_specifier_seq storage_specifier_opt type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {$2, leading_attribute_opt, $3, $4, $5, $6, $7, $8, $9});
      }
    | attribute_specifier_seq storage_specifier_opt type_qualifier_seq_opt type_specifier attribute_specifier_seq type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1, $5});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {$2, leading_attribute_opt, $3, $4, $6, $7, $8, $9, $10});
      }
    | attribute_specifier_seq storage_specifier attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$2});
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1, $3});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {storage_opt, leading_attribute_opt, $4, $5, $6, $7, $8, $9, $10});
      }
    | storage_specifier_opt type_qualifier_seq_opt type_specifier attribute_specifier_seq type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$4});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {$1, leading_attribute_opt, $2, $3, $5, $6, $7, $8, $9});
      }
    | storage_specifier attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {storage_opt, leading_attribute_opt, $3, $4, $5, $6, $7, $8, $9});
      }
    | storage_specifier attribute_specifier_seq storage_specifier type_qualifier_seq_opt type_specifier type_qualifier_seq_opt function_declarator asm_label_opt attribute_specifier_seq_opt SEMICOLON %dprec 2
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1, $3});
          void *leading_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$2});
          $$ = sysycc::make_nonterminal_node(
              "func_decl",
              {storage_opt, leading_attribute_opt, $4, $5, $6, $7, $8, $9, $10});
      }
    ;

function_declarator
    : declarator_identifier LPAREN function_parameter_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("function_declarator", {$1, $2, $3, $4}); }
    | LPAREN declarator_identifier RPAREN LPAREN function_parameter_list_opt RPAREN %dprec 2
      { $$ = sysycc::make_nonterminal_node("function_declarator", {$1, $2, $3, $4, $5, $6}); }
    | pointer LPAREN declarator_identifier RPAREN LPAREN function_parameter_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("function_declarator", {$1, $2, $3, $4, $5, $6, $7}); }
    | LPAREN pointer function_declarator RPAREN LPAREN function_parameter_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("function_declarator", {$1, $2, $3, $4, $5, $6, $7}); }
    | pointer declarator_identifier LPAREN function_parameter_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("function_declarator", {$1, $2, $3, $4, $5}); }
    ;

asm_label_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("asm_label_opt", {}); }
    | asm_label
      { $$ = sysycc::make_nonterminal_node("asm_label_opt", {$1}); }
    ;

asm_label
    : ASM LPAREN string_literal_seq RPAREN
      { $$ = sysycc::make_nonterminal_node("asm_label", {$1, $2, $3, $4}); }
    ;

string_literal_seq
    : STRING_LITERAL
      { $$ = sysycc::make_nonterminal_node("string_literal_seq", {$1}); }
    | string_literal_seq STRING_LITERAL
      { $$ = sysycc::make_nonterminal_node("string_literal_seq", {$1, $2}); }
    ;

attribute_specifier_seq_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {}); }
    | attribute_specifier_seq
      { $$ = sysycc::make_nonterminal_node("attribute_specifier_seq_opt", {$1}); }
    ;

attribute_specifier_seq
    : attribute_specifier
      { $$ = sysycc::make_nonterminal_node("attribute_specifier_seq", {$1}); }
    | attribute_specifier_seq attribute_specifier
      { $$ = sysycc::make_nonterminal_node("attribute_specifier_seq", {$1, $2}); }
    ;

attribute_specifier
    : ATTRIBUTE LPAREN LPAREN attribute_list_opt RPAREN RPAREN
      { $$ = sysycc::make_nonterminal_node("attribute_specifier", {$1, $2, $3, $4, $5, $6}); }
    ;

attribute_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("attribute_list_opt", {}); }
    | attribute_list
      { $$ = sysycc::make_nonterminal_node("attribute_list_opt", {$1}); }
    ;

attribute_list
    : attribute
      { $$ = sysycc::make_nonterminal_node("attribute_list", {$1}); }
    | attribute_list COMMA attribute
      { $$ = sysycc::make_nonterminal_node("attribute_list", {$1, $2, $3}); }
    ;

attribute
    : attribute_name
      { $$ = sysycc::make_nonterminal_node("attribute", {$1}); }
    | attribute_name LPAREN attribute_argument_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("attribute", {$1, $2, $3, $4}); }
    ;

attribute_name
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("attribute_name", {$1}); }
    | CONST
      { $$ = sysycc::make_nonterminal_node("attribute_name", {$1}); }
    ;

attribute_argument_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("attribute_argument_list_opt", {}); }
    | attribute_argument_list
      { $$ = sysycc::make_nonterminal_node("attribute_argument_list_opt", {$1}); }
    ;

attribute_argument_list
    : attribute_argument
      { $$ = sysycc::make_nonterminal_node("attribute_argument_list", {$1}); }
    | attribute_argument_list COMMA attribute_argument
      { $$ = sysycc::make_nonterminal_node("attribute_argument_list", {$1, $2, $3}); }
    ;

attribute_argument
    : assignment_expr
      { $$ = sysycc::make_nonterminal_node("attribute_argument", {$1}); }
    | attribute_name LPAREN type_specifier RPAREN
      { $$ = sysycc::make_nonterminal_node("attribute_argument", {$1, $2, $3, $4}); }
    ;

function_parameter_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("function_parameter_list_opt", {}); }
    | VOID
      { $$ = sysycc::make_nonterminal_node("function_parameter_list_opt", {$1}); }
    | parameter_list
      { $$ = sysycc::make_nonterminal_node("function_parameter_list_opt", {$1}); }
    ;

parameter_list
    : parameter_decl
      { $$ = sysycc::make_nonterminal_node("parameter_list", {$1}); }
    | parameter_list COMMA parameter_decl
      { $$ = sysycc::make_nonterminal_node("parameter_list", {$1, $2, $3}); }
    | parameter_list COMMA variadic_marker
      { $$ = sysycc::make_nonterminal_node("parameter_list", {$1, $2, $3}); }
    ;

variadic_marker
    : ELLIPSIS
      { $$ = sysycc::make_nonterminal_node("variadic_marker", {$1}); }
    ;

parameter_decl
    : type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer LPAREN pointer RPAREN LPAREN function_parameter_list_opt RPAREN
      {
          void *direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator", {$5, $6, $7, $8, $9, $10});
          void *declarator = sysycc::make_nonterminal_node(
              "declarator", {$4, direct_declarator});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {$1, $2, $3, declarator});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt LPAREN pointer RPAREN LPAREN function_parameter_list_opt RPAREN
      {
          void *direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator", {$4, $5, $6, $7, $8, $9});
          void *declarator = sysycc::make_nonterminal_node("declarator",
                                                           {direct_declarator});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {$1, $2, $3, declarator});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt abstract_array_suffix_list
      {
          void *pointer_level =
              sysycc::make_nonterminal_node("pointer_level", {});
          void *pointer =
              sysycc::make_nonterminal_node("pointer", {pointer_level});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {$1, $2, $3, pointer, $4});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt declarator %dprec 2
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3, $4}); }
    | storage_specifier type_qualifier_seq_opt type_specifier type_qualifier_seq_opt declarator %dprec 2
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {storage_opt, $2, $3, $4, $5});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt declarator attribute_specifier_seq %dprec 2
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$5});
          $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3, $4, attribute_opt});
      }
    | attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt declarator %dprec 2
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl", {attribute_opt, $2, $3, $4, $5});
      }
    | attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt declarator attribute_specifier_seq %dprec 2
      {
          void *prefix_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1});
          void *suffix_attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$6});
          $$ = sysycc::make_nonterminal_node("parameter_decl", {prefix_attribute_opt, $2, $3, $4, $5, suffix_attribute_opt});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer %dprec 1
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3, $4}); }
    | storage_specifier type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer %dprec 1
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {storage_opt, $2, $3, $4, $5});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer attribute_specifier_seq %dprec 1
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$5});
          $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3, $4, attribute_opt});
      }
    | attribute_specifier_seq type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer %dprec 1
      {
          void *attribute_opt = sysycc::make_nonterminal_node(
              "attribute_specifier_seq_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl", {attribute_opt, $2, $3, $4, $5});
      }
    | type_qualifier_seq_opt TYPE_NAME type_qualifier_seq_opt
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3}); }
    | storage_specifier type_qualifier_seq_opt TYPE_NAME type_qualifier_seq_opt
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {storage_opt, $2, $3, $4});
      }
    | type_qualifier_seq_opt nonvoid_type_specifier type_qualifier_seq_opt
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2, $3}); }
    | storage_specifier type_qualifier_seq_opt nonvoid_type_specifier type_qualifier_seq_opt
      {
          void *storage_opt =
              sysycc::make_nonterminal_node("storage_specifier_opt", {$1});
          $$ = sysycc::make_nonterminal_node("parameter_decl",
                                             {storage_opt, $2, $3, $4});
      }
    ;

block
    : LBRACE block_scope_enter block_items RBRACE
      {
          sysycc::pop_typedef_shadow_scope();
          $$ = sysycc::make_nonterminal_node("block", {$1, $3, $4});
      }
    ;

block_scope_enter
    : /* empty */
      {
          sysycc::push_typedef_shadow_scope();
          $$ = sysycc::make_nonterminal_node("block_scope_enter", {});
      }
    ;

block_items
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("block_items", {}); }
    | block_items block_item
      { $$ = sysycc::make_nonterminal_node("block_items", {$1, $2}); }
    ;

block_item
    : decl
      { $$ = sysycc::make_nonterminal_node("block_item", {$1}); }
    | stmt
      { $$ = sysycc::make_nonterminal_node("block_item", {$1}); }
    ;

stmt
    : expr_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | block
      { $$ = sysycc::make_nonterminal_node("stmt", {$1}); }
    | IF LPAREN cond RPAREN stmt %prec LOWER_THAN_ELSE
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | IF LPAREN cond RPAREN stmt ELSE stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5, $6, $7}); }
    | WHILE LPAREN cond RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | DO stmt WHILE LPAREN expr RPAREN SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5, $6, $7}); }
    | FOR LPAREN expr_opt SEMICOLON expr_opt SEMICOLON expr_opt RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5, $6, $7, $8, $9}); }
    | FOR LPAREN var_decl expr_opt SEMICOLON expr_opt RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5, $6, $7, $8}); }
    | SWITCH LPAREN expr RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | CASE const_expr COLON stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4}); }
    | DEFAULT COLON stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    | IDENTIFIER COLON stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    | BREAK SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | CONTINUE SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | GOTO IDENTIFIER SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    | GOTO MUL expr SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4}); }
    | RETURN expr_opt SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    ;

expr_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("expr_opt", {}); }
    | expr
      { $$ = sysycc::make_nonterminal_node("expr_opt", {$1}); }
    ;

expr
    : assignment_expr
      { $$ = sysycc::make_nonterminal_node("expr", {$1}); }
    | expr COMMA assignment_expr
      { $$ = sysycc::make_nonterminal_node("expr", {$1, $2, $3}); }
    ;

const_expr
    : assignment_expr
      { $$ = sysycc::make_nonterminal_node("const_expr", {$1}); }
    ;

cond
    : expr
      { $$ = sysycc::make_nonterminal_node("cond", {$1}); }
    ;

assignment_expr
    : conditional_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1}); }
    | unary_expr ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr ADD_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr SUB_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr MUL_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr DIV_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr MOD_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr SHL_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr SHR_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr AND_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr XOR_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    | unary_expr OR_ASSIGN assignment_expr
      { $$ = sysycc::make_nonterminal_node("assignment_expr", {$1, $2, $3}); }
    ;

conditional_expr
    : logical_or_expr
      { $$ = sysycc::make_nonterminal_node("conditional_expr", {$1}); }
    | logical_or_expr QUESTION expr COLON conditional_expr
      { $$ = sysycc::make_nonterminal_node("conditional_expr", {$1, $2, $3, $4, $5}); }
    ;

argument_expr_list
    : assignment_expr
      { $$ = sysycc::make_nonterminal_node("argument_expr_list", {$1}); }
    | argument_expr_list COMMA assignment_expr
      { $$ = sysycc::make_nonterminal_node("argument_expr_list", {$1, $2, $3}); }
    ;

logical_or_expr
    : logical_and_expr
      { $$ = sysycc::make_nonterminal_node("logical_or_expr", {$1}); }
    | logical_or_expr OR logical_and_expr
      { $$ = sysycc::make_nonterminal_node("logical_or_expr", {$1, $2, $3}); }
    ;

logical_and_expr
    : bit_or_expr
      { $$ = sysycc::make_nonterminal_node("logical_and_expr", {$1}); }
    | logical_and_expr AND bit_or_expr
      { $$ = sysycc::make_nonterminal_node("logical_and_expr", {$1, $2, $3}); }
    ;

bit_or_expr
    : bit_xor_expr
      { $$ = sysycc::make_nonterminal_node("bit_or_expr", {$1}); }
    | bit_or_expr BITOR bit_xor_expr
      { $$ = sysycc::make_nonterminal_node("bit_or_expr", {$1, $2, $3}); }
    ;

bit_xor_expr
    : bit_and_expr
      { $$ = sysycc::make_nonterminal_node("bit_xor_expr", {$1}); }
    | bit_xor_expr BITXOR bit_and_expr
      { $$ = sysycc::make_nonterminal_node("bit_xor_expr", {$1, $2, $3}); }
    ;

bit_and_expr
    : eq_expr
      { $$ = sysycc::make_nonterminal_node("bit_and_expr", {$1}); }
    | bit_and_expr BITAND eq_expr
      { $$ = sysycc::make_nonterminal_node("bit_and_expr", {$1, $2, $3}); }
    ;

eq_expr
    : rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1}); }
    | eq_expr EQ rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1, $2, $3}); }
    | eq_expr NE rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1, $2, $3}); }
    ;

rel_expr
    : shift_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1}); }
    | rel_expr LT shift_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr LE shift_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr GT shift_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr GE shift_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    ;

shift_expr
    : add_expr
      { $$ = sysycc::make_nonterminal_node("shift_expr", {$1}); }
    | shift_expr SHL add_expr
      { $$ = sysycc::make_nonterminal_node("shift_expr", {$1, $2, $3}); }
    | shift_expr SHR add_expr
      { $$ = sysycc::make_nonterminal_node("shift_expr", {$1, $2, $3}); }
    ;

add_expr
    : mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1}); }
    | add_expr PLUS mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1, $2, $3}); }
    | add_expr MINUS mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1, $2, $3}); }
    ;

mul_expr
    : cast_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1}); }
    | mul_expr MUL cast_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    | mul_expr DIV cast_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    | mul_expr MOD cast_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    ;

cast_expr
    : unary_expr
      { $$ = sysycc::make_nonterminal_node("cast_expr", {$1}); }
    | EXTENSION cast_expr
      { $$ = sysycc::make_nonterminal_node("extension_expr", {$1, $2}); }
    | LPAREN cast_target_type RPAREN cast_expr
      { $$ = sysycc::make_nonterminal_node("cast_expr", {$1, $2, $3, $4}); }
    ;

cast_target_type
    : type_qualifier_seq_opt type_specifier type_qualifier_seq_opt
      { $$ = sysycc::make_nonterminal_node("cast_target_type", {$1, $2, $3}); }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer
      { $$ = sysycc::make_nonterminal_node("cast_target_type", {$1, $2, $3, $4}); }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt LPAREN pointer RPAREN LPAREN function_parameter_list_opt RPAREN
      {
          void *direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator", {$4, $5, $6, $7, $8, $9});
          void *declarator = sysycc::make_nonterminal_node("declarator",
                                                           {direct_declarator});
          $$ = sysycc::make_nonterminal_node("cast_target_type",
                                             {$1, $2, $3, declarator});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt pointer LPAREN pointer RPAREN LPAREN function_parameter_list_opt RPAREN
      {
          void *direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator", {$5, $6, $7, $8, $9, $10});
          void *declarator = sysycc::make_nonterminal_node(
              "declarator", {$4, direct_declarator});
          $$ = sysycc::make_nonterminal_node("cast_target_type",
                                             {$1, $2, $3, declarator});
      }
    | type_qualifier_seq_opt type_specifier type_qualifier_seq_opt LPAREN pointer LPAREN pointer RPAREN LPAREN function_parameter_list_opt RPAREN RPAREN LPAREN function_parameter_list_opt RPAREN
      {
          void *inner_direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator", {$6, $7, $8, $9, $10, $11});
          void *outer_direct_declarator = sysycc::make_nonterminal_node(
              "direct_declarator",
              {$4, $5, inner_direct_declarator, $12, $13, $14, $15});
          void *declarator = sysycc::make_nonterminal_node(
              "declarator", {outer_direct_declarator});
          $$ = sysycc::make_nonterminal_node("cast_target_type",
                                             {$1, $2, $3, declarator});
      }
    ;

sizeof_type_name
    : type_qualifier_seq_opt type_specifier sizeof_type_suffix_opt
      { $$ = sysycc::make_nonterminal_node("sizeof_type_name", {$1, $2, $3}); }
    ;

sizeof_type_suffix_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("sizeof_type_suffix_opt", {}); }
    | pointer
      { $$ = sysycc::make_nonterminal_node("sizeof_type_suffix_opt", {$1}); }
    | abstract_array_suffix_list
      { $$ = sysycc::make_nonterminal_node("sizeof_type_suffix_opt", {$1}); }
    | pointer abstract_array_suffix_list
      { $$ = sysycc::make_nonterminal_node("sizeof_type_suffix_opt", {$1, $2}); }
    ;

abstract_array_suffix_list
    : LBRACKET expr_opt RBRACKET
      { $$ = sysycc::make_nonterminal_node("abstract_array_suffix_list", {$1, $2, $3}); }
    | abstract_array_suffix_list LBRACKET expr_opt RBRACKET
      { $$ = sysycc::make_nonterminal_node("abstract_array_suffix_list", {$1, $2, $3, $4}); }
    ;

unary_expr
    : postfix_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1}); }
    | SIZEOF unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | SIZEOF LPAREN sizeof_type_name RPAREN
      { $$ = sysycc::make_nonterminal_node("sizeof_type_expr", {$1, $2, $3, $4}); }
    | PLUS cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | MINUS cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | NOT cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | BITNOT cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | BITAND cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | AND IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | MUL cast_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | INC unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | DEC unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    ;

postfix_expr
    : primary_expr
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1}); }
    | builtin_va_arg_expr
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1}); }
    | postfix_expr LBRACKET expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3, $4}); }
    | postfix_expr LPAREN RPAREN
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr LPAREN argument_expr_list RPAREN
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3, $4}); }
    | postfix_expr ARROW IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr ARROW TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr DOT IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr DOT TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr INC
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2}); }
    | postfix_expr DEC
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2}); }
    ;

builtin_va_arg_expr
    : IDENTIFIER LPAREN assignment_expr COMMA sizeof_type_name RPAREN
      { $$ = sysycc::make_nonterminal_node("builtin_va_arg_expr", {$1, $2, $3, $4, $5, $6}); }
    ;

primary_expr
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | INT_LITERAL
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | FLOAT_LITERAL
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | CHAR_LITERAL
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | string_literal_seq
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | statement_expr
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | gnu_builtin_types_compatible_expr
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | LPAREN cast_target_type RPAREN LBRACE RBRACE
      { $$ = sysycc::make_nonterminal_node("compound_literal_expr", {$1, $2, $3, $4, $5}); }
    | LPAREN cast_target_type RPAREN LBRACE init_val_list RBRACE
      { $$ = sysycc::make_nonterminal_node("compound_literal_expr", {$1, $2, $3, $4, $5, $6}); }
    | LPAREN expr RPAREN
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1, $2, $3}); }
    ;

gnu_builtin_types_compatible_expr
    : IDENTIFIER LPAREN gnu_typeof_type_specifier COMMA gnu_typeof_type_specifier RPAREN
      { $$ = sysycc::make_nonterminal_node("gnu_builtin_types_compatible_expr", {$1, $2, $3, $4, $5, $6}); }
    ;

statement_expr
    : LPAREN block RPAREN
      { $$ = sysycc::make_nonterminal_node("statement_expr", {$1, $2, $3}); }
    ;

init_val
    : assignment_expr
      { $$ = sysycc::make_nonterminal_node("init_val", {$1}); }
    | LBRACE RBRACE
      { $$ = sysycc::make_nonterminal_node("init_val", {$1, $2}); }
    | LBRACE init_val_list RBRACE
      { $$ = sysycc::make_nonterminal_node("init_val", {$1, $2, $3}); }
    ;

init_val_list
    : init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1}); }
    | designated_init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1}); }
    | init_val_list COMMA init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1, $2, $3}); }
    | init_val_list COMMA designated_init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1, $2, $3}); }
    | init_val_list COMMA
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1, $2}); }
    ;

designated_init_val
    : designator_seq ASSIGN init_val
      { $$ = sysycc::make_nonterminal_node("designated_init_val", {$1, $2, $3}); }
    ;

designator_seq
    : designator
      { $$ = sysycc::make_nonterminal_node("designator_seq", {$1}); }
    | designator_seq designator
      { $$ = sysycc::make_nonterminal_node("designator_seq", {$1, $2}); }
    ;

designator
    : DOT IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("designator", {$1, $2}); }
    | DOT TYPE_NAME
      { $$ = sysycc::make_nonterminal_node("designator", {$1, $2}); }
    | LBRACKET const_expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("designator", {$1, $2, $3}); }
    ;

%%

void yyerror(void *scanner, const char *message) {
    auto *lexer_state =
        static_cast<sysycc::LexerState *>(yyget_extra(scanner));
    sysycc::SourceSpan source_span;
    if (lexer_state != nullptr) {
        source_span =
            sysycc::SourceSpan(lexer_state->get_token_begin_position(),
                               lexer_state->get_token_end_position());
    }

    const char *token_text = yyget_text(scanner);
    sysycc::set_parser_error_info(sysycc::ParserErrorInfo(
        message == nullptr ? "syntax error" : message,
        token_text == nullptr ? "" : token_text, source_span));
}
