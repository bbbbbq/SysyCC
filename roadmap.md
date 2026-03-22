# SysyCC Roadmap

## Purpose

This file records the current language support status of the SysyCC front-end.
It focuses on what syntax is already implemented and what is still missing.

## Project Direction

- `SysyCC` is intended to support both `SysY22` and `C` language workflows.
- Near-term implementation choices should prefer shared infrastructure that can
  serve both targets, instead of hard-coding the project around only one
  language subset.
- Subsequent feature work should follow test-driven development by adding or
  updating focused tests before implementation and regression verification.

## Current Pipeline

```text
source file
-> preprocess
-> lexer
-> parser
-> ast
-> semantic
-> ir
```

## IR

### Implemented

- modular IR generation stage after semantic analysis
- backend-independent `IRBackend` interface
- initial LLVM IR backend
- textual LLVM IR dump output to `build/intermediate_results/*.ll`
- top-level LLVM `declare` emission for builtin runtime-style external calls
- lowering for
  - integer and void functions
  - integer parameters and integer local variables
  - integer literals, identifiers, assignments, arithmetic, comparisons, and
    short-circuit logical expressions
  - direct function calls
  - `if`
  - `while`
  - `for`
  - `do-while`
  - `switch/case/default`
  - `break`
  - `continue`
  - pointer-aware lowering for member access, address-of, dereference, index
    expressions, and pointer arithmetic in the current supported subset
  - floating scalar lowering for `float`, `double`, `_Float16`, and
    `long double` in the current supported subset

### Not Implemented

- array lowering
- runtime-library call lowering coverage beyond the current direct-call subset
- `.ll -> .s` / object-file / linker driver pipeline

## Not Fully End-to-End Yet

This section tracks syntax that is already accepted somewhere in the
front-end, but is still not fully implemented from preprocess through IR.

### Front-End Supported, IR Not Fully Supported

- richer integer-type lowering outside the current tested subset
  - `long int`
  - `long long int`
  - `unsigned int`
  - `unsigned long long`

### Compatibility-Accepted, Semantics Still Incomplete

- non-`once` `#pragma`
  - recognized and ignored
  - not fully implemented semantically
- `#line`
  - logical file/line remapping exists
  - full industrial-strength source mapping is still incomplete
- GNU attributes beyond the current supported subset
  - only a small subset is semantically meaningful today
  - the rest are preserved or rejected for compatibility
- full qualifier system
  - `const char *`-style cases have real support
  - complete C qualifier semantics are still incomplete
- extension builtin types
  - `_Float16` now has front-end, semantic, IR cast/arithmetic/comparison
    coverage, and hexadecimal floating-literal lowering in the current
    supported scalar subset
  - fuller standard-library integration and wider backend coverage are still
    incomplete
- richer floating/runtime integration
  - `long double` and `_Float16` now have cast, arithmetic, comparison,
    branch-truthiness, and hexadecimal floating-literal lowering in the
    current IR subset
  - fuller target-specific runtime and ABI coverage are still incomplete
- internal `ptrdiff_t` modeling
  - pointer difference now uses an internal `ptrdiff_t`-style semantic result
  - parser-level spelling and fuller standard-library integration are still
    incomplete

### Highest-Value Next Steps

1. richer integer-type lowering
   - `long int`
   - `long long int`
   - `unsigned int`
   - `unsigned long long`
2. richer aggregate/object lowering
   - arrays
   - more complete global object coverage
3. fuller floating/runtime coverage beyond the current scalar cast/arithmetic subset

## Preprocess

### Implemented

- supported directive syntax
  - `#define NAME value`
  - `#define ADD(a, b) ((a) + (b))`
  - `#define LOG(...) __VA_ARGS__`
  - `#error message`
  - `#line 123`
  - `#line 123 "file.h"`
  - `#pragma once`
  - `#pragma anything-else`
  - `#undef NAME`
  - `#include "file.h"`
  - `#include <file.h>`
  - `#include_next <file.h>`
  - `#ifdef NAME`
  - `#ifndef NAME`
  - `#if expr`
  - `#elif expr`
  - `#elifdef NAME`
  - `#elifndef NAME`
  - `#else`
  - `#endif`
- object-like macros
  - `#define NAME value`
  - `#undef NAME`
- function-like macros
  - `#define ADD(a, b) ((a) + (b))`
  - `#define LOG(...) __VA_ARGS__`
  - multi-line macro definitions using trailing `\`
  - fixed-arity parameter substitution
  - variadic parameter substitution through `...` and `__VA_ARGS__`
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
- system include
  - `#include <file.h>`
  - search default system include directories
  - quoted includes fall back to system include directories after local and
    user-provided include directories
  - `#include_next <file.h>` continues from the next matching system include
    directory after the current header
- macro expansion syntax
  - ordinary object-like replacement
  - fixed-arity function-like replacement
  - nested expansion in ordinary source lines
  - `#param` stringification
  - `lhs ## rhs` token pasting
- conditional compilation
  - `#ifdef`
  - `#ifndef`
  - `#if`
  - `#elif`
  - `#elifdef`
  - `#elifndef`
  - `#else`
  - `#endif`
- simple constant expressions in `#if` and `#elif`
  - integer literals
  - macro identifiers
  - `defined(NAME)`
  - unary operators: `!`, `~`, unary `+`, unary `-`
  - arithmetic and shift operators: `*`, `/`, `%`, `+`, `-`, `<<`, `>>`
  - bitwise operators: `&`, `^`, `|`
  - relational operators: `<`, `<=`, `>`, `>=`
  - equality operators: `==`, `!=`
  - logical operators: `&&`, `||`
  - ternary `?:`
  - comma operator
  - parenthesized expressions
  - `__has_include(...)`
  - `__has_include_next(...)`

### Not Implemented

- unsupported directive syntax
- complete C preprocessor compatibility
- macro continuation syntax
  - variadic continuation macros with trailing `\`
- unsupported condition syntax in `#if/#elif`
  - full `defined` / builtin probing compatibility beyond the current minimal
    support
- full downstream source-location remapping for accepted `#line` directives
- pragma-specific semantics beyond `#pragma once`
- comment-preserving location mapping across preprocess and lexer stages

### Recommended Implementation Order

For the next preprocess-compatibility stage, the recommended implementation
order is:

1. full `defined` / builtin probing compatibility beyond the current minimal support
2. ternary conditional operator in `#if`
   - `?:`
3. comma operator in `#if`

This order prioritizes features that most often block real system-header and
`csmith` preprocessing before lower-frequency or more specialized preprocessor
syntax.

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
  - `. ->`
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
  - invalid `.` / `->` base types
  - missing struct members accessed through `.` / `->`
  - non-void functions that may exit without returning a value
  - recursive integer constant-expression evaluation for character literals and unary/binary operator trees

### Not Implemented

- more complete implicit conversion and usual arithmetic conversion rules
- floating-point constant folding
- full pointer arithmetic rules
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
  - postfix `[]`, `()`, `.`, `->`, `++`, `--`
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
- pointer-to-member combinations beyond basic `->`
- struct / union grammar beyond current `struct` support

## Near-Term Priorities

1. Consume `-I` paths for more include forms, especially `#include <...>`.
2. Improve preprocess diagnostics with clearer file and line reporting.
3. Expand AST lowering coverage for the remaining parser-accepted constructs.
4. Deepen semantic analysis around control flow, conversions, and constant evaluation.
5. Start preparing the post-semantic pipeline for IR generation.
