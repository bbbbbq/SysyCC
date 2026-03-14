%option noyywrap
%option nounput
%option noinput

%{
#include "parser.tab.h"
%}

DIGIT       [0-9]
LETTER      [A-Za-z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
DEC_INT     0|[1-9]{DIGIT}*

%%

[ \t\r\f\v]+                { }
\n                          { }
"//"[^\n]*                  { }
"/*"([^*]|\*+[^*/])*\*+"/"  { }

"const"                     { return CONST; }
"int"                       { return INT; }
"void"                      { return VOID; }
"if"                        { return IF; }
"else"                      { return ELSE; }
"while"                     { return WHILE; }
"break"                     { return BREAK; }
"continue"                  { return CONTINUE; }
"return"                    { return RETURN; }

"=="                        { return EQ; }
"!="                        { return NE; }
"<="                        { return LE; }
">="                        { return GE; }
"&&"                        { return AND; }
"||"                        { return OR; }

"+"                         { return PLUS; }
"-"                         { return MINUS; }
"*"                         { return MUL; }
"/"                         { return DIV; }
"%"                         { return MOD; }
"="                         { return ASSIGN; }
"<"                         { return LT; }
">"                         { return GT; }
"!"                         { return NOT; }

";"                         { return SEMICOLON; }
","                         { return COMMA; }
"("                         { return LPAREN; }
")"                         { return RPAREN; }
"["                         { return LBRACKET; }
"]"                         { return RBRACKET; }
"{"                         { return LBRACE; }
"}"                         { return RBRACE; }

{DEC_INT}                   { return INT_LITERAL; }
{IDENT}                     { return IDENTIFIER; }

.                           { return INVALID; }

%%
