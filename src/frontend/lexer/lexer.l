%option noyywrap
%option nounput
%option noinput
%x COMMENT

%{
#include "frontend/parser/parser.tab.h"
#include "frontend/parser/parser_runtime.hpp"

static int g_line = 1;
static int g_column = 1;
static int g_token_line_begin = 1;
static int g_token_column_begin = 1;
static int g_token_line_end = 1;
static int g_token_column_end = 1;

void reset_lexer_state(void);
int lexer_current_line_begin(void);
int lexer_current_column_begin(void);
int lexer_current_line_end(void);
int lexer_current_column_end(void);

static void update_position(const char *text, int length) {
    g_token_line_begin = g_line;
    g_token_column_begin = g_column;
    g_token_line_end = g_line;
    g_token_column_end = g_column;

    for (int i = 0; i < length; ++i) {
        g_token_line_end = g_line;
        g_token_column_end = g_column;
        if (text[i] == '\n') {
            ++g_line;
            g_column = 1;
        } else {
            ++g_column;
        }
    }
}

static int make_token(int token, const char *label) {
    yylval.node = sysycc::make_terminal_node(label, yytext);
    return token;
}

#define YY_USER_ACTION update_position(yytext, yyleng);
%}

DIGIT       [0-9]
NONZERO     [1-9]
OCT_DIGIT   [0-7]
HEX_DIGIT   [0-9a-fA-F]
LETTER      [A-Za-z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
DEC_INT     0|{NONZERO}{DIGIT}*
OCT_INT     0{OCT_DIGIT}+
HEX_INT     0[xX]{HEX_DIGIT}+
INVALID_OCT 0{OCT_DIGIT}*[89]{DIGIT}*
INVALID_HEX 0[xX]({LETTER}|{DIGIT})*

%%
[ \t\r\f\v]+                { }
\n                          { }
"//"[^\n]*                  { }
"/*"                        { BEGIN(COMMENT); }

<COMMENT>"*/"               { BEGIN(INITIAL); }
<COMMENT>[^*\n]+            { }
<COMMENT>"*"+[^*/\n]*       { }
<COMMENT>\n                 { }
<COMMENT><<EOF>>            { BEGIN(INITIAL); return make_token(INVALID, "INVALID"); }

"const"                     { return make_token(CONST, "CONST"); }
"int"                       { return make_token(INT, "INT"); }
"void"                      { return make_token(VOID, "VOID"); }
"if"                        { return make_token(IF, "IF"); }
"else"                      { return make_token(ELSE, "ELSE"); }
"while"                     { return make_token(WHILE, "WHILE"); }
"break"                     { return make_token(BREAK, "BREAK"); }
"continue"                  { return make_token(CONTINUE, "CONTINUE"); }
"return"                    { return make_token(RETURN, "RETURN"); }

"=="                        { return make_token(EQ, "EQ"); }
"!="                        { return make_token(NE, "NE"); }
"<="                        { return make_token(LE, "LE"); }
">="                        { return make_token(GE, "GE"); }
"&&"                        { return make_token(AND, "AND"); }
"||"                        { return make_token(OR, "OR"); }

"+"                         { return make_token(PLUS, "PLUS"); }
"-"                         { return make_token(MINUS, "MINUS"); }
"*"                         { return make_token(MUL, "MUL"); }
"/"                         { return make_token(DIV, "DIV"); }
"%"                         { return make_token(MOD, "MOD"); }
"="                         { return make_token(ASSIGN, "ASSIGN"); }
"<"                         { return make_token(LT, "LT"); }
">"                         { return make_token(GT, "GT"); }
"!"                         { return make_token(NOT, "NOT"); }

";"                         { return make_token(SEMICOLON, "SEMICOLON"); }
","                         { return make_token(COMMA, "COMMA"); }
"("                         { return make_token(LPAREN, "LPAREN"); }
")"                         { return make_token(RPAREN, "RPAREN"); }
"["                         { return make_token(LBRACKET, "LBRACKET"); }
"]"                         { return make_token(RBRACKET, "RBRACKET"); }
"{"                         { return make_token(LBRACE, "LBRACE"); }
"}"                         { return make_token(RBRACE, "RBRACE"); }

{HEX_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL"); }
{OCT_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL"); }
{DEC_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL"); }
{INVALID_HEX}               { return make_token(INVALID, "INVALID"); }
{INVALID_OCT}               { return make_token(INVALID, "INVALID"); }
{IDENT}                     { return make_token(IDENTIFIER, "IDENTIFIER"); }

.                           { return make_token(INVALID, "INVALID"); }

%%

void reset_lexer_state(void) {
    g_line = 1;
    g_column = 1;
    g_token_line_begin = 1;
    g_token_column_begin = 1;
    g_token_line_end = 1;
    g_token_column_end = 1;
}

int lexer_current_line_begin(void) {
    return g_token_line_begin;
}

int lexer_current_column_begin(void) {
    return g_token_column_begin;
}

int lexer_current_line_end(void) {
    return g_token_line_end;
}

int lexer_current_column_end(void) {
    return g_token_column_end;
}
