%{
#include <cstdio>

int yylex(void);
void yyerror(const char* message);
%}

%token INVALID
%token CONST INT VOID IF ELSE WHILE BREAK CONTINUE RETURN
%token IDENTIFIER INT_LITERAL
%token PLUS MINUS MUL DIV MOD
%token ASSIGN EQ NE LT LE GT GE
%token NOT AND OR
%token SEMICOLON COMMA
%token LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE

%start comp_unit
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

comp_unit
    : comp_unit_items
    ;

comp_unit_items
    : /* empty */
    | comp_unit_items comp_unit_item
    ;

comp_unit_item
    : decl
    | func_def
    ;

decl
    : const_decl
    | var_decl
    ;

const_decl
    : CONST INT const_def_list SEMICOLON
    ;

const_def_list
    : const_def
    | const_def_list COMMA const_def
    ;

const_def
    : IDENTIFIER ASSIGN expr
    | IDENTIFIER const_dims ASSIGN const_init_val
    ;

const_dims
    : LBRACKET const_expr RBRACKET
    | const_dims LBRACKET const_expr RBRACKET
    ;

var_decl
    : INT var_def_list SEMICOLON
    ;

var_def_list
    : var_def
    | var_def_list COMMA var_def
    ;

var_def
    : IDENTIFIER
    | IDENTIFIER dims
    | IDENTIFIER ASSIGN init_val
    | IDENTIFIER dims ASSIGN init_val
    ;

dims
    : LBRACKET expr RBRACKET
    | dims LBRACKET expr RBRACKET
    ;

func_def
    : INT IDENTIFIER LPAREN RPAREN block
    | INT IDENTIFIER LPAREN func_fparams RPAREN block
    | VOID IDENTIFIER LPAREN RPAREN block
    | VOID IDENTIFIER LPAREN func_fparams RPAREN block
    ;

func_fparams
    : func_fparam
    | func_fparams COMMA func_fparam
    ;

func_fparam
    : INT IDENTIFIER
    | INT IDENTIFIER LBRACKET RBRACKET
    | INT IDENTIFIER LBRACKET RBRACKET dims
    ;

block
    : LBRACE block_items RBRACE
    ;

block_items
    : /* empty */
    | block_items block_item
    ;

block_item
    : decl
    | stmt
    ;

stmt
    : lval ASSIGN expr SEMICOLON
    | expr SEMICOLON
    | SEMICOLON
    | block
    | IF LPAREN cond RPAREN stmt %prec LOWER_THAN_ELSE
    | IF LPAREN cond RPAREN stmt ELSE stmt
    | WHILE LPAREN cond RPAREN stmt
    | BREAK SEMICOLON
    | CONTINUE SEMICOLON
    | RETURN SEMICOLON
    | RETURN expr SEMICOLON
    ;

expr
    : l_or_expr
    ;

const_expr
    : expr
    ;

cond
    : l_or_expr
    ;

lval
    : IDENTIFIER
    | lval LBRACKET expr RBRACKET
    ;

primary_expr
    : lval
    | INT_LITERAL
    | LPAREN expr RPAREN
    ;

func_rparams
    : expr
    | func_rparams COMMA expr
    ;

unary_expr
    : primary_expr
    | IDENTIFIER LPAREN RPAREN
    | IDENTIFIER LPAREN func_rparams RPAREN
    | PLUS unary_expr
    | MINUS unary_expr
    | NOT unary_expr
    ;

mul_expr
    : unary_expr
    | mul_expr MUL unary_expr
    | mul_expr DIV unary_expr
    | mul_expr MOD unary_expr
    ;

add_expr
    : mul_expr
    | add_expr PLUS mul_expr
    | add_expr MINUS mul_expr
    ;

rel_expr
    : add_expr
    | rel_expr LT add_expr
    | rel_expr LE add_expr
    | rel_expr GT add_expr
    | rel_expr GE add_expr
    ;

eq_expr
    : rel_expr
    | eq_expr EQ rel_expr
    | eq_expr NE rel_expr
    ;

l_and_expr
    : eq_expr
    | l_and_expr AND eq_expr
    ;

l_or_expr
    : l_and_expr
    | l_or_expr OR l_and_expr
    ;

const_init_val
    : const_expr
    | LBRACE RBRACE
    | LBRACE const_init_val_list RBRACE
    ;

const_init_val_list
    : const_init_val
    | const_init_val_list COMMA const_init_val
    ;

init_val
    : expr
    | LBRACE RBRACE
    | LBRACE init_val_list RBRACE
    ;

init_val_list
    : init_val
    | init_val_list COMMA init_val
    ;

%%

void yyerror(const char* message) {
    std::fprintf(stderr, "parser error: %s\n", message);
}
