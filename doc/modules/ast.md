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
- `EnumeratorDecl`
- `EnumDecl`
- `TypedefDecl`
- `BuiltinTypeNode`
- `PointerTypeNode`
- `StructTypeNode`
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
- [tests/ast/ast_unknown_expr/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_expr/run.sh)
- [tests/ast/ast_function_call/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_function_call/run.sh)
- [tests/ast/ast_nested_init_list/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_nested_init_list/run.sh)
- [tests/ast/ast_pointer_types/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_types/run.sh)
- [tests/ast/ast_member_access/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_member_access/run.sh)
- [tests/ast/ast_source_span/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_source_span/run.sh)
- [tests/ast/ast_type_decls/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_type_decls/run.sh)
- [tests/ast/ast_top_level_decls/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_top_level_decls/run.sh)
- [tests/ast/ast_unknown_expr_preservation/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_expr_preservation/run.sh)
- [tests/ast/ast_unknown_guard/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_unknown_guard/run.sh)
- [tests/ast/ast_void_return/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_void_return/run.sh)
- [tests/ast/ast_control_flow/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_control_flow/run.sh)
- [tests/ast/ast_stmt_extensions/run.sh](/Users/caojunze424/code/SysyCC/tests/ast/ast_stmt_extensions/run.sh)
