# Tests Module

## Scope

The tests module stores SysY22 input files and helper scripts used for quick
local verification.

## Main Layout

Each test now lives in its own subdirectory (`tests/<name>/`) containing the `.sy` input and an executable `run.sh`. The top-level `run_all.sh` iterates over these folders.

## Main Files

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
- [tests/literal_formats](/Users/caojunze424/code/SysyCC/tests/literal_formats)
- [tests/macro_literal_expansion_bug](/Users/caojunze424/code/SysyCC/tests/macro_literal_expansion_bug) (targeted reproducer, not part of `run_all.sh`)
- [tests/function_macro_argument_literal_bug](/Users/caojunze424/code/SysyCC/tests/function_macro_argument_literal_bug) (targeted reproducer, not part of `run_all.sh`)
- [tests/include_cycle_bug](/Users/caojunze424/code/SysyCC/tests/include_cycle_bug) (targeted reproducer, not part of `run_all.sh`)
- [tests/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/invalid_token_diagnostic) (targeted lexer diagnostic check, not part of `run_all.sh`)
- [tests/invalid_macro_name_bug](/Users/caojunze424/code/SysyCC/tests/invalid_macro_name_bug) (targeted reproducer, not part of `run_all.sh`)
- [tests/lexer_global_state_bug](/Users/caojunze424/code/SysyCC/tests/lexer_global_state_bug) (static structure check, not part of `run_all.sh`)
- [tests/lexer_parse_node_mode_guard](/Users/caojunze424/code/SysyCC/tests/lexer_parse_node_mode_guard) (static lexer/parser separation check, not part of `run_all.sh`)
- [tests/minimal](/Users/caojunze424/code/SysyCC/tests/minimal)
- [tests/empty_token_stream_behavior](/Users/caojunze424/code/SysyCC/tests/empty_token_stream_behavior) (targeted lexer empty-stream check, not part of `run_all.sh`)
- [tests/precise_token_kinds](/Users/caojunze424/code/SysyCC/tests/precise_token_kinds)
- [tests/preprocess_dispatch_sentinel_bug](/Users/caojunze424/code/SysyCC/tests/preprocess_dispatch_sentinel_bug) (static structure check, not part of `run_all.sh`)
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
- cover functions, control flow, arrays, literal formats, literal-aware comment stripping, preprocessing, object and function-like macros, stringification, token pasting, local includes, include search paths, conditional directives, expression-based `#if` branches, and broader C-style parser extensions
- support one-click local runs via each folder’s `run.sh`
- serve as smoke tests during development and validate intermediate outputs
- keep focused bug reproducers and structure checks runnable through the same top-level regression entry

## Current Test Flow

Each test directory provides a `run.sh` script that will:

- configure the project
- build the project
- run `SysyCC` on its matching `*.sy` file

`run_all.sh` will execute every test directory under `tests/` that contains an executable `run.sh`.

For ordinary success-path tests, `run_all.sh` also validates that each test produced:

- `build/intermediate_results/<test_name>.preprocessed.sy`
- `build/intermediate_results/<test_name>.tokens.txt`
- `build/intermediate_results/<test_name>.parse.txt`

For targeted failure-diagnostic, empty-stream, or static-structure checks such as [tests/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/invalid_token_diagnostic), [tests/empty_token_stream_behavior](/Users/caojunze424/code/SysyCC/tests/empty_token_stream_behavior), [tests/lexer_global_state_bug](/Users/caojunze424/code/SysyCC/tests/lexer_global_state_bug), and [tests/preprocess_dispatch_sentinel_bug](/Users/caojunze424/code/SysyCC/tests/preprocess_dispatch_sentinel_bug), `run_all.sh` treats the assertions inside each test's own `run.sh` as the source of truth and does not require non-empty intermediate result files.
