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

static int get_keyword_parser_token(sysycc::TokenKind kind) {
    switch (kind) {
    case sysycc::TokenKind::KwConst:
        return CONST;
    case sysycc::TokenKind::KwVolatile:
        return VOLATILE;
    case sysycc::TokenKind::KwExtern:
        return EXTERN;
    case sysycc::TokenKind::KwStatic:
        return STATIC;
    case sysycc::TokenKind::KwAttribute:
        return ATTRIBUTE;
    case sysycc::TokenKind::KwAsm:
        return ASM;
    case sysycc::TokenKind::KwInline:
        return INLINE;
    case sysycc::TokenKind::KwRestrict:
        return RESTRICT;
    case sysycc::TokenKind::KwNullability:
        return NULLABILITY;
    case sysycc::TokenKind::KwLong:
        return LONG;
    case sysycc::TokenKind::KwSigned:
        return SIGNED;
    case sysycc::TokenKind::KwShort:
        return SHORT;
    case sysycc::TokenKind::KwUnsigned:
        return UNSIGNED;
    case sysycc::TokenKind::KwInt:
        return INT;
    case sysycc::TokenKind::KwChar:
        return CHAR;
    case sysycc::TokenKind::KwVoid:
        return VOID;
    case sysycc::TokenKind::KwFloat:
        return FLOAT;
    case sysycc::TokenKind::KwDouble:
        return DOUBLE;
    case sysycc::TokenKind::KwFloat16:
        return FLOAT16;
    case sysycc::TokenKind::KwIf:
        return IF;
    case sysycc::TokenKind::KwElse:
        return ELSE;
    case sysycc::TokenKind::KwWhile:
        return WHILE;
    case sysycc::TokenKind::KwFor:
        return FOR;
    case sysycc::TokenKind::KwDo:
        return DO;
    case sysycc::TokenKind::KwSwitch:
        return SWITCH;
    case sysycc::TokenKind::KwCase:
        return CASE;
    case sysycc::TokenKind::KwDefault:
        return DEFAULT;
    case sysycc::TokenKind::KwBreak:
        return BREAK;
    case sysycc::TokenKind::KwContinue:
        return CONTINUE;
    case sysycc::TokenKind::KwGoto:
        return GOTO;
    case sysycc::TokenKind::KwReturn:
        return RETURN;
    case sysycc::TokenKind::KwStruct:
        return STRUCT;
    case sysycc::TokenKind::KwUnion:
        return UNION;
    case sysycc::TokenKind::KwEnum:
        return ENUM;
    case sysycc::TokenKind::KwTypedef:
        return TYPEDEF;
    case sysycc::TokenKind::KwSizeof:
        return SIZEOF;
    default:
        return IDENTIFIER;
    }
}

static const char *get_keyword_token_label(int token) {
    switch (token) {
    case CONST:
        return "CONST";
    case VOLATILE:
        return "VOLATILE";
    case EXTERN:
        return "EXTERN";
    case STATIC:
        return "STATIC";
    case ATTRIBUTE:
        return "ATTRIBUTE";
    case ASM:
        return "ASM";
    case INLINE:
        return "INLINE";
    case RESTRICT:
        return "RESTRICT";
    case NULLABILITY:
        return "NULLABILITY";
    case LONG:
        return "LONG";
    case SIGNED:
        return "SIGNED";
    case SHORT:
        return "SHORT";
    case UNSIGNED:
        return "UNSIGNED";
    case INT:
        return "INT";
    case CHAR:
        return "CHAR";
    case VOID:
        return "VOID";
    case FLOAT:
        return "FLOAT";
    case DOUBLE:
        return "DOUBLE";
    case FLOAT16:
        return "FLOAT16";
    case IF:
        return "IF";
    case ELSE:
        return "ELSE";
    case WHILE:
        return "WHILE";
    case FOR:
        return "FOR";
    case DO:
        return "DO";
    case SWITCH:
        return "SWITCH";
    case CASE:
        return "CASE";
    case DEFAULT:
        return "DEFAULT";
    case BREAK:
        return "BREAK";
    case CONTINUE:
        return "CONTINUE";
    case GOTO:
        return "GOTO";
    case RETURN:
        return "RETURN";
    case STRUCT:
        return "STRUCT";
    case UNION:
        return "UNION";
    case ENUM:
        return "ENUM";
    case TYPEDEF:
        return "TYPEDEF";
    case SIZEOF:
        return "SIZEOF";
    case ALIGNAS:
        return "ALIGNAS";
    default:
        return "IDENTIFIER";
    }
}

#define YY_USER_ACTION update_position((sysycc::LexerState *)yyextra, yytext, yyleng);
%}

DIGIT       [0-9]
NONZERO     [1-9]
OCT_DIGIT   [0-7]
HEX_DIGIT   [0-9a-fA-F]
LETTER      [A-Za-z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*
INT_SUFFIX  ([uU]([lL]{1,2})?|([lL]{1,2})[uU]?)
DEC_INT     0|{NONZERO}{DIGIT}*
OCT_INT     0{OCT_DIGIT}+
HEX_INT     0[xX]{HEX_DIGIT}+
DEC_FLOAT   ({DIGIT}+"."{DIGIT}*|{DIGIT}*"."{DIGIT}+)([eE][+-]?{DIGIT}+)?
SCI_FLOAT   {DIGIT}+[eE][+-]?{DIGIT}+
FLOAT_SUFFIX [fFlL]
HEX_FLOAT   0[xX](({HEX_DIGIT}+"."{HEX_DIGIT}*|{HEX_DIGIT}*"."{HEX_DIGIT}+|{HEX_DIGIT}+)[pP][+-]?{DIGIT}+)
FLOAT_CONST ({DEC_FLOAT}|{SCI_FLOAT}|{HEX_FLOAT}){FLOAT_SUFFIX}?
ESCAPE      \\(['"\\abfnrtv?]|[0-7]{1,3}|x{HEX_DIGIT}+)
CHAR_BODY   ([^'\\\n]|{ESCAPE})
CHAR_CONST  \'{CHAR_BODY}\'
STRING_BODY ([^"\\\n]|{ESCAPE})*
STRING_CONST \"{STRING_BODY}\"
INVALID_OCT 0{OCT_DIGIT}*[89]{DIGIT}*
INVALID_HEX 0[xX]({LETTER}|{DIGIT})*
ANNOT_IDENT _LIBC_[A-Z0-9_]*

%%
[ \t\r\f\v]+                { }
\n                          { }

"++"                        { return make_token(INC, "INC", (sysycc::LexerState *)yyextra, yytext, yylval); }
"--"                        { return make_token(DEC, "DEC", (sysycc::LexerState *)yyextra, yytext, yylval); }
"->"                        { return make_token(ARROW, "ARROW", (sysycc::LexerState *)yyextra, yytext, yylval); }
"..."                       { return make_token(ELLIPSIS, "ELLIPSIS", (sysycc::LexerState *)yyextra, yytext, yylval); }
"=="                        { return make_token(EQ, "EQ", (sysycc::LexerState *)yyextra, yytext, yylval); }
"!="                        { return make_token(NE, "NE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<="                        { return make_token(LE, "LE", (sysycc::LexerState *)yyextra, yytext, yylval); }
">="                        { return make_token(GE, "GE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"&&"                        { return make_token(AND, "AND", (sysycc::LexerState *)yyextra, yytext, yylval); }
"||"                        { return make_token(OR, "OR", (sysycc::LexerState *)yyextra, yytext, yylval); }
"+="                        { return make_token(ADD_ASSIGN, "ADD_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"-="                        { return make_token(SUB_ASSIGN, "SUB_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"*="                        { return make_token(MUL_ASSIGN, "MUL_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"/="                        { return make_token(DIV_ASSIGN, "DIV_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"%="                        { return make_token(MOD_ASSIGN, "MOD_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<<="                       { return make_token(SHL_ASSIGN, "SHL_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
">>="                       { return make_token(SHR_ASSIGN, "SHR_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"&="                        { return make_token(AND_ASSIGN, "AND_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"^="                        { return make_token(XOR_ASSIGN, "XOR_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"|="                        { return make_token(OR_ASSIGN, "OR_ASSIGN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"<<"                        { return make_token(SHL, "SHL", (sysycc::LexerState *)yyextra, yytext, yylval); }
">>"                        { return make_token(SHR, "SHR", (sysycc::LexerState *)yyextra, yytext, yylval); }

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
"?"                         { return make_token(QUESTION, "QUESTION", (sysycc::LexerState *)yyextra, yytext, yylval); }
":"                         { return make_token(COLON, "COLON", (sysycc::LexerState *)yyextra, yytext, yylval); }
"."                         { return make_token(DOT, "DOT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"("                         { return make_token(LPAREN, "LPAREN", (sysycc::LexerState *)yyextra, yytext, yylval); }
")"                         { return make_token(RPAREN, "RPAREN", (sysycc::LexerState *)yyextra, yytext, yylval); }
"["                         { return make_token(LBRACKET, "LBRACKET", (sysycc::LexerState *)yyextra, yytext, yylval); }
"]"                         { return make_token(RBRACKET, "RBRACKET", (sysycc::LexerState *)yyextra, yytext, yylval); }
"{"                         { return make_token(LBRACE, "LBRACE", (sysycc::LexerState *)yyextra, yytext, yylval); }
"}"                         { return make_token(RBRACE, "RBRACE", (sysycc::LexerState *)yyextra, yytext, yylval); }

"restrict"                  { return make_token(RESTRICT, "RESTRICT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"__restrict"                { return make_token(RESTRICT, "RESTRICT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"__restrict__"              { return make_token(RESTRICT, "RESTRICT", (sysycc::LexerState *)yyextra, yytext, yylval); }
"_Nullable"                 { return make_token(NULLABILITY, "NULLABILITY", (sysycc::LexerState *)yyextra, yytext, yylval); }
"_Nonnull"                  { return make_token(NULLABILITY, "NULLABILITY", (sysycc::LexerState *)yyextra, yytext, yylval); }
"_Null_unspecified"         { return make_token(NULLABILITY, "NULLABILITY", (sysycc::LexerState *)yyextra, yytext, yylval); }
"__const"                   { return make_token(CONST, "CONST", (sysycc::LexerState *)yyextra, yytext, yylval); }
"__const__"                 { return make_token(CONST, "CONST", (sysycc::LexerState *)yyextra, yytext, yylval); }
"_Alignas"                  { return make_token(ALIGNAS, "ALIGNAS", (sysycc::LexerState *)yyextra, yytext, yylval); }

{STRING_CONST}              { return make_token(STRING_LITERAL, "STRING_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{CHAR_CONST}                { return make_token(CHAR_LITERAL, "CHAR_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{FLOAT_CONST}               { return make_token(FLOAT_LITERAL, "FLOAT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{HEX_INT}{INT_SUFFIX}?      { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{OCT_INT}{INT_SUFFIX}?      { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{DEC_INT}{INT_SUFFIX}?      { return make_token(INT_LITERAL, "INT_LITERAL", (sysycc::LexerState *)yyextra, yytext, yylval); }
{INVALID_HEX}               { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }
{INVALID_OCT}               { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }
{ANNOT_IDENT}               { return make_token(ANNOTATION_IDENT, "ANNOTATION_IDENT", (sysycc::LexerState *)yyextra, yytext, yylval); }
{IDENT}                     {
                              sysycc::LexerState *state = (sysycc::LexerState *)yyextra;
                              if (state != nullptr &&
                                  state->get_classify_typedef_names() &&
                                  sysycc::is_typedef_name_registered(yytext)) {
                                  return make_token(TYPE_NAME, "TYPE_NAME",
                                                    state, yytext, yylval);
                              }
                              const sysycc::TokenKind kind =
                                  sysycc::classify_identifier_kind(*state, yytext);
                              const int token = get_keyword_parser_token(kind);
                              return make_token(token, get_keyword_token_label(token),
                                                state, yytext, yylval);
                            }

.                           { return make_token(INVALID, "INVALID", (sysycc::LexerState *)yyextra, yytext, yylval); }

%%
