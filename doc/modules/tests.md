# Tests Module

## Scope

The tests module stores SysY22 input files and helper scripts used for quick
local verification.

## Main Layout

Each test now lives in its own subdirectory (`tests/<name>/`) containing the `.sy` input and an executable `run.sh`. The top-level `run_all.sh` iterates over these folders.

## Main Files

- [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh)
- [tests/array_init](/Users/caojunze424/code/SysyCC/tests/array_init) (run.sh + array_init.sy)
- [tests/comment_preprocess](/Users/caojunze424/code/SysyCC/tests/comment_preprocess)
- [tests/comment_literals](/Users/caojunze424/code/SysyCC/tests/comment_literals)
- [tests/c_parser_extensions](/Users/caojunze424/code/SysyCC/tests/c_parser_extensions)
- [tests/control_flow](/Users/caojunze424/code/SysyCC/tests/control_flow)
- [tests/elif](/Users/caojunze424/code/SysyCC/tests/elif)
- [tests/function_call](/Users/caojunze424/code/SysyCC/tests/function_call)
- [tests/function_macro](/Users/caojunze424/code/SysyCC/tests/function_macro)
- [tests/include_local](/Users/caojunze424/code/SysyCC/tests/include_local)
- [tests/include_path](/Users/caojunze424/code/SysyCC/tests/include_path)
- [tests/conditional_expr](/Users/caojunze424/code/SysyCC/tests/conditional_expr)
- [tests/preprocess_nested_conditionals](/Users/caojunze424/code/SysyCC/tests/preprocess_nested_conditionals)
- [tests/lexer_operator_mix](/Users/caojunze424/code/SysyCC/tests/lexer_operator_mix)
- [tests/literal_formats](/Users/caojunze424/code/SysyCC/tests/literal_formats)
- [tests/macro_literal_expansion_bug](/Users/caojunze424/code/SysyCC/tests/macro_literal_expansion_bug) (targeted reproducer)
- [tests/function_macro_argument_literal_bug](/Users/caojunze424/code/SysyCC/tests/function_macro_argument_literal_bug) (targeted reproducer)
- [tests/include_cycle_bug](/Users/caojunze424/code/SysyCC/tests/include_cycle_bug) (targeted reproducer)
- [tests/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/invalid_token_diagnostic) (targeted lexer diagnostic check)
- [tests/invalid_macro_name_bug](/Users/caojunze424/code/SysyCC/tests/invalid_macro_name_bug) (targeted reproducer)
- [tests/lexer_global_state_bug](/Users/caojunze424/code/SysyCC/tests/lexer_global_state_bug) (static structure check)
- [tests/lexer_parse_node_mode_guard](/Users/caojunze424/code/SysyCC/tests/lexer_parse_node_mode_guard) (static lexer/parser separation check)
- [tests/minimal](/Users/caojunze424/code/SysyCC/tests/minimal)
- [tests/empty_token_stream_behavior](/Users/caojunze424/code/SysyCC/tests/empty_token_stream_behavior) (targeted lexer empty-stream check)
- [tests/precise_token_kinds](/Users/caojunze424/code/SysyCC/tests/precise_token_kinds)
- [tests/ast_minimal](/Users/caojunze424/code/SysyCC/tests/ast_minimal)
- [tests/ast_multiple_functions](/Users/caojunze424/code/SysyCC/tests/ast_multiple_functions)
- [tests/ast_source_span](/Users/caojunze424/code/SysyCC/tests/ast_source_span)
- [tests/ast_type_decls](/Users/caojunze424/code/SysyCC/tests/ast_type_decls)
- [tests/ast_unknown_expr_preservation](/Users/caojunze424/code/SysyCC/tests/ast_unknown_expr_preservation)
- [tests/ast_unary_literals](/Users/caojunze424/code/SysyCC/tests/ast_unary_literals)
- [tests/ast_float_return_type](/Users/caojunze424/code/SysyCC/tests/ast_float_return_type)
- [tests/ast_function_call](/Users/caojunze424/code/SysyCC/tests/ast_function_call)
- [tests/ast_member_access](/Users/caojunze424/code/SysyCC/tests/ast_member_access)
- [tests/ast_nested_init_list](/Users/caojunze424/code/SysyCC/tests/ast_nested_init_list)
- [tests/ast_pointer_types](/Users/caojunze424/code/SysyCC/tests/ast_pointer_types)
- [tests/ast_stmt_extensions](/Users/caojunze424/code/SysyCC/tests/ast_stmt_extensions)
- [tests/ast_control_flow](/Users/caojunze424/code/SysyCC/tests/ast_control_flow)
- [tests/ast_top_level_decls](/Users/caojunze424/code/SysyCC/tests/ast_top_level_decls)
- [tests/ast_unknown_guard](/Users/caojunze424/code/SysyCC/tests/ast_unknown_guard) (targeted AST completeness check)
- [tests/ast_unknown_expr](/Users/caojunze424/code/SysyCC/tests/ast_unknown_expr)
- [tests/ast_void_return](/Users/caojunze424/code/SysyCC/tests/ast_void_return)
- [tests/preprocess_dispatch_sentinel_bug](/Users/caojunze424/code/SysyCC/tests/preprocess_dispatch_sentinel_bug) (static structure check)
- [tests/ifdef](/Users/caojunze424/code/SysyCC/tests/ifdef)
- [tests/ifndef](/Users/caojunze424/code/SysyCC/tests/ifndef)
- [tests/if0](/Users/caojunze424/code/SysyCC/tests/if0)
- [tests/if1](/Users/caojunze424/code/SysyCC/tests/if1)
- [tests/preprocess_define](/Users/caojunze424/code/SysyCC/tests/preprocess_define)
- [tests/preprocess_undef](/Users/caojunze424/code/SysyCC/tests/preprocess_undef)
- [tests/stringify_include](/Users/caojunze424/code/SysyCC/tests/stringify_include)
- [tests/token_paste](/Users/caojunze424/code/SysyCC/tests/token_paste)
- [tests/run_all.sh](/Users/caojunze424/code/SysyCC/tests/run_all.sh)

## Responsibilities

- provide minimal valid SysY22 source examples arranged per directory (`tests/<name>/`)
- cover functions, control flow, arrays, literal formats, literal-aware comment stripping, preprocessing, nested conditional directives, object and function-like macros, stringification, token pasting, local includes, include search paths, expression-based `#if` branches, precise operator token kinds, broader C-style parser extensions, and AST lowering checks for nested init lists, source spans, pointer declarators, `->` member access, and completeness guarding
- support one-click local runs via each folder’s `run.sh`
- serve as smoke tests during development and validate intermediate outputs
- keep focused bug reproducers and structure checks runnable through the same top-level regression entry

## Current Test Flow

Each test directory provides a `run.sh` script that will:

- configure the project
- build the project
- run `SysyCC` on its matching `*.sy` file
- perform self-checks on its expected artifacts or diagnostics

The shared helper script [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh) provides common build and artifact assertions for ordinary success-path tests.

`run_all.sh` will execute every test directory under `tests/` that contains an executable `run.sh`.
After the full run, it also prints a Markdown-style summary table and writes the
same report to `build/test_result.md`.

For ordinary success-path tests, `run_all.sh` also validates that each test produced:

- `build/intermediate_results/<test_name>.preprocessed.sy`
- `build/intermediate_results/<test_name>.tokens.txt`
- `build/intermediate_results/<test_name>.parse.txt`

For targeted failure-diagnostic, empty-stream, AST-completeness, or static-structure checks such as [tests/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/invalid_token_diagnostic), [tests/empty_token_stream_behavior](/Users/caojunze424/code/SysyCC/tests/empty_token_stream_behavior), [tests/ast_unknown_guard](/Users/caojunze424/code/SysyCC/tests/ast_unknown_guard), [tests/lexer_global_state_bug](/Users/caojunze424/code/SysyCC/tests/lexer_global_state_bug), and [tests/preprocess_dispatch_sentinel_bug](/Users/caojunze424/code/SysyCC/tests/preprocess_dispatch_sentinel_bug), `run_all.sh` still executes the test but treats the assertions inside each test's own `run.sh` as the source of truth and does not require non-empty intermediate result files.

The generated summary table records, for every executed test:

- test name
- final status (`PASS` / `FAIL`)
- a short detail column describing artifact validation or dedicated assertions
