%{
#include <cstdio>

#include "frontend/driver/parser_runtime.hpp"

int yylex(void);
void yyerror(const char *message);
%}

%union {
    void *node;
}

%token <node> INVALID
%token <node> CONST INT VOID IF ELSE WHILE BREAK CONTINUE RETURN
%token <node> IDENTIFIER INT_LITERAL
%token <node> PLUS MINUS MUL DIV MOD
%token <node> ASSIGN EQ NE LT LE GT GE
%token <node> NOT AND OR
%token <node> SEMICOLON COMMA
%token <node> LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE

%type <node> comp_unit comp_unit_items comp_unit_item
%type <node> decl const_decl const_def_list const_def const_dims
%type <node> var_decl var_def_list var_def dims
%type <node> func_def func_fparams func_fparam
%type <node> block block_items block_item stmt
%type <node> expr const_expr cond lval primary_expr func_rparams
%type <node> unary_expr mul_expr add_expr rel_expr eq_expr l_and_expr l_or_expr
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
    | func_def
      { $$ = sysycc::make_nonterminal_node("comp_unit_item", {$1}); }
    ;

decl
    : const_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    | var_decl
      { $$ = sysycc::make_nonterminal_node("decl", {$1}); }
    ;

const_decl
    : CONST INT const_def_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("const_decl", {$1, $2, $3, $4}); }
    ;

const_def_list
    : const_def
      { $$ = sysycc::make_nonterminal_node("const_def_list", {$1}); }
    | const_def_list COMMA const_def
      { $$ = sysycc::make_nonterminal_node("const_def_list", {$1, $2, $3}); }
    ;

const_def
    : IDENTIFIER ASSIGN expr
      { $$ = sysycc::make_nonterminal_node("const_def", {$1, $2, $3}); }
    | IDENTIFIER const_dims ASSIGN const_init_val
      { $$ = sysycc::make_nonterminal_node("const_def", {$1, $2, $3, $4}); }
    ;

const_dims
    : LBRACKET const_expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("const_dims", {$1, $2, $3}); }
    | const_dims LBRACKET const_expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("const_dims", {$1, $2, $3, $4}); }
    ;

var_decl
    : INT var_def_list SEMICOLON
      { $$ = sysycc::make_nonterminal_node("var_decl", {$1, $2, $3}); }
    ;

var_def_list
    : var_def
      { $$ = sysycc::make_nonterminal_node("var_def_list", {$1}); }
    | var_def_list COMMA var_def
      { $$ = sysycc::make_nonterminal_node("var_def_list", {$1, $2, $3}); }
    ;

var_def
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("var_def", {$1}); }
    | IDENTIFIER dims
      { $$ = sysycc::make_nonterminal_node("var_def", {$1, $2}); }
    | IDENTIFIER ASSIGN init_val
      { $$ = sysycc::make_nonterminal_node("var_def", {$1, $2, $3}); }
    | IDENTIFIER dims ASSIGN init_val
      { $$ = sysycc::make_nonterminal_node("var_def", {$1, $2, $3, $4}); }
    ;

dims
    : LBRACKET expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("dims", {$1, $2, $3}); }
    | dims LBRACKET expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("dims", {$1, $2, $3, $4}); }
    ;

func_def
    : INT IDENTIFIER LPAREN RPAREN block
      { $$ = sysycc::make_nonterminal_node("func_def", {$1, $2, $3, $4, $5}); }
    | INT IDENTIFIER LPAREN func_fparams RPAREN block
      { $$ = sysycc::make_nonterminal_node("func_def", {$1, $2, $3, $4, $5, $6}); }
    | VOID IDENTIFIER LPAREN RPAREN block
      { $$ = sysycc::make_nonterminal_node("func_def", {$1, $2, $3, $4, $5}); }
    | VOID IDENTIFIER LPAREN func_fparams RPAREN block
      { $$ = sysycc::make_nonterminal_node("func_def", {$1, $2, $3, $4, $5, $6}); }
    ;

func_fparams
    : func_fparam
      { $$ = sysycc::make_nonterminal_node("func_fparams", {$1}); }
    | func_fparams COMMA func_fparam
      { $$ = sysycc::make_nonterminal_node("func_fparams", {$1, $2, $3}); }
    ;

func_fparam
    : INT IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("func_fparam", {$1, $2}); }
    | INT IDENTIFIER LBRACKET RBRACKET
      { $$ = sysycc::make_nonterminal_node("func_fparam", {$1, $2, $3, $4}); }
    | INT IDENTIFIER LBRACKET RBRACKET dims
      { $$ = sysycc::make_nonterminal_node("func_fparam", {$1, $2, $3, $4, $5}); }
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
    : lval ASSIGN expr SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4}); }
    | expr SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1}); }
    | block
      { $$ = sysycc::make_nonterminal_node("stmt", {$1}); }
    | IF LPAREN cond RPAREN stmt %prec LOWER_THAN_ELSE
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | IF LPAREN cond RPAREN stmt ELSE stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5, $6, $7}); }
    | WHILE LPAREN cond RPAREN stmt
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3, $4, $5}); }
    | BREAK SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | CONTINUE SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | RETURN SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2}); }
    | RETURN expr SEMICOLON
      { $$ = sysycc::make_nonterminal_node("stmt", {$1, $2, $3}); }
    ;

expr
    : l_or_expr
      { $$ = sysycc::make_nonterminal_node("expr", {$1}); }
    ;

const_expr
    : expr
      { $$ = sysycc::make_nonterminal_node("const_expr", {$1}); }
    ;

cond
    : l_or_expr
      { $$ = sysycc::make_nonterminal_node("cond", {$1}); }
    ;

lval
    : IDENTIFIER
      { $$ = sysycc::make_nonterminal_node("lval", {$1}); }
    | lval LBRACKET expr RBRACKET
      { $$ = sysycc::make_nonterminal_node("lval", {$1, $2, $3, $4}); }
    ;

primary_expr
    : lval
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | INT_LITERAL
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1}); }
    | LPAREN expr RPAREN
      { $$ = sysycc::make_nonterminal_node("primary_expr", {$1, $2, $3}); }
    ;

func_rparams
    : expr
      { $$ = sysycc::make_nonterminal_node("func_rparams", {$1}); }
    | func_rparams COMMA expr
      { $$ = sysycc::make_nonterminal_node("func_rparams", {$1, $2, $3}); }
    ;

unary_expr
    : primary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1}); }
    | IDENTIFIER LPAREN RPAREN
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2, $3}); }
    | IDENTIFIER LPAREN func_rparams RPAREN
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2, $3, $4}); }
    | PLUS unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | MINUS unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
    | NOT unary_expr
      { $$ = sysycc::make_nonterminal_node("unary_expr", {$1, $2}); }
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

add_expr
    : mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1}); }
    | add_expr PLUS mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1, $2, $3}); }
    | add_expr MINUS mul_expr
      { $$ = sysycc::make_nonterminal_node("add_expr", {$1, $2, $3}); }
    ;

rel_expr
    : add_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1}); }
    | rel_expr LT add_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr LE add_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr GT add_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    | rel_expr GE add_expr
      { $$ = sysycc::make_nonterminal_node("rel_expr", {$1, $2, $3}); }
    ;

eq_expr
    : rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1}); }
    | eq_expr EQ rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1, $2, $3}); }
    | eq_expr NE rel_expr
      { $$ = sysycc::make_nonterminal_node("eq_expr", {$1, $2, $3}); }
    ;

l_and_expr
    : eq_expr
      { $$ = sysycc::make_nonterminal_node("l_and_expr", {$1}); }
    | l_and_expr AND eq_expr
      { $$ = sysycc::make_nonterminal_node("l_and_expr", {$1, $2, $3}); }
    ;

l_or_expr
    : l_and_expr
      { $$ = sysycc::make_nonterminal_node("l_or_expr", {$1}); }
    | l_or_expr OR l_and_expr
      { $$ = sysycc::make_nonterminal_node("l_or_expr", {$1, $2, $3}); }
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

void yyerror(const char *message) {
    std::fprintf(stderr, "parser error: %s\n", message);
}
