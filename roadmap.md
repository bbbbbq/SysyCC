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
-> semantic
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
  - `FieldDecl`
  - `StructDecl`
  - `EnumDecl`
  - `EnumeratorDecl`
  - `TypedefDecl`
  - `BlockStmt`
  - `DeclStmt`
  - `ExprStmt`
  - `IfStmt`
  - `WhileStmt`
  - `ForStmt`
  - `DoWhileStmt`
  - `SwitchStmt`
  - `CaseStmt`
  - `DefaultStmt`
  - `BreakStmt`
  - `ContinueStmt`
  - `ReturnStmt`
  - `IntegerLiteralExpr`
  - `FloatLiteralExpr`
  - `CharLiteralExpr`
  - `StringLiteralExpr`
  - `IdentifierExpr`
  - `UnaryExpr`
  - `PrefixExpr`
  - `PostfixExpr`
  - `BinaryExpr`
  - `AssignExpr`
  - `CallExpr`
  - `IndexExpr`
  - `MemberExpr`
  - `InitListExpr`
  - `BuiltinTypeNode`
  - `PointerTypeNode`
  - `StructTypeNode`
  - `EnumTypeNode`
- AST source span propagation from parse tree
- AST completeness tracking in `CompilerContext`
- AST dump output to `build/intermediate_results/*.ast.txt`

### Not Implemented

- dedicated lexer tests for newly recognized C-style tokens beyond the current parser grammar
- fully complete AST lowering for all parser-accepted constructs
- comma operator expressions
- ternary operator
  - `?:`
- direct member access with `.`
- pointer-to-member combinations beyond basic `->`
- richer declarator/type lowering beyond the current pointer and array forms

## Semantic

### Implemented

- semantic pass integrated after AST lowering
- builtin runtime-library symbol installation
- lexical scope stack and symbol registration for
  - functions
  - parameters
  - variables
  - constants
  - typedef names
  - struct names
  - enum names
  - enumerators
- AST-node-to-type and AST-node-to-symbol bindings
- first integer constant-expression value tracking in `SemanticModel`
- semantic diagnostics for
  - undefined identifiers
  - same-scope redefinitions
  - function-call arity mismatches
  - function-call argument type mismatches
  - calls to non-function objects
  - assignment type mismatches
  - assignments to non-assignable targets
  - `return` type mismatches
  - invalid arithmetic, bitwise, shift, logical, relational, and equality operands
  - non-scalar condition expressions
  - non-pointer / non-array index bases
  - non-integer array subscripts
  - non-constant array dimensions
  - invalid operands for unary `&`, `*`, `+`, `-`, `!`, `~`
  - invalid operands for prefix/postfix `++`
  - invalid `break` / `continue` placement
  - invalid `case` / `default` placement
  - duplicate `case` labels within one `switch`
  - multiple `default` labels within one `switch`
  - non-constant `case` labels
  - non-constant integer `const` initializers
  - non-constant enumerator values
  - integer-zero null pointer constants assigned or passed to pointer targets
  - array-to-pointer decay for pointer-compatible call and assignment checks
  - pointer arithmetic for `pointer +/- integer` and `pointer - pointer`
  - invalid `->` base types
  - missing struct members accessed through `->`
  - non-void functions that may exit without returning a value
  - recursive integer constant-expression evaluation for character literals and unary/binary operator trees

### Not Implemented

- more complete implicit conversion and usual arithmetic conversion rules
- floating-point constant folding
- full pointer arithmetic rules
- direct member access with `.`
- full argument passing rules for arrays and pointer decay
- full lvalue / modifiable-lvalue rules
- function declaration compatibility and redeclaration checks
- unreachable-code and control-flow diagnostics
- enum value auto-increment rules
- complete constant-expression evaluation for all semantic contexts
- constant-expression range / overflow diagnostics
- struct initialization semantic checks
- array initializer shape and bounds checks
- builtin runtime-library signature coverage beyond the current set
- semantic output dumping for inspection

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
3. Expand AST lowering coverage for the remaining parser-accepted constructs.
4. Deepen semantic analysis around control flow, conversions, and constant evaluation.
5. Start preparing the post-semantic pipeline for IR generation.
