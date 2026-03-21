%option reentrant
%option bison-bridge
%option noyywrap
%option nounput
%option noinput

%{
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.tab.h"
#include "frontend/parser/parser_runtime.hpp"

static void update_position(sysycc::LexerState *state, const char *text,
                            int length) {
    if (state == nullptr) {
        return;
    }
    state->update_position(text, length);
}

static int make_token(int token, const char *label,
                      sysycc::LexerState *state, const char *text,
                      YYSTYPE *yylval_param) {
    if (state != nullptr && state->get_emit_parse_nodes()) {
        yylval_param->node = sysycc::make_terminal_node(
            label, text,
            sysycc::SourceSpan(
                state->get_token_begin_position(),
                state->get_token_end_position()));
    }
    return token;
}

#define YY_USER_ACTION update_position((sysycc::LexerState *)yyextra, yytext, yyleng);
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
DEC_FLOAT   ({DIGIT}+"."{DIGIT}*|{DIGIT}*"."{DIGIT}+)([eE][+-]?{DIGIT}+)?
SCI_FLOAT   {DIGIT}+[eE][+-]?{DIGIT}+
FLOAT_CONST {DEC_FLOAT}|{SCI_FLOAT}
ESCAPE      \\(['"\\abfnrtv?]|[0-7]{1,3}|x{HEX_DIGIT}+)
CHAR_BODY   ([^'\\\n]|{ESCAPE})
CHAR_CONST  \'{CHAR_BODY}\'
STRING_BODY ([^"\\\n]|{ESCAPE})*
STRING_CONST \"{STRING_BODY}\"
INVALID_OCT 0{OCT_DIGIT}*[89]{DIGIT}*
INVALID_HEX 0[xX]({LETTER}|{DIGIT})*

%%
[ \t\r\f\v]+                { }
\n                          { }

"const"                     { return make_token(CONST, "CONST", (sysycc::LexerState *)yyextra, yytext, yylval); }
"int"                       { return make_token(INT, "INT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"void"                      { return make_token(VOID, "VOID", (sysycc::LexerState *)yyextra, yytext, yylval); }
"float"                     { return make_token(FLOAT, "FLOAT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"if"                        { return make_token(IF, "IF", (sysycc::LexerState *)yyextra, yytext, yylval); }
"else"                      { return make_token(ELSE, "ELSE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"while"                     { return make_token(WHILE, "WHILE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"for"                       { return make_token(FOR, "FOR", (sysycc::LexerState *)yyextra, yytext, yylval); }
"do"                        { return make_token(DO, "DO", (sysycc::LexerState *)yyextra, yytext, yylval); }
"switch"                    { return make_token(SWITCH, "SWITCH", (sysycc::LexerState *)yyextra, yytext, yylval); }
"case"                      { return make_token(CASE, "CASE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"default"                   { return make_token(DEFAULT, "DEFAULT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"break"                     { return make_token(BREAK, "BREAK", (sysycc::LexerState *)yyextra, yytext, yylval); }
"continue"                  { return make_token(CONTINUE, "CONTINUE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"return"                    { return make_token(RETURN, "RETURN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"struct"                    { return make_token(STRUCT, "STRUCT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"enum"                      { return make_token(ENUM, "ENUM", (sysycc::LexerState *)yyextra, yytext, yylval); }
"typedef"                   { return make_token(TYPEDEF, "TYPEDEF", (sysycc::LexerState *)yyextra, yytext, yylval); }

"++"                        { return make_token(INC, "INC", (sysycc::LexerState *)yyextra, yytext, yylval); }
"--"                        { return make_token(DEC, "DEC", (sysycc::LexerState *)yyextra, yytext, yylval); }
"->"                        { return make_token(ARROW, "ARROW", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<<"                        { return make_token(SHL, "SHL", (sysycc::LexerState *)yyextra, yytext, yylval); }
">>"                        { return make_token(SHR, "SHR", (sysycc::LexerState *)yyextra, yytext, yylval); }
"=="                        { return make_token(EQ, "EQ", (sysycc::LexerState *)yyextra, yytext, yylval); }
"!="                        { return make_token(NE, "NE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<="                        { return make_token(LE, "LE", (sysycc::LexerState *)yyextra, yytext, yylval); }
">="                        { return make_token(GE, "GE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"&&"                        { return make_token(AND, "AND", (sysycc::LexerState *)yyextra, yytext, yylval); }
"||"                        { return make_token(OR, "OR", (sysycc::LexerState *)yyextra, yytext, yylval); }

"+"                         { return make_token(PLUS, "PLUS", (sysycc::LexerState *)yyextra, yytext, yylval); }
"-"                         { return make_token(MINUS, "MINUS", (sysycc::LexerState *)yyextra, yytext, yylval); }
"*"                         { return make_token(MUL, "MUL", (sysycc::LexerState *)yyextra, yytext, yylval); }
"/"                         { return make_token(DIV, "DIV", (sysycc::LexerState *)yyextra, yytext, yylval); }
"%"                         { return make_token(MOD, "MOD", (sysycc::LexerState *)yyextra, yytext, yylval); }
"&"                         { return make_token(BITAND, "BITAND", (sysycc::LexerState *)yyextra, yytext, yylval); }
"|"                         { return make_token(BITOR, "BITOR", (sysycc::LexerState *)yyextra, yytext, yylval); }
"^"                         { return make_token(BITXOR, "BITXOR", (sysycc::LexerState *)yyextra, yytext, yylval); }
"~"                         { return make_token(BITNOT, "BITNOT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"="                         { return make_token(ASSIGN, "ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<"                         { return make_token(LT, "LT", (sysycc::LexerState *)yyextra, yytext, yylval); }
">"                         { return make_token(GT, "GT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"!"                         { return make_token(NOT, "NOT", (sysycc::LexerState *)yyextra, yytext, yylval); }

";"                         { return make_token(SEMICOLON, "SEMICOLON", (sysycc::LexerState *)yyextra, yytext, yylval); }
","                         { return make_token(COMMA, "COMMA", (sysycc::LexerState *)yyextra, yytext, yylval); }
":"                         { return make_token(COLON, "COLON", (sysycc::LexerState *)yyextra, yytext, yylval); }
"."                         { return make_token(DOT, "DOT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"("                         { return make_token(LPAREN, "LPAREN", (sysycc::LexerState *)yyextra, yytext, yylval); }
")"                         { return make_token(RPAREN, "RPAREN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"["                         { return make_token(LBRACKET, "LBRACKET", (sysycc::LexerState *)yyextra, yytext, yylval); }
"]"                         { return make_token(RBRACKET, "RBRACKET", (sysycc::LexerState *)yyextra, yytext, yylval); }
"{"                         { return make_token(LBRACE, "LBRACE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"}"                         { return make_token(RBRACE, "RBRACE", (sysycc::LexerState *)yyextra, yytext, yylval); }

{STRING_CONST}              { return make_token(STRING_LITERAL, "STRING_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{CHAR_CONST}                { return make_token(CHAR_LITERAL, "CHAR_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{FLOAT_CONST}               { return make_token(FLOAT_LITERAL, "FLOAT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{HEX_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{OCT_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{DEC_INT}                   { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{INVALID_HEX}               { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }
{INVALID_OCT}               { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }
{IDENT}                     { return make_token(IDENTIFIER, "IDENTIFIER", (sysycc::LexerState *)yyextra, yytext, yylval); }

.                           { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }

%%
