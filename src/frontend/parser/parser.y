%{
#include <cstdio>

#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser_runtime.hpp"

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
%token <node> CONST EXTERN ATTRIBUTE INLINE LONG INT VOID FLOAT DOUBLE IF ELSE WHILE FOR DO SWITCH CASE DEFAULT BREAK CONTINUE RETURN STRUCT ENUM TYPEDEF
%token <node> IDENTIFIER INT_LITERAL FLOAT_LITERAL CHAR_LITERAL STRING_LITERAL
%token <node> PLUS MINUS MUL DIV MOD
%token <node> INC DEC BITAND BITOR BITXOR BITNOT SHL SHR ARROW
%token <node> ASSIGN EQ NE LT LE GT GE
%token <node> NOT AND OR
%token <node> QUESTION SEMICOLON COMMA COLON DOT
%token <node> LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE

%type <node> comp_unit comp_unit_items comp_unit_item
%type <node> decl const_decl const_init_declarator_list const_init_declarator
%type <node> var_decl init_declarator_list init_declarator declarator_list declarator pointer
%type <node> direct_declarator expr_opt
%type <node> typedef_decl struct_decl enum_decl
%type <node> type_specifier nonvoid_type_specifier basic_type struct_specifier enum_specifier
%type <node> struct_field_list_opt struct_field_list struct_field_decl
%type <node> enumerator_list_opt enumerator_list enumerator
%type <node> func_def func_decl storage_specifier_opt parameter_list_opt parameter_list parameter_decl
%type <node> attribute_specifier_seq_opt attribute_specifier_seq attribute_specifier
%type <node> attribute_list_opt attribute_list attribute attribute_argument_list_opt
%type <node> attribute_argument_list attribute_argument
%type <node> block block_items block_item stmt
%type <node> expr const_expr cond argument_expr_list
%type <node> assignment_expr conditional_expr logical_or_expr logical_and_expr bit_or_expr
%type <node> bit_xor_expr bit_and_expr eq_expr rel_expr shift_expr add_expr
%type <node> mul_expr unary_expr postfix_expr primary_expr
%type <node> const_init_val const_init_val_list init_val init_val_list

%start comp_unit
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

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
    | func_decl
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    | func_def
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    ;

storage_specifier_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {}); }
    | EXTERN
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {$1}); }
    | INLINE
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {$1}); }
    | EXTERN INLINE
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {$1, $2}); }
    | INLINE EXTERN
      { $$ = sysycc::make_nonterminal_node("storage_specifier_opt", {$1, $2}); }
    ;

decl
    : const_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | var_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | typedef_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | struct_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | enum_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    ;

const_decl
    : CONST type_specifier const_init_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("const_decl", {$1, $2, $3, $4}); }
    ;

const_init_declarator_list
    : const_init_declarator
      { $$ = sysycc::make_nonterminal_node("const_init_declarator_list", {$1}); }
    | const_init_declarator_list COMMA const_init_declarator
      { $$ = sysycc::make_nonterminal_node("const_init_declarator_list", {$1, $2, $3}); }
    ;

const_init_declarator
    : declarator ASSIGN const_init_val
      { $$ = sysycc::make_nonterminal_node("const_init_declarator", {$1, $2, $3}); }
    ;

var_decl
    : type_specifier init_declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("var_decl", {$1, $2, $3}); }
    ;

typedef_decl
    : TYPEDEF type_specifier declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("typedef_decl", {$1, $2, $3, $4}); }
    ;

struct_decl
    : struct_specifier SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_decl", {$1, $2}); }
    ;

enum_decl
    : enum_specifier SEMICOLON
      { $$ = sysycc::make_nonterminal_node("enum_decl", {$1, $2}); }
    ;

type_specifier
    : basic_type
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | struct_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | enum_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    ;

nonvoid_type_specifier
    : LONG DOUBLE
      {
          void *basic_type =
              sysycc::make_nonterminal_node("basic_type", {$1, $2});
          $$ = sysycc::make_nonterminal_node("type_specifier", {basic_type});
      }
    | INT
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
    | struct_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    | enum_specifier
      { $$ = sysycc::make_nonterminal_node("type_specifier", {$1}); }
    ;

basic_type
    : LONG DOUBLE
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1, $2}); }
    | INT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | VOID
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | FLOAT
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    | DOUBLE
      { $$ = sysycc::make_nonterminal_node("basic_type", {$1}); }
    ;

struct_specifier
    : STRUCT IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("struct_specifier", {$1, $2}); }
    | STRUCT IDENTIFIER LBRACE struct_field_list_opt RBRACE
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
    : type_specifier declarator_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("struct_field_decl", {$1, $2, $3}); }
    ;

enum_specifier
    : ENUM IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("enum_specifier", {$1, $2}); }
    | ENUM IDENTIFIER LBRACE enumerator_list_opt RBRACE
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
    : declarator
      { $$ = sysycc::make_nonterminal_node("init_declarator", {$1}); }
    | declarator ASSIGN init_val
      { $$ = sysycc::make_nonterminal_node("init_declarator", {$1, $2, $3}); }
    ;

declarator_list
    : declarator
      { $$ = sysycc::make_nonterminal_node("declarator_list", {$1}); }
    | declarator_list COMMA declarator
      { $$ = sysycc::make_nonterminal_node("declarator_list", {$1, $2, $3}); }
    ;

declarator
    : direct_declarator
      { $$ = sysycc::make_nonterminal_node("declarator", {$1}); }
    | pointer direct_declarator
      { $$ = sysycc::make_nonterminal_node("declarator", {$1, $2}); }
    ;

pointer
    : MUL
      { $$ = sysycc::make_nonterminal_node("pointer", {$1}); }
    | MUL pointer
      { $$ = sysycc::make_nonterminal_node("pointer", {$1, $2}); }
    ;

direct_declarator
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1}); }
    | direct_declarator LBRACKET expr_opt RBRACKET
      { $$ = sysycc::make_nonterminal_node("direct_declarator", {$1, $2, $3, $4}); }
    ;

func_def
    : storage_specifier_opt attribute_specifier_seq_opt type_specifier IDENTIFIER LPAREN parameter_list_opt RPAREN block
      { $$ = sysycc::make_nonterminal_node("func_def", {$1, $2, $3, $4, $5, $6, $7, $8}); }
    ;

func_decl
    : storage_specifier_opt attribute_specifier_seq_opt type_specifier IDENTIFIER LPAREN parameter_list_opt RPAREN SEMICOLON
      { $$ = sysycc::make_nonterminal_node("func_decl", {$1, $2, $3, $4, $5, $6, $7, $8}); }
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
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("attribute", {$1}); }
    | IDENTIFIER LPAREN attribute_argument_list_opt RPAREN
      { $$ = sysycc::make_nonterminal_node("attribute", {$1, $2, $3, $4}); }
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
    ;

parameter_list_opt
    : /* empty */
      { $$ = sysycc::make_nonterminal_node("parameter_list_opt", {}); }
    | VOID
      { $$ = sysycc::make_nonterminal_node("parameter_list_opt", {$1}); }
    | parameter_list
      { $$ = sysycc::make_nonterminal_node("parameter_list_opt", {$1}); }
    ;

parameter_list
    : parameter_decl
      { $$ = sysycc::make_nonterminal_node("parameter_list", {$1}); }
    | parameter_list COMMA parameter_decl
      { $$ = sysycc::make_nonterminal_node("parameter_list", {$1, $2, $3}); }
    ;

parameter_decl
    : type_specifier declarator
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1, $2}); }
    | nonvoid_type_specifier
      { $$ = sysycc::make_nonterminal_node("parameter_decl", {$1}); }
    ;

block
    : LBRACE block_items RBRACE
      { $$ = sysycc::make_nonterminal_node("block", {$1, $2, $3}); }
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
    | SWITCH LPAREN expr RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | CASE const_expr COLON stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4}); }
    | DEFAULT COLON stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    | BREAK SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | CONTINUE SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
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
    ;

conditional_expr
    : logical_or_expr
      { $$ = sysycc::make_nonterminal_node("conditional_expr", {$1}); }
    | logical_or_expr QUESTION expr COLON conditional_expr
      { $$ = sysycc::make_nonterminal_node("conditional_expr", {$1, $2, $3, $4, $5}); }
    ;

argument_expr_list
    : expr
      { $$ = sysycc::make_nonterminal_node("argument_expr_list", {$1}); }
    | argument_expr_list COMMA expr
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
    : unary_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1}); }
    | mul_expr MUL unary_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    | mul_expr DIV unary_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    | mul_expr MOD unary_expr
      { $$ = sysycc::make_nonterminal_node("mul_expr", {$1, $2, $3}); }
    ;

unary_expr
    : postfix_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1}); }
    | PLUS unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | MINUS unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | NOT unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | BITNOT unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | BITAND unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | MUL unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | INC unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | DEC unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    ;

postfix_expr
    : primary_expr
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1}); }
    | postfix_expr LBRACKET expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3, $4}); }
    | postfix_expr LPAREN RPAREN
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr LPAREN argument_expr_list RPAREN
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3, $4}); }
    | postfix_expr ARROW IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr DOT IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2, $3}); }
    | postfix_expr INC
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2}); }
    | postfix_expr DEC
      { $$ = sysycc::make_nonterminal_node("postfix_expr", {$1, $2}); }
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
    | STRING_LITERAL
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | LPAREN expr RPAREN
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1, $2, $3}); }
    ;

const_init_val
    : const_expr
      { $$ = sysycc::make_nonterminal_node("const_init_val", {$1}); }
    | LBRACE RBRACE
      { $$ = sysycc::make_nonterminal_node("const_init_val", {$1, $2}); }
    | LBRACE const_init_val_list RBRACE
      { $$ = sysycc::make_nonterminal_node("const_init_val", {$1, $2, $3}); }
    ;

const_init_val_list
    : const_init_val
      { $$ = sysycc::make_nonterminal_node("const_init_val_list", {$1}); }
    | const_init_val_list COMMA const_init_val
      { $$ = sysycc::make_nonterminal_node("const_init_val_list", {$1, $2, $3}); }
    ;

init_val
    : expr
      { $$ = sysycc::make_nonterminal_node("init_val", {$1}); }
    | LBRACE RBRACE
      { $$ = sysycc::make_nonterminal_node("init_val", {$1, $2}); }
    | LBRACE init_val_list RBRACE
      { $$ = sysycc::make_nonterminal_node("init_val", {$1, $2, $3}); }
    ;

init_val_list
    : init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1}); }
    | init_val_list COMMA init_val
      { $$ = sysycc::make_nonterminal_node("init_val_list", {$1, $2, $3}); }
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
