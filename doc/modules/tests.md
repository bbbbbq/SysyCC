# Tests Module

## Scope

The tests module stores stage-oriented SysY22 test cases plus the shared helper
scripts used by local regression runs.

## Main Layout

Tests are now grouped by pipeline stage:

```text
tests/
├── dialects/
│   └── <case>/
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

### `tests/dialects/`

Lightweight architecture regressions for the shared dialect skeleton, including:

- default dialect registration aggregation
- preprocess/stage registry visibility
- preprocess directive/probe/attribute/builtin-type/IR handler ownership
  visibility
- explicit keyword-conflict recording instead of silent overwrite
- runtime lexer keyword classification through the shared keyword registry
- runtime parser feature gating over parsed syntax
- runtime AST feature gating over lowered nodes
- runtime preprocess feature gating over predefined macros and builtin probes
- compiler fail-fast behavior for invalid dialect registration state
- strict-C99 dialect configuration
- optional dialect-pack switch behavior
- CLI-to-dialect-option mapping

Representative paths:

- [tests/dialects/default_dialect_registry](/Users/caojunze424/code/SysyCC/tests/dialects/default_dialect_registry)
- [tests/dialects/handler_registry_conflict_policy](/Users/caojunze424/code/SysyCC/tests/dialects/handler_registry_conflict_policy)
- [tests/dialects/lexer_keyword_conflict_policy](/Users/caojunze424/code/SysyCC/tests/dialects/lexer_keyword_conflict_policy)
- [tests/dialects/lexer_keyword_runtime_classification](/Users/caojunze424/code/SysyCC/tests/dialects/lexer_keyword_runtime_classification)
- [tests/dialects/parser_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/parser_feature_runtime_policy)
- [tests/dialects/ast_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/ast_feature_runtime_policy)
- [tests/dialects/semantic_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/semantic_feature_runtime_policy)
- [tests/dialects/preprocess_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/preprocess_feature_runtime_policy)
- [tests/dialects/dialect_registration_fail_fast](/Users/caojunze424/code/SysyCC/tests/dialects/dialect_registration_fail_fast)
- [tests/dialects/strict_c99_dialect_configuration](/Users/caojunze424/code/SysyCC/tests/dialects/strict_c99_dialect_configuration)
- [tests/dialects/optional_dialect_pack_switches](/Users/caojunze424/code/SysyCC/tests/dialects/optional_dialect_pack_switches)
- [tests/dialects/cli_dialect_option_mapping](/Users/caojunze424/code/SysyCC/tests/dialects/cli_dialect_option_mapping)

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
- [tests/preprocess/error_directive_empty_payload](/Users/caojunze424/code/SysyCC/tests/preprocess/error_directive_empty_payload)
- [tests/preprocess/define_invalid_parameter_name](/Users/caojunze424/code/SysyCC/tests/preprocess/define_invalid_parameter_name)
- [tests/preprocess/define_duplicate_parameter_name](/Users/caojunze424/code/SysyCC/tests/preprocess/define_duplicate_parameter_name)
- [tests/preprocess/define_variadic_parameter_position](/Users/caojunze424/code/SysyCC/tests/preprocess/define_variadic_parameter_position)
- [tests/preprocess/function_like_macro_if_identifier](/Users/caojunze424/code/SysyCC/tests/preprocess/function_like_macro_if_identifier)
- [tests/preprocess/has_include_false_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_false_branch)
- [tests/preprocess/has_cpp_attribute_condition](/Users/caojunze424/code/SysyCC/tests/preprocess/has_cpp_attribute_condition)
- [tests/preprocess/has_include_malformed](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_malformed)
- [tests/preprocess/has_include_next_true_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_next_true_branch)
- [tests/preprocess/has_include_true_branch](/Users/caojunze424/code/SysyCC/tests/preprocess/has_include_true_branch)
- [tests/preprocess/inactive_if_expression_shield](/Users/caojunze424/code/SysyCC/tests/preprocess/inactive_if_expression_shield)
- [tests/preprocess/integer_suffix_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/integer_suffix_conditional_expr)
- [tests/preprocess/include_empty_path](/Users/caojunze424/code/SysyCC/tests/preprocess/include_empty_path)
- [tests/preprocess/include_error_trace](/Users/caojunze424/code/SysyCC/tests/preprocess/include_error_trace)
- [tests/preprocess/include_next](/Users/caojunze424/code/SysyCC/tests/preprocess/include_next)
- [tests/preprocess/missing_endif](/Users/caojunze424/code/SysyCC/tests/preprocess/missing_endif)
- [tests/preprocess/elifdef_missing_condition](/Users/caojunze424/code/SysyCC/tests/preprocess/elifdef_missing_condition)
- [tests/preprocess/line_directive](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive)
- [tests/preprocess/line_directive_logical_location](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_logical_location)
- [tests/preprocess/line_directive_missing_number](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_missing_number)
- [tests/preprocess/line_directive_spaced_filename](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_spaced_filename)
- [tests/preprocess/line_directive_trailing_tokens](/Users/caojunze424/code/SysyCC/tests/preprocess/line_directive_trailing_tokens)
- [tests/preprocess/long_macro_pipeline](/Users/caojunze424/code/SysyCC/tests/preprocess/long_macro_pipeline)
- [tests/preprocess/macro_redefinition_conflict](/Users/caojunze424/code/SysyCC/tests/preprocess/macro_redefinition_conflict)
- [tests/preprocess/macro_redefinition_equivalent](/Users/caojunze424/code/SysyCC/tests/preprocess/macro_redefinition_equivalent)
- [tests/preprocess/predefined_host_macros](/Users/caojunze424/code/SysyCC/tests/preprocess/predefined_host_macros)
- [tests/preprocess/long_include_graph](/Users/caojunze424/code/SysyCC/tests/preprocess/long_include_graph)
- [tests/preprocess/long_condition_matrix](/Users/caojunze424/code/SysyCC/tests/preprocess/long_condition_matrix)
- [tests/preprocess/preprocess_error_location](/Users/caojunze424/code/SysyCC/tests/preprocess/preprocess_error_location)
- [tests/preprocess/pragma_once](/Users/caojunze424/code/SysyCC/tests/preprocess/pragma_once)
- [tests/preprocess/spaced_directive_keywords](/Users/caojunze424/code/SysyCC/tests/preprocess/spaced_directive_keywords)
- [tests/preprocess/ternary_conditional_expr](/Users/caojunze424/code/SysyCC/tests/preprocess/ternary_conditional_expr)
- [tests/preprocess/variadic_macro](/Users/caojunze424/code/SysyCC/tests/preprocess/variadic_macro)
- [tests/preprocess/warning_directive](/Users/caojunze424/code/SysyCC/tests/preprocess/warning_directive)
- [tests/preprocess/warning_directive_empty_payload](/Users/caojunze424/code/SysyCC/tests/preprocess/warning_directive_empty_payload)
- [tests/preprocess/warning_directive_payload_preserved](/Users/caojunze424/code/SysyCC/tests/preprocess/warning_directive_payload_preserved)
- [tests/preprocess/include_path](/Users/caojunze424/code/SysyCC/tests/preprocess/include_path)
- [tests/preprocess/system_include_iso646](/Users/caojunze424/code/SysyCC/tests/preprocess/system_include_iso646)
- [tests/preprocess/system_macro_redefinition_allowed](/Users/caojunze424/code/SysyCC/tests/preprocess/system_macro_redefinition_allowed)
- [tests/preprocess/preprocess_nested_conditionals](/Users/caojunze424/code/SysyCC/tests/preprocess/preprocess_nested_conditionals)

### `tests/lexer/`

Lexer-stage regressions and targeted diagnostics, including:

- exact token-kind dumps
- operator-mix tokenization
- literal formats
- `inline` keyword tokenization through prototype parsing
- `long double` keyword tokenization
- `union` and `unsigned` keyword tokenization through declaration parsing
- invalid-token diagnostics
- scanner-structure checks

Representative paths:

- [tests/lexer/precise_token_kinds](/Users/caojunze424/code/SysyCC/tests/lexer/precise_token_kinds)
- [tests/lexer/c_token_kinds](/Users/caojunze424/code/SysyCC/tests/lexer/c_token_kinds)
- [tests/lexer/lexer_operator_mix](/Users/caojunze424/code/SysyCC/tests/lexer/lexer_operator_mix)
- [tests/lexer/invalid_token_diagnostic](/Users/caojunze424/code/SysyCC/tests/lexer/invalid_token_diagnostic)
- [tests/lexer/integer_literal_suffix](/Users/caojunze424/code/SysyCC/tests/lexer/integer_literal_suffix)
- [tests/lexer/long_double_tokens](/Users/caojunze424/code/SysyCC/tests/lexer/long_double_tokens)
- [tests/lexer/line_directive_logical_location](/Users/caojunze424/code/SysyCC/tests/lexer/line_directive_logical_location)
- [tests/lexer/line_directive_spaced_logical_location](/Users/caojunze424/code/SysyCC/tests/lexer/line_directive_spaced_logical_location)

### `tests/parser/`

Parser-oriented frontend smoke tests, including:

- minimal whole-pipeline inputs
- control-flow parsing
- function calls
- parser-extension acceptance
- GNU-style function attribute parsing
- member-access parsing
- builtin `double` type parsing
- builtin `_Float16` type parsing
- `extern` variable declarations
- compatible file-scope global redeclarations
- builtin `signed char` / `short` / `unsigned short` declaration forms
- builtin `long int` type parsing
- builtin `long long int` type parsing
- `union` declarations and inline anonymous union declarations
- declaration-only function prototypes
- `inline` declaration-only function prototypes
- unnamed prototype parameters
- unnamed pointer prototype parameters
- `const char *`-style prototype parameters
- `long double` declaration parsing
- parser failure diagnostics with current-token text and logical source spans

Representative paths:

- [tests/parser/minimal](/Users/caojunze424/code/SysyCC/tests/parser/minimal)
- [tests/parser/if_else](/Users/caojunze424/code/SysyCC/tests/parser/if_else)
- [tests/parser/empty_statement](/Users/caojunze424/code/SysyCC/tests/parser/empty_statement)
- [tests/parser/control_flow](/Users/caojunze424/code/SysyCC/tests/parser/control_flow)
- [tests/parser/c_parser_extensions](/Users/caojunze424/code/SysyCC/tests/parser/c_parser_extensions)
- [tests/parser/double_type](/Users/caojunze424/code/SysyCC/tests/parser/double_type)
- [tests/parser/extern_variable_decl](/Users/caojunze424/code/SysyCC/tests/parser/extern_variable_decl)
- [tests/parser/float16_type](/Users/caojunze424/code/SysyCC/tests/parser/float16_type)
- [tests/parser/function_prototype_decl](/Users/caojunze424/code/SysyCC/tests/parser/function_prototype_decl)
- [tests/parser/gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/parser/gnu_attribute_prototype)
- [tests/parser/inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/parser/inline_function_prototype)
- [tests/parser/const_char_pointer_prototype](/Users/caojunze424/code/SysyCC/tests/parser/const_char_pointer_prototype)
- [tests/parser/long_int_type](/Users/caojunze424/code/SysyCC/tests/parser/long_int_type)
- [tests/parser/long_long_int_type](/Users/caojunze424/code/SysyCC/tests/parser/long_long_int_type)
- [tests/parser/long_double_type](/Users/caojunze424/code/SysyCC/tests/parser/long_double_type)
- [tests/parser/signed_short_builtin_types](/Users/caojunze424/code/SysyCC/tests/parser/signed_short_builtin_types)
- [tests/parser/ternary_expr](/Users/caojunze424/code/SysyCC/tests/parser/ternary_expr)
- [tests/parser/union_decl](/Users/caojunze424/code/SysyCC/tests/parser/union_decl)
- [tests/parser/unnamed_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/unnamed_parameter_prototype)
- [tests/parser/unnamed_pointer_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/unnamed_pointer_parameter_prototype)
- [tests/parser/parser_error_diagnostic](/Users/caojunze424/code/SysyCC/tests/parser/parser_error_diagnostic)

### `tests/ast/`

AST lowering regressions, including:

- basic translation-unit lowering
- declaration and type lowering
- initializer-list preservation
- source spans
- pointer/member access
- direct pointer arithmetic over `pointer + integer`, `pointer - integer`, and
  `pointer - pointer`
- completeness guards
- control-flow lowering
- builtin `double` type lowering
- builtin `_Float16` type lowering
- `extern` variable declaration lowering
- builtin `signed char` / `short` / `unsigned short` type lowering
- builtin `long int` type lowering
- builtin `long long int` type lowering
- union declaration lowering
- declaration-only function prototype lowering
- GNU-style function attribute lowering
- `inline` declaration-only function prototype lowering
- unnamed prototype parameter lowering
- unnamed pointer prototype parameter lowering
- `const char *` prototype parameter lowering with preserved pointee qualifiers
- `long double` declaration lowering

Representative paths:

- [tests/ast/ast_minimal](/Users/caojunze424/code/SysyCC/tests/ast/ast_minimal)
- [tests/ast/ast_logical_source_span](/Users/caojunze424/code/SysyCC/tests/ast/ast_logical_source_span)
- [tests/ast/ast_dot_member_access](/Users/caojunze424/code/SysyCC/tests/ast/ast_dot_member_access)
- [tests/ast/ast_index_expr](/Users/caojunze424/code/SysyCC/tests/ast/ast_index_expr)
- [tests/ast/ast_pointer_types](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_types)
- [tests/ast/ast_source_span](/Users/caojunze424/code/SysyCC/tests/ast/ast_source_span)
- [tests/ast/ast_stmt_extensions](/Users/caojunze424/code/SysyCC/tests/ast/ast_stmt_extensions)
- [tests/ast/ast_conditional_expr](/Users/caojunze424/code/SysyCC/tests/ast/ast_conditional_expr)
- [tests/ast/ast_double_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_double_type)
- [tests/ast/ast_extern_variable_decl](/Users/caojunze424/code/SysyCC/tests/ast/ast_extern_variable_decl)
- [tests/ast/ast_float16_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_float16_type)
- [tests/ast/ast_function_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_function_prototype)
- [tests/ast/ast_gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_gnu_attribute_prototype)
- [tests/ast/ast_inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_inline_function_prototype)
- [tests/ast/ast_const_char_pointer_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_const_char_pointer_prototype)
- [tests/ast/ast_long_int_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_long_int_type)
- [tests/ast/ast_long_long_int_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_long_long_int_type)
- [tests/ast/ast_long_double_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_long_double_type)
- [tests/ast/ast_signed_short_builtin_types](/Users/caojunze424/code/SysyCC/tests/ast/ast_signed_short_builtin_types)
- [tests/ast/ast_union_decl](/Users/caojunze424/code/SysyCC/tests/ast/ast_union_decl)
- [tests/ast/ast_unnamed_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_unnamed_parameter_prototype)
- [tests/ast/ast_unnamed_pointer_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_unnamed_pointer_parameter_prototype)

### `tests/semantic/`

Semantic-analysis regressions, including:

- undefined identifiers and redefinitions
- call arity and call-type checking
- assignment/lvalue rules
- variable-initializer type rules
- unary/binary operand constraints
- member-access validation
- switch/case/default checks
- constant-expression checks
- pointer arithmetic and array decay
- internal `ptrdiff_t`-width pointer-difference IR preservation
- integer coercion at assignment, initializer, and call-argument sites
- builtin `double` variables and return types
- builtin `_Float16` declaration types
- `extern` variable declarations
- builtin `signed char` / `short` / `unsigned short` declaration types
- builtin `long int` declaration types
- builtin `long long int` declaration types
- union declarations and union-member access through `.` / `->`
- unnamed pointer prototype parameters
- `const char *` prototype parameters
- qualification-preserving and qualification-dropping pointer call checks
- runtime pointer arithmetic through `pointer + integer`, `pointer - integer`,
  and `pointer - pointer`
- declaration-only function prototypes
- declaration-only `inline` function prototypes
- GNU-style function attribute prototypes
- unsupported recognized GNU function attributes
- unnamed prototype parameters
- `long double` declaration-only prototypes

Representative paths:

- [tests/semantic/semantic_undefined_identifier](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_undefined_identifier)
- [tests/semantic/semantic_const_initializer_constant](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_initializer_constant)
- [tests/semantic/semantic_var_initializer_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_var_initializer_type)
- [tests/semantic/semantic_dot_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_dot_type)
- [tests/semantic/semantic_array_decay_assignment](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_array_decay_assignment)
- [tests/semantic/semantic_call_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_call_type)
- [tests/semantic/semantic_extern_variable_decl](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_extern_variable_decl)
- [tests/semantic/semantic_global_redeclaration](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_global_redeclaration)
- [tests/semantic/semantic_float16_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_float16_type)
- [tests/semantic/semantic_duplicate_case](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_duplicate_case)
- [tests/semantic/semantic_pointer_arithmetic](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_arithmetic)
- [tests/ir/ir_ptrdiff_pointer_difference](/Users/caojunze424/code/SysyCC/tests/ir/ir_ptrdiff_pointer_difference)
- [tests/semantic/semantic_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_source_file)
- [tests/semantic/semantic_logical_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_logical_source_file)
- [tests/semantic/semantic_conditional_condition](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_conditional_condition)
- [tests/semantic/semantic_double_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_double_type)
- [tests/semantic/semantic_function_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_function_prototype)
- [tests/semantic/semantic_gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_gnu_attribute_prototype)
- [tests/semantic/semantic_inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_inline_function_prototype)
- [tests/semantic/semantic_long_double_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_long_double_type)
- [tests/semantic/semantic_signed_short_builtin_types](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_signed_short_builtin_types)
- [tests/semantic/semantic_const_pointer_call_ok](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_pointer_call_ok)
- [tests/semantic/semantic_const_pointer_call_reject](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_pointer_call_reject)
- [tests/semantic/semantic_usual_arithmetic_conversions](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_usual_arithmetic_conversions)
- [tests/semantic/semantic_union_decl](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_union_decl)
- [tests/semantic/semantic_unsupported_attribute](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unsupported_attribute)
- [tests/semantic/semantic_unnamed_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unnamed_parameter_prototype)

### `tests/ir/`

IR-lowering regressions, including:

- integer arithmetic and comparisons
- short-circuit boolean lowering
- loop and branch lowering
- direct call lowering
- integer ternary lowering
- function-level `__always_inline__` lowering to LLVM `alwaysinline`
- `const char *` parameter lowering through qualifier-stripping pointer IR
- `extern` global declaration lowering
- initialized global variable definition lowering

Representative paths:

- [tests/ir/ir_binary_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_binary_expr)
- [tests/ir/ir_short_circuit](/Users/caojunze424/code/SysyCC/tests/ir/ir_short_circuit)
- [tests/ir/ir_conditional_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_conditional_expr)
- [tests/ir/ir_always_inline_attribute](/Users/caojunze424/code/SysyCC/tests/ir/ir_always_inline_attribute)
- [tests/ir/ir_const_char_pointer_param](/Users/caojunze424/code/SysyCC/tests/ir/ir_const_char_pointer_param)
- [tests/ir/ir_extern_global_decl](/Users/caojunze424/code/SysyCC/tests/ir/ir_extern_global_decl)
- [tests/ir/ir_global_variable_definition](/Users/caojunze424/code/SysyCC/tests/ir/ir_global_variable_definition)
- [tests/ir/ir_integer_literal_suffix](/Users/caojunze424/code/SysyCC/tests/ir/ir_integer_literal_suffix)

### `tests/run/`

End-to-end execution regressions that compile generated LLVM IR with a local
runtime stub, feed stdin, and compare stdout, including:

- integer input via `getint`
- global variable reads through generated LLVM IR
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
- integer coercion in assignments, local initializers, and call arguments
- arithmetic and comparisons
- short-circuit logical control flow
- `if` / `while` / `for` / `do-while`
- `switch/case/default`
- `break` / `continue`

Representative paths:

- [tests/ir/ir_minimal](/Users/caojunze424/code/SysyCC/tests/ir/ir_minimal)
- [tests/ir/ir_modulo](/Users/caojunze424/code/SysyCC/tests/ir/ir_modulo)
- [tests/ir/ir_function_call](/Users/caojunze424/code/SysyCC/tests/ir/ir_function_call)
- [tests/ir/ir_assignment_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_assignment_integer_conversion)
- [tests/ir/ir_initializer_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_initializer_integer_conversion)
- [tests/ir/ir_call_argument_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_call_argument_integer_conversion)
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
