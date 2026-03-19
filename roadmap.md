# SysyCC Roadmap

## Purpose

This file records the current language support status of the SysyCC front-end.
It focuses on what syntax is already implemented and what is still missing.

## Current Pipeline

```text
source file
-> preprocess
-> lexer
-> parser
-> ast
```

## Preprocess

### Implemented

- object-like macros
  - `#define NAME value`
  - `#undef NAME`
- function-like macros
  - `#define ADD(a, b) ((a) + (b))`
  - fixed-arity parameter substitution
  - nested macro invocation expansion
  - stringification operator `#`
  - token pasting operator `##`
- comment stripping
  - `// ...`
  - `/* ... */`
  - preserves string and character literal contents while removing comments
- local include
  - `#include "file.h"`
  - search current file directory first
  - search CLI `-I` include directories next
- conditional compilation
  - `#ifdef`
  - `#ifndef`
  - `#if`
  - `#elif`
  - `#else`
  - `#endif`
- simple constant expressions in `#if` and `#elif`
  - integer literals
  - macro identifiers
  - `defined(NAME)`
  - unary operators: `!`, unary `+`, unary `-`
  - arithmetic operators: `*`, `/`, `%`, `+`, `-`
  - relational operators: `<`, `<=`, `>`, `>=`
  - equality operators: `==`, `!=`
  - logical operators: `&&`, `||`
  - parenthesized expressions

### Not Implemented

- `#include <file.h>`
- system header default search paths
- complete C preprocessor compatibility
- variadic macros
- multi-line macro definitions using trailing `\`
- `#error`
- `#pragma`
- `#line`
- `#elifdef` / `#elifndef`
- include guards / once optimization semantics
- comment-preserving location mapping across preprocess and lexer stages

## Lexer

### Implemented

- keywords
  - `const`
  - `int`
  - `void`
  - `float`
  - `if`
  - `else`
  - `while`
  - `for`
  - `do`
  - `switch`
  - `case`
  - `default`
  - `break`
  - `continue`
  - `return`
  - `struct`
  - `enum`
  - `typedef`
- identifiers
- integer literals
  - decimal
  - octal
  - hexadecimal
- floating-point literals
- character literals
- string literals
- operators
  - `+ - * / %`
  - `++ --`
  - `& | ^ ~`
  - `<< >>`
  - `->`
  - `= == != < <= > >=`
  - `! && ||`
- punctuators
  - `; , : ( ) [ ] { }`
- invalid token detection for
  - malformed integer forms
  - unknown characters
- token source span tracking
- exact token kind storage for downstream passes and token dumps

## AST

- initial AST lowering pass
- parser tree to AST translation rooted at `TranslationUnit`
- currently lowered nodes:
  - `FunctionDecl`
  - `ParamDecl`
  - `VarDecl`
  - `ConstDecl`
  - `BlockStmt`
  - `DeclStmt`
  - `ExprStmt`
  - `IfStmt`
  - `WhileStmt`
  - `ContinueStmt`
  - `ReturnStmt`
  - `IntegerLiteralExpr`
  - `IdentifierExpr`
  - `BinaryExpr`
  - `AssignExpr`
  - `CallExpr`
  - `IndexExpr`
  - `InitListExpr`
- AST dump output to `build/intermediate_results/*.ast.txt`

### Not Implemented

- dedicated lexer tests for newly recognized C-style tokens beyond the current parser grammar

## Parser

### Implemented

- compilation unit with mixed declarations and function definitions
- declarations
  - `const int`
  - `int`
  - `float`
  - `typedef`
  - `struct` declarations and definitions
  - `enum` declarations and definitions
- variable and constant definitions
  - scalar definitions
  - array definitions
  - pointer declarators
  - initializer lists
- function definitions
  - `int` return type
  - `void` return type
  - `float` return type
  - empty parameter list
  - normal parameters
  - array parameters
  - pointer parameters
- blocks and block items
- statements
  - assignment
  - expression statement
  - empty statement
  - nested block
  - `if`
  - `if ... else`
  - `while`
  - `for`
  - `do ... while`
  - `switch`
  - `case`
  - `default`
  - `break`
  - `continue`
  - `return;`
  - `return expr;`
- expressions
  - identifier primary expressions
  - integer primary expressions
  - float / char / string primary expressions
  - function calls
  - postfix `[]`, `()`, `->`, `++`, `--`
  - unary `+ - ! ~ & *`
  - prefix `++ --`
  - multiplicative `* / %`
  - additive `+ -`
  - shift `<< >>`
  - bitwise `& ^ |`
  - relational `< <= > >=`
  - equality `== !=`
  - logical `&& ||`
- parse tree generation and dump output

### Not Implemented

- AST generation as a separate semantic tree
- semantic analysis
- type checking
- symbol table checks
- constant folding in the compiler core
- comma operator expressions
- ternary operator
  - `?:`
- declarations based on user-defined types
- user-defined type names in later declarations after `typedef`
- direct member access with `.`
- pointer-to-member combinations beyond basic `->`
- struct / union grammar beyond current `struct` support

## Near-Term Priorities

1. Consume `-I` paths for more include forms, especially `#include <...>`.
2. Improve preprocess diagnostics with clearer file and line reporting.
3. Expand AST lowering coverage for more statements, expressions, and type forms.
4. Add a `SemanticPass`.
5. Expand SysY22 coverage only where it matches the project target language.
