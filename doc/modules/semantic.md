# Semantic Module

## Scope

The semantic module provides the first semantic-analysis skeleton after AST
lowering. It consumes the compiler-facing AST, installs builtin runtime
symbols, manages lexical scopes, and records semantic results in a separate
semantic model.

## Directory Layout

```text
src/frontend/semantic/
├── model/
│   ├── semantic_diagnostic.hpp
│   ├── semantic_diagnostic.cpp
│   ├── semantic_model.hpp
│   ├── semantic_model.cpp
│   ├── semantic_symbol.hpp
│   ├── semantic_symbol.cpp
│   ├── semantic_type.hpp
│   └── semantic_type.cpp
├── analysis/
│   ├── semantic_analyzer.hpp
│   ├── semantic_analyzer.cpp
│   ├── decl_analyzer.hpp
│   ├── decl_analyzer.cpp
│   ├── stmt_analyzer.hpp
│   ├── stmt_analyzer.cpp
│   ├── expr_analyzer.hpp
│   └── expr_analyzer.cpp
├── support/
│   ├── builtin_symbols.hpp
│   ├── builtin_symbols.cpp
│   ├── scope_stack.hpp
│   ├── scope_stack.cpp
│   ├── semantic_context.hpp
│   └── semantic_context.cpp
├── type_system/
│   ├── constant_evaluator.hpp
│   ├── constant_evaluator.cpp
│   ├── conversion_checker.hpp
│   ├── conversion_checker.cpp
│   ├── type_resolver.hpp
│   └── type_resolver.cpp
├── semantic.hpp
├── semantic_pass.hpp
├── semantic_pass.cpp
```

## Responsibilities

- expose `SemanticPass` as the only public pass entry for semantic analysis
- keep semantic data separate from AST node classes through `model/`
- install builtin runtime-library functions before user AST traversal
- manage nested lexical scopes through `support/`
- bind AST nodes to semantic symbols and semantic types
- separate orchestration, support infrastructure, and type-system helpers so
  the directory tree itself reflects semantic module dependencies

## Current Status

The current implementation has a first batch of real semantic rules:

- `SemanticPass` is connected to the main pipeline
- `analysis/SemanticAnalyzer` now acts as an orchestrator
- declaration rules live in `analysis/DeclAnalyzer`
- statement rules live in `analysis/StmtAnalyzer`
- expression and operator rules live in `analysis/ExprAnalyzer`
- type construction lives in `type_system/TypeResolver`
- type-compatibility and operand-category checks live in
  `type_system/ConversionChecker`
- foldable integer constant-expression queries live in
  `type_system/ConstantEvaluator`
- transient analysis state and builtin installation live in `support/`
- semantic output data structures live in `model/`
- builtin functions such as `getint`, `putint`, `putfloat`, `putch`,
  `starttime`, and `stoptime` are registered into the initial scope
- declaration analysis records symbols for functions, parameters, variables,
  constants, typedefs, structs, enums, and enumerators
- expression analysis records basic type bindings for identifiers, literals,
  unary/prefix/postfix expressions, calls, indexes, and member expressions
- semantic analysis now reports and rejects:
  - undefined identifiers
  - same-scope redefinitions
  - function-call arity mismatches
  - function-call argument type mismatches
  - calls to non-function objects
  - assignment type mismatches
  - assignments to non-assignable targets
  - non-void / void `return` mismatches
  - invalid arithmetic, bitwise, shift, logical, relational, and equality
    binary operands
  - non-scalar condition expressions
  - non-pointer index bases
  - non-integer array subscripts
  - non-constant array dimensions
  - integer-zero null pointer constants assigned or passed to pointer targets
  - array-to-pointer decay for pointer-compatible call and assignment checks
  - pointer arithmetic for `pointer +/- integer` and `pointer - pointer`
  - invalid operands for unary `&` and `*`
  - invalid operands for unary `+`, unary `-`, `!`, and `~`
  - invalid operands for prefix/postfix `++`
  - `break` outside loops or `switch`
  - `continue` outside loops
  - `case` / `default` labels outside `switch`
  - duplicate `case` labels within one `switch`
  - multiple `default` labels within one `switch`
  - non-constant `case` labels
  - non-constant integer `const` initializers
  - non-constant enumerator values
  - invalid `.` / `->` use on unsupported base types
  - access to missing struct members through `.` / `->`
  - non-void functions whose current body may exit without returning a value
- semantic pass success now depends on whether any `Error` diagnostics were
  produced during AST traversal
- integer constant-expression queries now fall back to recursive AST evaluation
  for character literals and unary/binary operator trees
- if the current AST is not complete yet, `SemanticPass` stores a semantic
  model but skips strict rule checking so incomplete lowering does not block
  ordinary frontend smoke tests

## Output

- semantic results are stored in
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  as a `SemanticModel`
- `SemanticModel` stores:
  - success state
  - diagnostics
  - AST-node-to-type bindings
  - AST-node-to-symbol bindings
  - integer constant-expression values for foldable AST nodes
  - owned semantic types and symbols

## Notes

- This module is intentionally kept separate from AST so the AST classes remain
  structural and semantic results stay replaceable.
- The current analyzer is still intentionally small; it does not yet cover full
  implicit-conversion rules, floating-point constant folding, or broader
  control-flow diagnostics beyond the current duplicate-`case`/missing-return
  checks.
- `ExprAnalyzer` now receives both `.` and `->` member expressions from the
  parser/AST pipeline and applies the same struct-member lookup rules to each
  operator.
- The recent refactor keeps `SemanticAnalyzer` thin so future semantic work can
  continue in specialized helpers instead of growing one class indefinitely.
