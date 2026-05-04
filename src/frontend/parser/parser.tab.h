/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison GLR parsers in C

   Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     INVALID = 258,
     CONST = 259,
     VOLATILE = 260,
     EXTERN = 261,
     STATIC = 262,
     REGISTER = 263,
     ATTRIBUTE = 264,
     ASM = 265,
     EXTENSION = 266,
     INLINE = 267,
     NORETURN = 268,
     RESTRICT = 269,
     ATOMIC = 270,
     NULLABILITY = 271,
     LONG = 272,
     SIGNED = 273,
     SHORT = 274,
     UNSIGNED = 275,
     INT = 276,
     CHAR = 277,
     VOID = 278,
     FLOAT = 279,
     DOUBLE = 280,
     FLOAT16 = 281,
     IF = 282,
     ELSE = 283,
     WHILE = 284,
     FOR = 285,
     DO = 286,
     SWITCH = 287,
     CASE = 288,
     DEFAULT = 289,
     BREAK = 290,
     CONTINUE = 291,
     GOTO = 292,
     RETURN = 293,
     STRUCT = 294,
     UNION = 295,
     ENUM = 296,
     TYPEDEF = 297,
     SIZEOF = 298,
     TYPEOF = 299,
     ALIGNAS = 300,
     TYPE_NAME = 301,
     IDENTIFIER = 302,
     ANNOTATION_IDENT = 303,
     INT_LITERAL = 304,
     FLOAT_LITERAL = 305,
     CHAR_LITERAL = 306,
     STRING_LITERAL = 307,
     PLUS = 308,
     MINUS = 309,
     MUL = 310,
     DIV = 311,
     MOD = 312,
     INC = 313,
     DEC = 314,
     BITAND = 315,
     BITOR = 316,
     BITXOR = 317,
     BITNOT = 318,
     SHL = 319,
     SHR = 320,
     ARROW = 321,
     ASSIGN = 322,
     ADD_ASSIGN = 323,
     SUB_ASSIGN = 324,
     MUL_ASSIGN = 325,
     DIV_ASSIGN = 326,
     MOD_ASSIGN = 327,
     SHL_ASSIGN = 328,
     SHR_ASSIGN = 329,
     AND_ASSIGN = 330,
     XOR_ASSIGN = 331,
     OR_ASSIGN = 332,
     EQ = 333,
     NE = 334,
     LT = 335,
     LE = 336,
     GT = 337,
     GE = 338,
     NOT = 339,
     AND = 340,
     OR = 341,
     ELLIPSIS = 342,
     QUESTION = 343,
     SEMICOLON = 344,
     COMMA = 345,
     COLON = 346,
     DOT = 347,
     LPAREN = 348,
     RPAREN = 349,
     LBRACKET = 350,
     RBRACKET = 351,
     LBRACE = 352,
     RBRACE = 353,
     LOWER_THAN_ELSE = 354,
     ANNOTATION_BARE = 355
   };
#endif


/* Copy the first part of user declarations.  */
#line 1 "/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.y"

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


#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE 
#line 22 "/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.y"
{
    void *node;
}
/* Line 2616 of glr.c.  */
#line 174 "/Users/caojunze424/code/SysyCC/build/generated/frontend/parser/parser.tab.h"
	YYSTYPE;
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{

  char yydummy;

} YYLTYPE;
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


extern YYSTYPE yylval;



