# AST Module

## Scope

The AST module lowers the parser runtime tree into a cleaner compiler-facing AST
and provides a dump utility for inspecting the lowered structure.

## Directory Layout

```text
src/frontend/ast/
├── ast.hpp
├── ast_kind.hpp
├── ast_node.hpp
├── ast_node.cpp
├── ast_dump.hpp
├── ast_dump.cpp
├── ast_pass.hpp
├── ast_pass.cpp
└── detail/
    ├── ast_builder.hpp
    ├── ast_builder.cpp
    ├── ast_builder_context.hpp
    ├── ast_builder_context.cpp
    ├── parse_tree_matcher.hpp
    └── parse_tree_matcher.cpp
```

## Responsibilities

- keep AST construction separate from parser-runtime details
- lower the parse tree into a smaller AST rooted at `TranslationUnit`
- provide a stable layer for later semantic analysis to consume compiler-facing
  nodes instead of grammar-shaped parse trees
- write AST dumps to `build/intermediate_results/*.ast.txt` when `--dump-ast`
  is enabled

## Key Files

- [ast_pass.hpp](/Users/caojunze424/code/SysyCC/src/frontend/ast/ast_pass.hpp)
- [ast_node.hpp](/Users/caojunze424/code/SysyCC/src/frontend/ast/ast_node.hpp)
- [ast_dump.hpp](/Users/caojunze424/code/SysyCC/src/frontend/ast/ast_dump.hpp)
- [ast_builder.hpp](/Users/caojunze424/code/SysyCC/src/frontend/ast/detail/ast_builder.hpp)

## Output Artifacts

- AST root stored in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- AST completeness state stored in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp) via `get_ast_complete()`
- optional AST dump files in `build/intermediate_results`
  - AST dumps now include `SourceSpan` lines for lowered nodes when location
    data is available

## Current Lowering Coverage

The current first-pass AST builder recognizes:

- `TranslationUnit`
- `FunctionDecl`
- `ParamDecl`
- `FieldDecl`
- `VarDecl`
- `ConstDecl`
- `StructDecl`
- `UnionDecl`
- `EnumeratorDecl`
- `EnumDecl`
- `TypedefDecl`
- `BuiltinTypeNode`
- `QualifiedTypeNode`
- `PointerTypeNode`
- `StructTypeNode`
- `UnionTypeNode`
- `EnumTypeNode`
- `BlockStmt`
- `DeclStmt`
- `ExprStmt`
- `IfStmt`
- `WhileStmt`
- `DoWhileStmt`
- `ForStmt`
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
- `CastExpr`
- `ConditionalExpr`
- `AssignExpr`
- `CallExpr`
- `IndexExpr`
- `MemberExpr`
- `InitListExpr`

Nodes that are not lowered yet are preserved as placeholders:

- `UnknownDecl`
- `UnknownStmt`
- `UnknownExpr`
- `UnknownTypeNode`

`AstPass` now treats those `Unknown*` placeholders as an incomplete lowering
result:

- it records the completeness state on `CompilerContext`
- it still writes the AST dump for inspection
- it returns failure when `--dump-ast` explicitly asks for AST output from an
  incomplete lowering

Function lowering also preserves declaration-only prototypes:

- `FunctionDecl::get_body()` may be `nullptr` when a top-level function ends
  with `;`
- declaration-only prototypes now include both `extern` and `inline` forms
- `FunctionDecl` nodes now preserve parsed GNU attribute lists from
  declaration-specifier position
- `FunctionDecl` nodes now also preserve optional GNU asm-label suffixes used
  by system-header alias declarations
- `FunctionDecl` nodes now preserve whether the parsed signature is variadic so
  later semantic and IR stages can distinguish fixed-arity and variadic
  functions
- unnamed prototype parameters remain `ParamDecl` nodes with an empty internal
  name and appear as `<unnamed>` in AST dumps
- unnamed pointer prototype parameters lower through `PointerTypeNode`
  parameter types without introducing synthetic parameter names
- unnamed typedef-name prototype parameters such as `size_t` lower through the
  same empty-name `ParamDecl` path
- simple parameter-side `const` qualifiers now lower as `QualifiedTypeNode`
  wrapped under the pointee side of `PointerTypeNode`, so `const char *`
  preserves qualifier structure in the AST
- parameter-side and declaration-side `volatile` qualifiers now lower through
  the same `QualifiedTypeNode` path, so forms such as `volatile int *` and
  `const volatile int` preserve their qualifier structure instead of being
  erased during AST construction
- pointer-side qualifiers such as `restrict`, `__restrict`, and
  `__restrict__` now lower onto `PointerTypeNode` itself, so
  `const char * __restrict name` preserves both the pointee-side `const`
  and the pointer-side `restrict`
- pointer-side `volatile` now also lowers onto `PointerTypeNode` itself, so
  `volatile int * volatile p` preserves both the pointee-side and pointer-side
  qualifier placement in AST dumps and downstream semantic analysis
- pointer-side nullability annotations such as `_Nullable`, `_Nonnull`, and
  `_Null_unspecified` now also lower onto `PointerTypeNode` itself, so
  pointer annotations can be preserved in the AST and passed through semantic
  analysis before being erased by IR lowering
- Darwin-style pointer annotation spellings such as `_LIBC_COUNT(__n)` and
  `_LIBC_CSTR` are accepted in declarators and function-pointer parameter
  shapes, and bare pointer-side annotation qualifiers such as
  `_LIBC_UNSAFE_INDEXABLE` are accepted through the same path, without
  changing the lowered pointee type structure
- builtin declaration lowering now includes `long double`
- builtin declaration lowering now includes `_Float16`
- builtin declaration lowering now also includes `long int`
- builtin declaration lowering now also includes `long long int`
- builtin declaration lowering now also includes `unsigned long`
- builtin declaration lowering now also includes `signed char`, `short`,
  `unsigned char`, and `unsigned short`
- declaration lowering now also preserves `union` declarations and inline
  anonymous union type nodes used directly in local declarations
- builtin declaration lowering now includes `unsigned int` and
  `unsigned long long`
- declaration lowering also accepts `extern` variable declarations and lowers
  them through the same `VarDecl` node family
- declaration lowering also preserves top-level `static` on both `VarDecl`
  and `FunctionDecl`, so later semantic and IR stages can distinguish
  internal-linkage declarations from ordinary external-linkage declarations
- GNU qualifier spellings such as `__const` and `__const__` lower through the
  same `Const` / `QualifiedTypeNode` path as ordinary `const`
- GNU qualifier spellings such as `__volatile` and `__volatile__` lower
  through the same `QualifiedTypeNode` / `PointerTypeNode` qualifier path as
  ordinary `volatile`
- `VarDecl` now preserves whether the parsed declaration carried `extern`, so
  later semantic and IR stages can distinguish global/external storage from
  ordinary local storage
- expression lowering now includes C-style cast nodes with one target
  `TypeNode` and one operand expression
- cast-target lowering now also preserves pointer-target spellings such as
  `(int *)value` and qualifier-carrying target spellings such as
  `(const char *)buffer`
- named-type lowering now preserves parser-seeded bootstrap typedef spellings
  such as `size_t`, `ptrdiff_t`, and `va_list` through `NamedTypeNode` so
  later semantic resolution can bind them to builtin aliases
- declaration lowering now also preserves grouped function-pointer declarators
  such as `void (*routine)(void *)` as `PointerTypeNode` over
  `FunctionTypeNode`
- declaration lowering also accepts grouped ordinary function declarators such
  as `static int (safe_add)(int x, int y)` and still preserves the lowered
  `FunctionDecl` name as `safe_add`
- `FunctionTypeNode` now also preserves whether a declarator-level function
  type is variadic
- top-level function lowering now also preserves pointer-return prototypes
  such as `void *memchr(...)` by wrapping the function return type in
  `PointerTypeNode`
- declarator dimension collection now stops at the captured dimension
  expression, so nested subscripts inside macros such as `ARRAY_SIZE(x)` or
  `__typeof__(&(x)[0])` do not leak into the declared variable's array rank

## Notes

- The AST module is intentionally placed between parser output and future
  semantic analysis.
- The current builder targets the parser runtime tree produced by
  [parser_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.hpp).
- The first validation test for this module is
  [tests/ast/ast_minimal/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_minimal/run.sh).
- Additional baseline AST tests live in:
  - [tests/ast/ast_multiple_functions/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_multiple_functions/run.sh)
- [tests/ast/ast_float_return_type/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_float_return_type/run.sh)
- [tests/ast/ast_cast_expr/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_cast_expr/run.sh)
- [tests/ast/ast_double_type/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_double_type/run.sh)
- [tests/ast/ast_extern_variable_decl/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_extern_variable_decl/run.sh)
- [tests/ast/ast_float16_type/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_float16_type/run.sh)
- [tests/ast/ast_unknown_expr/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_expr/run.sh)
- [tests/ast/ast_function_call/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_function_call/run.sh)
- [tests/ast/ast_gnu_attribute_prototype/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_gnu_attribute_prototype/run.sh)
- [tests/ast/ast_inline_function_prototype/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_inline_function_prototype/run.sh)
- [tests/ast/ast_const_char_pointer_prototype/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_const_char_pointer_prototype/run.sh)
- [tests/ast/ast_pointer_target_cast_expr/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_target_cast_expr/run.sh)
- [tests/ast/ast_nested_init_list/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_nested_init_list/run.sh)
- [tests/ast/ast_unnamed_pointer_parameter_prototype/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unnamed_pointer_parameter_prototype/run.sh)
- [tests/ast/ast_pointer_types/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_types/run.sh)
- [tests/ast/ast_union_decl/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_union_decl/run.sh)
- [tests/ast/ast_signed_short_builtin_types/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_signed_short_builtin_types/run.sh)
- [tests/ast/ast_member_access/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_member_access/run.sh)
- [tests/ast/ast_source_span/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_source_span/run.sh)
- [tests/ast/ast_type_decls/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_type_decls/run.sh)
- [tests/ast/ast_top_level_decls/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_top_level_decls/run.sh)
- [tests/ast/ast_conditional_expr/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_conditional_expr/run.sh)
- [tests/ast/ast_unknown_expr_preservation/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_expr_preservation/run.sh)
- [tests/ast/ast_unknown_guard/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_guard/run.sh)
- [tests/ast/ast_void_return/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_void_return/run.sh)
- [tests/ast/ast_control_flow/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_control_flow/run.sh)
- [tests/ast/ast_stmt_extensions/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_stmt_extensions/run.sh)
