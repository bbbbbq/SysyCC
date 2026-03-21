# Tests Module

## Scope

The tests module stores stage-oriented SysY22 test cases plus the shared helper
scripts used by local regression runs.

## Main Layout

Tests are now grouped by pipeline stage:

```text
tests/
├── fuzz/
│   └── generate_and_build_csmith_cases.sh
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
- targeted directive-diagnostic failures such as malformed `#include`,
  unterminated conditionals, invalid `#line`, and malformed
  `__has_include(...)`
- long-form integrated scenarios that combine macro pipelines, layered include
  graphs, and multi-branch feature-probe conditions in one translation unit
- focused bug reproducers and structure checks

Representative paths:

- [tests/preprocess/comment_preprocess](/Users/caojunze424/code/SysyCC/tests/preprocess/comment_preprocess)
- [tests/preprocess/comma_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/comma_conditional_expr)
- [tests/preprocess/function_macro](/Users/caojunze424/code/SysyCC/tests/preprocess/function_macro)
- [tests/preprocess/multiline_macro_define](/Users/caojunze424/code/SysyCC/tests/preprocess/multiline_macro_define)
- [tests/preprocess/bitwise_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/bitwise_conditional_expr)
- [tests/preprocess/clang_builtin_probe_condition](/Users/caojunze424/code/SysyCC/tests/preprocess/clang_builtin_probe_condition)
- [tests/preprocess/elifdef](/Users/caojunze424/code/SysyCC/tests/preprocess/elifdef)
- [tests/preprocess/elifndef](/Users/caojunze424/code/SysyCC/tests/preprocess/elifndef)
- [tests/preprocess/error_directive](/Users/caojunze424/code/SysyCC/tests/preprocess/error_directive)
- [tests/preprocess/define_invalid_parameter_name](/Users/caojunze424/code/SysyCC/tests/preprocess/define_invalid_parameter_name)
- [tests/preprocess/define_duplicate_parameter_name](/Users/caojunze424/code/SysyCC/tests/preprocess/define_duplicate_parameter_name)
- [tests/preprocess/define_variadic_parameter_position](/Users/caojunze424/code/SysyCC/tests/preprocess/define_variadic_parameter_position)
- [tests/preprocess/function_like_macro_if_identifier](/Users/caojunze424/code/SysyCC/tests/preprocess/function_like_macro_if_identifier)
- [tests/preprocess/has_include_false_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_false_branch)
- [tests/preprocess/has_cpp_attribute_condition](/Users/caojunze424/code/SysyCC/tests/preprocess/has_cpp_attribute_condition)
- [tests/preprocess/has_include_malformed](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_malformed)
- [tests/preprocess/has_include_next_true_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_next_true_branch)
- [tests/preprocess/has_include_true_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_true_branch)
- [tests/preprocess/integer_suffix_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/integer_suffix_conditional_expr)
- [tests/preprocess/include_empty_path](/Users/caojunze424/code/SysyCC/tests/preprocess/include_empty_path)
- [tests/preprocess/include_next](/Users/caojunze424/code/SysyCC/tests/preprocess/include_next)
- [tests/preprocess/missing_endif](/Users/caojunze424/code/SysyCC/tests/preprocess/missing_endif)
- [tests/preprocess/elifdef_missing_condition](/Users/caojunze424/code/SysyCC/tests/preprocess/elifdef_missing_condition)
- [tests/preprocess/line_directive](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive)
- [tests/preprocess/line_directive_logical_location](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_logical_location)
- [tests/preprocess/line_directive_missing_number](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_missing_number)
- [tests/preprocess/long_macro_pipeline](/Users/caojunze424/code/SysyCC/tests/preprocess/long_macro_pipeline)
- [tests/preprocess/long_include_graph](/Users/caojunze424/code/SysyCC/tests/preprocess/long_include_graph)
- [tests/preprocess/long_condition_matrix](/Users/caojunze424/code/SysyCC/tests/preprocess/long_condition_matrix)
- [tests/preprocess/preprocess_error_location](/Users/caojunze424/code/SysyCC/tests/preprocess/preprocess_error_location)
- [tests/preprocess/pragma_once](/Users/caojunze424/code/SysyCC/tests/preprocess/pragma_once)
- [tests/preprocess/spaced_directive_keywords](/Users/caojunze424/code/SysyCC/tests/preprocess/spaced_directive_keywords)
- [tests/preprocess/ternary_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/ternary_conditional_expr)
- [tests/preprocess/variadic_macro](/Users/caojunze424/code/SysyCC/tests/preprocess/variadic_macro)
- [tests/preprocess/warning_directive](/Users/caojunze424/code/SysyCC/tests/preprocess/warning_directive)
- [tests/preprocess/include_path](/Users/caojunze424/code/SysyCC/tests/preprocess/include_path)
- [tests/preprocess/system_include_iso646](/Users/caojunze424/code/SysyCC/tests/preprocess/system_include_iso646)
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
- [tests/lexer/c_token_kinds](/Users/caojunze424/code/SysyCC/tests/lexer/c_token_kinds)
- [tests/lexer/lexer_operator_mix](/Users/caojunze424/code/SysyCC/tests/lexer/lexer_operator_mix)
- [tests/lexer/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/lexer/invalid_token_diagnostic)
- [tests/lexer/line_directive_logical_location](/Users/caojunze424/code/SysyCC/tests/lexer/line_directive_logical_location)

### `tests/parser/`

Parser-oriented frontend smoke tests, including:

- minimal whole-pipeline inputs
- control-flow parsing
- function calls
- parser-extension acceptance
- member-access parsing

Representative paths:

- [tests/parser/minimal](/Users/caojunze424/code/SysyCC/tests/parser/minimal)
- [tests/parser/if_else](/Users/caojunze424/code/SysyCC/tests/parser/if_else)
- [tests/parser/empty_statement](/Users/caojunze424/code/SysyCC/tests/parser/empty_statement)
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
- [tests/ast/ast_dot_member_access](/Users/caojunze424/code/SysyCC/tests/ast/ast_dot_member_access)
- [tests/ast/ast_index_expr](/Users/caojunze424/code/SysyCC/tests/ast/ast_index_expr)
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
- [tests/semantic/semantic_const_initializer_constant](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_initializer_constant)
- [tests/semantic/semantic_dot_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_dot_type)
- [tests/semantic/semantic_array_decay_assignment](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_array_decay_assignment)
- [tests/semantic/semantic_call_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_call_type)
- [tests/semantic/semantic_duplicate_case](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_duplicate_case)
- [tests/semantic/semantic_pointer_arithmetic](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_arithmetic)
- [tests/semantic/semantic_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_source_file)
- [tests/semantic/semantic_logical_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_logical_source_file)

### `tests/run/`

End-to-end execution regressions that compile generated LLVM IR with a local
runtime stub, feed stdin, and compare stdout, including:

- integer input via `getint`
- integer output via `putint`
- character output via `putch`
- arithmetic loops such as `for` and `do-while`
- `switch/case/default` dispatch
- short-circuit boolean control flow with runtime-visible behavior
- fixed-size scalarized data-structure scenarios such as:
  - stack / queue / deque
  - ring buffer
  - linked-list-style cursor traversal
  - BST-style lookup
  - priority queue ordering
  - sorted list insertion
  - set membership
  - key-to-value map lookup
- runtime-style link validation for emitted LLVM `declare` statements

Representative paths:

- [tests/run/run_echo_int](/Users/caojunze424/code/SysyCC/tests/run/run_echo_int)
- [tests/run/run_sum_two_ints](/Users/caojunze424/code/SysyCC/tests/run/run_sum_two_ints)
- [tests/run/run_factorial_for](/Users/caojunze424/code/SysyCC/tests/run/run_factorial_for)
- [tests/run/run_switch_dispatch](/Users/caojunze424/code/SysyCC/tests/run/run_switch_dispatch)
- [tests/run/run_short_circuit_guard](/Users/caojunze424/code/SysyCC/tests/run/run_short_circuit_guard)
- [tests/run/run_do_while_sum](/Users/caojunze424/code/SysyCC/tests/run/run_do_while_sum)
- [tests/run/run_stack_lifo](/Users/caojunze424/code/SysyCC/tests/run/run_stack_lifo)
- [tests/run/run_queue_fifo](/Users/caojunze424/code/SysyCC/tests/run/run_queue_fifo)
- [tests/run/run_ring_buffer_wrap](/Users/caojunze424/code/SysyCC/tests/run/run_ring_buffer_wrap)
- [tests/run/run_deque_pop_ends](/Users/caojunze424/code/SysyCC/tests/run/run_deque_pop_ends)
- [tests/run/run_linked_list_sum](/Users/caojunze424/code/SysyCC/tests/run/run_linked_list_sum)
- [tests/run/run_bst_search](/Users/caojunze424/code/SysyCC/tests/run/run_bst_search)
- [tests/run/run_priority_queue_pop](/Users/caojunze424/code/SysyCC/tests/run/run_priority_queue_pop)
- [tests/run/run_sorted_list_insert](/Users/caojunze424/code/SysyCC/tests/run/run_sorted_list_insert)
- [tests/run/run_set_membership](/Users/caojunze424/code/SysyCC/tests/run/run_set_membership)
- [tests/run/run_map_lookup](/Users/caojunze424/code/SysyCC/tests/run/run_map_lookup)
- [tests/run/support/runtime_stub.c](/Users/caojunze424/code/SysyCC/tests/run/support/runtime_stub.c)

Each runtime case also maintains its own `build/` directory under
`tests/run/<case>/build/`, where the case stores copied frontend artifacts,
the emitted LLVM IR used for host compilation, and the final linked executable.

### `tests/fuzz/`

Fuzz-input generation helpers that are not part of the ordinary regression
matrix. The current scripts are:

- [tests/fuzz/generate_and_build_csmith_cases.sh](/Users/caojunze424/code/SysyCC/tests/fuzz/generate_and_build_csmith_cases.sh)
- [tests/fuzz/run_csmith_cases.sh](/Users/caojunze424/code/SysyCC/tests/fuzz/run_csmith_cases.sh)

The script accepts either:

- `<count>`
- `--generate-only <count>`

and creates numbered case directories such as `tests/fuzz/001/`,
`tests/fuzz/002/`, and so on.

By default it invokes the locally built Csmith binary at
`tools/csmith/build/src/csmith`, generates one C source file named
`fuzz_<id>.c` per directory, and then compiles each generated file with `clang`,
automatically adding:

- `-I tools/csmith/runtime`
- `-I tools/csmith/build/runtime`

so the generated source can resolve `#include "csmith.h"` and the runtime
headers it transitively includes. Each numbered directory then contains:

- `fuzz_<id>.c`
- `fuzz_<id>.out`

When `--generate-only` is used, the same numbered directories are created but
only `fuzz_<id>.c` is generated.

Example with generation only:

```bash
bash tests/fuzz/generate_and_build_csmith_cases.sh --generate-only 3
```

This produces:

```text
tests/fuzz/001/fuzz_001.c
tests/fuzz/002/fuzz_002.c
tests/fuzz/003/fuzz_003.c
```

Example with generation and compilation:

```bash
bash tests/fuzz/generate_and_build_csmith_cases.sh 2
```

This produces:

```text
tests/fuzz/001/fuzz_001.c
tests/fuzz/001/fuzz_001.out
tests/fuzz/002/fuzz_002.c
tests/fuzz/002/fuzz_002.out
```

Generated numbered directories under `tests/fuzz/` are ignored by Git so the
script can be used as a local fuzz-input workspace.

`run_csmith_cases.sh` executes generated fuzz cases as a differential test
between the host toolchain and `SysyCC`, then archives all intermediate results
per numbered directory. It supports:

- `all` to run every numbered case directory under `tests/fuzz/`
- one or more specific case ids such as `001` or `1 4 7`

For each requested case, the script:

1. compiles `fuzz_<id>.c` with `clang`
2. runs the `clang` binary with the case-local input file if present, or an
   empty input file otherwise
3. invokes `SysyCC` with `--dump-ir`
4. if `SysyCC` succeeds, compiles the emitted LLVM IR with `clang`
5. runs the `SysyCC`-produced executable with the same input
6. compares stdout, stderr, and exit code

If `SysyCC` compilation fails, the script keeps the compiler error output in the
same case directory and records the failure in a top-level summary file:

- [tests/fuzz/result.md](/Users/caojunze424/code/SysyCC/tests/fuzz/result.md)

Each case directory then accumulates files such as:

- `fuzz_<id>.clang.compile.stdout.txt`
- `fuzz_<id>.clang.compile.stderr.txt`
- `fuzz_<id>.clang.stdout.txt`
- `fuzz_<id>.clang.stderr.txt`
- `fuzz_<id>.clang.exit.txt`
- `fuzz_<id>.sysycc.compile.stdout.txt`
- `fuzz_<id>.sysycc.compile.stderr.txt`
- `fuzz_<id>.sysycc.compile.exit.txt`
- `fuzz_<id>.ll`
- `fuzz_<id>.sysycc.link.stdout.txt`
- `fuzz_<id>.sysycc.link.stderr.txt`
- `fuzz_<id>.sysycc.link.exit.txt`
- `fuzz_<id>.sysycc.stdout.txt`
- `fuzz_<id>.sysycc.stderr.txt`
- `fuzz_<id>.sysycc.exit.txt`
- `fuzz_<id>.compare.txt`

This makes it possible to inspect both compiler paths and the final output
comparison for one specific numbered case directory.

Example:

```bash
bash tests/fuzz/run_csmith_cases.sh 001
bash tests/fuzz/run_csmith_cases.sh all
```

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
- [tests/ir/ir_modulo](/Users/caojunze424/code/SysyCC/tests/ir/ir_modulo)
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
Their per-case `build/` directories provide a stable place to inspect the
runtime-focused intermediate files after a test run.
