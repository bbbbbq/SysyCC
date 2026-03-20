# Tests Module

## Scope

The tests module stores stage-oriented SysY22 test cases plus the shared helper
scripts used by local regression runs.

## Main Layout

Tests are now grouped by pipeline stage:

```text
tests/
├── ast/
│   └── <case>/
├── ir/
│   └── <case>/
├── lexer/
│   └── <case>/
├── parser/
│   └── <case>/
├── preprocess/
│   └── <case>/
├── run/
│   ├── support/
│   └── <case>/
├── semantic/
│   └── <case>/
├── run_all.sh
└── test_helpers.sh
```

Each concrete case lives under `tests/<stage>/<case>/` and contains:

- one or more `.sy` inputs
- an executable `run.sh`
- any stage-specific helper files such as local headers

## Stage Groups

### `tests/preprocess/`

Preprocessing regressions and reproducers, including:

- comment stripping
- object-like and function-like macros
- stringification and token pasting
- `#if/#ifdef/#ifndef/#elif/#else/#endif`
- include search paths and include-cycle handling
- focused bug reproducers and structure checks

Representative paths:

- [tests/preprocess/comment_preprocess](/Users/caojunze424/code/SysyCC/tests/preprocess/comment_preprocess)
- [tests/preprocess/function_macro](/Users/caojunze424/code/SysyCC/tests/preprocess/function_macro)
- [tests/preprocess/include_path](/Users/caojunze424/code/SysyCC/tests/preprocess/include_path)
- [tests/preprocess/preprocess_nested_conditionals](/Users/caojunze424/code/SysyCC/tests/preprocess/preprocess_nested_conditionals)

### `tests/lexer/`

Lexer-stage regressions and targeted diagnostics, including:

- exact token-kind dumps
- operator-mix tokenization
- literal formats
- invalid-token diagnostics
- scanner-structure checks

Representative paths:

- [tests/lexer/precise_token_kinds](/Users/caojunze424/code/SysyCC/tests/lexer/precise_token_kinds)
- [tests/lexer/lexer_operator_mix](/Users/caojunze424/code/SysyCC/tests/lexer/lexer_operator_mix)
- [tests/lexer/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/lexer/invalid_token_diagnostic)

### `tests/parser/`

Parser-oriented frontend smoke tests, including:

- minimal whole-pipeline inputs
- control-flow parsing
- function calls
- parser-extension acceptance
- member-access parsing

Representative paths:

- [tests/parser/minimal](/Users/caojunze424/code/SysyCC/tests/parser/minimal)
- [tests/parser/control_flow](/Users/caojunze424/code/SysyCC/tests/parser/control_flow)
- [tests/parser/c_parser_extensions](/Users/caojunze424/code/SysyCC/tests/parser/c_parser_extensions)

### `tests/ast/`

AST lowering regressions, including:

- basic translation-unit lowering
- declaration and type lowering
- initializer-list preservation
- source spans
- pointer/member access
- completeness guards
- control-flow lowering

Representative paths:

- [tests/ast/ast_minimal](/Users/caojunze424/code/SysyCC/tests/ast/ast_minimal)
- [tests/ast/ast_pointer_types](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_types)
- [tests/ast/ast_source_span](/Users/caojunze424/code/SysyCC/tests/ast/ast_source_span)
- [tests/ast/ast_stmt_extensions](/Users/caojunze424/code/SysyCC/tests/ast/ast_stmt_extensions)

### `tests/semantic/`

Semantic-analysis regressions, including:

- undefined identifiers and redefinitions
- call arity and call-type checking
- assignment/lvalue rules
- unary/binary operand constraints
- member-access validation
- switch/case/default checks
- constant-expression checks
- pointer arithmetic and array decay

Representative paths:

- [tests/semantic/semantic_undefined_identifier](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_undefined_identifier)
- [tests/semantic/semantic_call_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_call_type)
- [tests/semantic/semantic_duplicate_case](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_duplicate_case)
- [tests/semantic/semantic_pointer_arithmetic](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_arithmetic)

### `tests/run/`

End-to-end execution regressions that compile generated LLVM IR with a local
runtime stub, feed stdin, and compare stdout, including:

- integer input via `getint`
- integer output via `putint`
- character output via `putch`
- runtime-style link validation for emitted LLVM `declare` statements

Representative paths:

- [tests/run/run_echo_int](/Users/caojunze424/code/SysyCC/tests/run/run_echo_int)
- [tests/run/run_sum_two_ints](/Users/caojunze424/code/SysyCC/tests/run/run_sum_two_ints)
- [tests/run/support/runtime_stub.c](/Users/caojunze424/code/SysyCC/tests/run/support/runtime_stub.c)

### `tests/ir/`

LLVM IR lowering regressions, including:

- minimal returns
- locals, loads, stores, assignments
- arithmetic and comparisons
- short-circuit logical control flow
- `if` / `while` / `for` / `do-while`
- `switch/case/default`
- `break` / `continue`

Representative paths:

- [tests/ir/ir_minimal](/Users/caojunze424/code/SysyCC/tests/ir/ir_minimal)
- [tests/ir/ir_function_call](/Users/caojunze424/code/SysyCC/tests/ir/ir_function_call)
- [tests/ir/ir_short_circuit](/Users/caojunze424/code/SysyCC/tests/ir/ir_short_circuit)
- [tests/ir/ir_switch](/Users/caojunze424/code/SysyCC/tests/ir/ir_switch)

## Shared Helpers

- [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh)
  provides common build, artifact, link, and runtime-output assertions for
  success-path tests.
- [tests/run_all.sh](/Users/caojunze424/code/SysyCC/tests/run_all.sh)
  recursively discovers every executable `tests/<stage>/<case>/run.sh`,
  executes them, and writes a Markdown summary to `build/test_result.md`.

## Artifact Checks

For ordinary success-path tests, `run_all.sh` still validates that each case
produced:

- `build/intermediate_results/<case>.preprocessed.sy`
- `build/intermediate_results/<case>.tokens.txt`
- `build/intermediate_results/<case>.parse.txt`

Targeted failure-diagnostic, AST-completeness, or static-structure cases remain
self-validating through their own `run.sh`; `run_all.sh` records those as
`verified via dedicated run.sh assertions`.

The same dedicated-assertion path is also used for `tests/run/`, because those
cases validate executable stdout instead of only checking intermediate artifacts.
