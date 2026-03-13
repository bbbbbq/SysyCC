%option noyywrap
%option nounput
%option noinput

%{
#include <cstdio>
#include <cstdlib>

static int g_line = 1;
static int g_column = 1;
static int g_token_line = 1;
static int g_token_column = 1;
static int g_comment_line = 1;
static int g_comment_column = 1;

void reset_lexer_state(void);

static void update_position(const char* text, int length) {
    g_token_line = g_line;
    g_token_column = g_column;

    for (int i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            ++g_line;
            g_column = 1;
        } else {
            ++g_column;
        }
    }
}

static void print_token(const char* kind) {
    std::printf("%d:%d %-14s %s\n", g_token_line, g_token_column, kind, yytext);
}

#define YY_USER_ACTION update_position(yytext, yyleng);
%}

%x BLOCK_COMMENT

DIGIT       [0-9]
HEX_DIGIT   [0-9a-fA-F]
LETTER      [A-Za-z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
DEC_INT     0|[1-9]{DIGIT}*
OCT_INT     0[0-7]+
HEX_INT     0[xX]{HEX_DIGIT}+

%%

[ \t\r\f\v]+                { }
\n                          { }
"//"[^\n]*                  { }

"/*"                        {
                               g_comment_line = g_token_line;
                               g_comment_column = g_token_column;
                               BEGIN(BLOCK_COMMENT);
                            }

<BLOCK_COMMENT>[^*\n]+      { }
<BLOCK_COMMENT>"*"+[^*/\n]* { }
<BLOCK_COMMENT>\n           { }
<BLOCK_COMMENT>"*"+"/"      { BEGIN(INITIAL); }
<BLOCK_COMMENT><<EOF>>      {
                               std::fprintf(
                                   stderr,
                                   "%d:%d lexical error: unterminated block comment\n",
                                   g_comment_line,
                                   g_comment_column
                               );
                               return 1;
                            }

"const"                     { print_token("KW_CONST"); }
"int"                       { print_token("KW_INT"); }
"void"                      { print_token("KW_VOID"); }
"if"                        { print_token("KW_IF"); }
"else"                      { print_token("KW_ELSE"); }
"while"                     { print_token("KW_WHILE"); }
"break"                     { print_token("KW_BREAK"); }
"continue"                  { print_token("KW_CONTINUE"); }
"return"                    { print_token("KW_RETURN"); }

"=="                        { print_token("EQUAL"); }
"!="                        { print_token("NOT_EQUAL"); }
"<="                        { print_token("LESS_EQUAL"); }
">="                        { print_token("GREATER_EQUAL"); }
"&&"                        { print_token("AND_AND"); }
"||"                        { print_token("OR_OR"); }

"+"                         { print_token("PLUS"); }
"-"                         { print_token("MINUS"); }
"*"                         { print_token("STAR"); }
"/"                         { print_token("SLASH"); }
"%"                         { print_token("PERCENT"); }
"="                         { print_token("ASSIGN"); }
"<"                         { print_token("LESS"); }
">"                         { print_token("GREATER"); }
"!"                         { print_token("NOT"); }

";"                         { print_token("SEMICOLON"); }
","                         { print_token("COMMA"); }
"("                         { print_token("LPAREN"); }
")"                         { print_token("RPAREN"); }
"["                         { print_token("LBRACKET"); }
"]"                         { print_token("RBRACKET"); }
"{"                         { print_token("LBRACE"); }
"}"                         { print_token("RBRACE"); }

{HEX_INT}                   { print_token("INT_LITERAL"); }
0[xX]                       {
                               std::fprintf(
                                   stderr,
                                   "%d:%d lexical error: invalid hexadecimal literal\n",
                                   g_token_line,
                                   g_token_column
                               );
                               return 1;
                            }

{OCT_INT}                   { print_token("INT_LITERAL"); }
0[0-9]+                     {
                               std::fprintf(
                                   stderr,
                                   "%d:%d lexical error: invalid octal literal\n",
                                   g_token_line,
                                   g_token_column
                               );
                               return 1;
                            }

{DEC_INT}                   { print_token("INT_LITERAL"); }
{IDENT}                     { print_token("IDENTIFIER"); }

.                           {
                               std::fprintf(
                                   stderr,
                                   "%d:%d lexical error: unexpected character '%s'\n",
                                   g_token_line,
                                   g_token_column,
                                   yytext
                               );
                               return 1;
                            }

<<EOF>>                     {
                               std::printf("%d:%d %-14s\n", g_line, g_column, "EOF");
                               return 0;
                            }

%%

void reset_lexer_state(void) {
    g_line = 1;
    g_column = 1;
    g_token_line = 1;
    g_token_column = 1;
    g_comment_line = 1;
    g_comment_column = 1;
}
