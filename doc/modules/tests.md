# Tests Module

## Scope

The tests module stores stage-oriented SysY22 test cases plus the shared helper
scripts used by local regression runs.

## Main Layout

Tests are now grouped by pipeline stage:

```text
tests/
├── compiler2025/
│   ├── run_functional.sh
│   ├── run_arm_functional.sh
│   ├── run_arm_performance.sh
│   ├── run_host_ir_performance.sh
│   ├── run_functional_in_docker.sh
│   ├── run_arm_functional_in_docker.sh
│   ├── run_arm_performance_in_docker.sh
│   └── runtime support files
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
├── run_tier1.sh
├── run_tier2.sh
├── run_full.sh
└── test_helpers.sh
```

Each concrete case lives under `tests/<stage>/<case>/` and contains:

- one or more `.sy` inputs
- an executable `run.sh`
- any stage-specific helper files such as local headers

The shared [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh)
now also does three important pieces of resource coordination for local runs:

- serializes the main `cmake --build` step per `build/` directory
- chooses more memory-conservative default parallelism for build/test workers
- wraps heavy host-side tools such as `SysyCC`, `clang`, and `clang++`
  behind one shared slot controller so several manually started single-case
  scripts do not all spawn peak-memory compile/link work at once

`tests/compiler2025/` holds larger external-suite entry points that sit beside,
rather than inside, the regular `tests/<stage>/<case>/run.sh` tree. The
current scripts cover:

- the original correctness harness for the recovered `functional` and
  `h_functional` suites, now executed through SysyCC-generated native AArch64
  assembly plus cross assemble/link and qemu user-mode execution rather than
  the older host-side `--dump-ir` fallback
- an ARM-performance correctness runner that treats
  `tests/compiler2025/extracted/ARM-性能` as a case root and checks the
  generated program output against the bundled `.out` files
- an ARM-performance benchmark runner that compares SysyCC-generated LLVM IR
  programs against a direct Clang baseline and writes a Markdown timing report
- a host-side IR-performance runner for Core IR optimization work, which keeps
  the same `SysyCC -> .ll -> clang link/run` measurement path but adds
  compile/run timeouts so long-running or wedged cases do not stall the whole
  suite
- matching Docker wrappers for the recovered functional suite plus the ARM
  functional/performance runners, so the larger compiler2025 suites can be
  executed inside the repository Docker image instead of depending on the host
  toolchain layout

Recent end-to-end coverage that now matters for the shared SysY22/C goal
includes:

- parser/AST/semantic/IR/runtime coverage for compound assignments
- parser/AST/semantic/IR/runtime coverage for the comma operator
- parser/AST/semantic coverage for `struct` bit-field declarators
- IR/runtime coverage for bit-field read-modify-write, signed extraction, and
  global aggregate initializers
- AST/semantic coverage for unnamed typedef-name prototype parameters such as
  `size_t` and `va_list`
- parser/semantic/IR/runtime coverage for named `struct` forward declarations
- IR/runtime coverage for supported `struct` return functions and discarded
  aggregate-return calls
- parser coverage for leading-attribute function declarations
- IR/runtime coverage for implicit `ret void` insertion in empty `void`
  functions
- IR/runtime coverage for string literals lowered through private LLVM globals
- semantic/IR/runtime coverage for string literal character-array initializers
- IR/runtime coverage for char literal expressions lowered as ordinary integer
  constants
- semantic/IR/runtime coverage for enum-valued variables, parameters, and
  returns flowing through LLVM IR as ordinary integer storage
- semantic coverage for empty enums, negative enumerators, duplicate
  enumerator values, and pointer-based self-referential struct declarations
- semantic/runtime coverage for pointer/integer casts and float/integer
  truncation or widening boundaries
- semantic/runtime coverage for conditional expressions used in integer
  constant-expression contexts such as array dimensions, `case` labels, and
  enumerator values
- semantic/IR/runtime coverage for function-designator decay into local
  function-pointer variables and indirect function-pointer calls
- IR/runtime coverage for prefix and postfix increment expressions
- preprocess/runtime coverage for csmith safe-math headers whose `#if`
  branches depend on standard limit macros and builtin numeric macros
- preprocess coverage for adjacent-string concatenation after `#`
  stringization and for variadic macro calls with no trailing variadic
  arguments
- semantic/IR/runtime coverage for unary bitwise-not integer promotions,
  including `long long` operands that must keep their widened result type
- IR/runtime coverage for mixed signed/unsigned integer comparisons whose
  usual arithmetic conversion changes comparison signedness without changing
  the lowered LLVM storage width
- IR/runtime coverage for typedef-backed 64-bit integer comparisons such as
  `int64_t value = -1; value <= 0UL;`, which must still lower through
  unsigned `i64` comparison semantics
- IR/runtime coverage for narrow unsigned bit-field comparisons such as
  `bits.value <= -1`, including comma-expression wrappers that must preserve
  the bit-field width long enough for integer-promotion selection
- semantic/runtime coverage for incompatible pointer assignments that must warn
  without aborting compilation
- semantic/runtime coverage for incompatible pointer returns that must warn
  without aborting compilation
- semantic/runtime coverage for `volatile struct` member address initializers,
  including qualifier propagation from the owning aggregate onto the selected
  field type
- IR/runtime coverage for global and local union initializer lists, including
  cases where the first initialized field is narrower than the union storage
- IR/runtime coverage for aggregate assignment expressions used directly as
  fixed-parameter call arguments
- runtime coverage for LeetCode-inspired array algorithms adapted to the
  SysyCC stdin/stdout harness, including in-place duplicate compaction,
  binary-search insertion index lookup, Kadane maximum subarray, single-pass
  stock-profit scanning, and Boyer-Moore majority voting
- bug-reproducer coverage for direct standard-library heap allocation through
  `malloc` / `free`, preserving the current failure mode where the compiler
  still chokes on transitive macOS system-header constructs while trying to
  compile a dynamically allocated integer-buffer sum program
- currently failing standard-library compatibility coverage for additional
  macOS system-header and builtin expansion gaps, including `string.h`
  `memcpy`, `math.h` `isnan`, `ctype.h` classification macros, and
  `assert.h` predefined-builtin usage
- currently failing standard-library compatibility coverage for compiler
  builtin-type and declaration forms surfaced by common headers, including
  `stddef.h` `ptrdiff_t`, `time.h` declaration aliases, `float.h` builtin
  floating macros, and `stdalign.h` `alignas`
- runtime coverage for longer LeetCode-inspired composite problems including
  dynamic 2D union-find, multi-source grid BFS, handwritten heap-based graph
  shortest path, interval-room scheduling with paired heaps, and 2D dynamic
  programming over matrix state
- IR failure coverage for unsupported function bodies, which must now stop
  compilation with a diagnostic instead of being silently skipped
- IR failure coverage for internal backend emission failures, which must now
  abort `IRBuilder` with a compiler diagnostic instead of silently producing a
  partial module
- Core IR foundation coverage for raw module printing, value-use tracking, and
  terminator detection before the future optimization pipeline is wired into
  the production compiler path
- Core IR pipeline coverage for a placeholder optimization stage that preserves
  one staged module before lowering
- frontend-to-Core-IR coverage for staged lowering of minimal function bodies
  and structured `if/else` control flow before the future optimization
  pipeline is wired into the production compiler path
- frontend-to-Core-IR coverage for staged lowering of local scalar variables,
  simple assignments, direct calls, and integer comparison expressions before
  the future optimization pipeline is wired into the production compiler path
- frontend-to-Core-IR coverage for staged lowering of unary integer
  expressions and loop-shaped control flow with `while`, `break`, and
  `continue`
- frontend-to-Core-IR coverage for staged lowering of `for` and `do-while`
  loops through explicit Core IR basic blocks
- frontend-to-Core-IR coverage for staged lowering of string literals through
  private Core IR globals plus derived element pointers
- frontend-to-Core-IR coverage for staged lowering of top-level scalar globals
  and explicit global address-based load/store
- frontend-to-Core-IR coverage for explicit stack-slot addresses, pointer
  dereference stores, function addresses, and indirect function-pointer calls
- frontend-to-Core-IR coverage for array indexing through explicit staged
  `gep`, including value-based indices
- frontend-to-Core-IR coverage for scalar struct-member load/store through the
  shared aggregate layout service
- frontend-to-Core-IR coverage for `goto` / label control flow and `switch`
  dispatch through explicit staged basic blocks
- staged Core-IR-to-LLVM coverage for the currently supported subset through
  the explicit top-level Core IR pass sequence
- staged Core-IR-to-LLVM coverage for array indexing plus struct-member
  addressing in the same pipeline-level regression
- staged Core-IR-to-LLVM coverage for pointer arithmetic and pointer
  differences
- staged Core-IR-to-LLVM coverage for static internal linkage, top-level
  constant global-address initializers, and union-backed aggregate storage
- staged Core-IR-to-LLVM coverage for variadic default-argument promotions at
  direct call sites
- IR coverage for float add/sub/mul/div lowering through `fadd`, `fsub`,
  `fmul`, and `fdiv`
- staged Core-IR-to-AArch64 placeholder coverage for the current
  not-yet-implemented ARM backend contract

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
- compiler option-to-context synchronization across constructor, `set_option`,
  and `Run()`

Representative paths:

- [tests/dialects/default_dialect_registry](/Users/caojunze424/code/SysyCC/tests/dialects/default_dialect_registry)
- [tests/dialects/handler_registry_conflict_policy](/Users/caojunze424/code/SysyCC/tests/dialects/handler_registry_conflict_policy)
- [tests/dialects/lexer_keyword_conflict_policy](/Users/caojunze424/code/SysyCC/tests/dialects/lexer_keyword_conflict_policy)
- [tests/dialects/lexer_keyword_runtime_classification](/Users/caojunze424/code/SysyCC/tests/dialects/lexer_keyword_runtime_classification)
- [tests/dialects/parser_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/parser_feature_runtime_policy)
- [tests/dialects/ast_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/ast_feature_runtime_policy)
- [tests/dialects/semantic_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/semantic_feature_runtime_policy)
- [tests/dialects/restrict_qualified_pointer_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/restrict_qualified_pointer_runtime_policy)
- [tests/dialects/pointer_nullability_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/pointer_nullability_runtime_policy)
- [tests/dialects/preprocess_feature_runtime_policy](/Users/caojunze424/code/SysyCC/tests/dialects/preprocess_feature_runtime_policy)
- [tests/dialects/dialect_registration_fail_fast](/Users/caojunze424/code/SysyCC/tests/dialects/dialect_registration_fail_fast)
- [tests/dialects/strict_c99_dialect_configuration](/Users/caojunze424/code/SysyCC/tests/dialects/strict_c99_dialect_configuration)
- [tests/dialects/optional_dialect_pack_switches](/Users/caojunze424/code/SysyCC/tests/dialects/optional_dialect_pack_switches)
- [tests/dialects/cli_dialect_option_mapping](/Users/caojunze424/code/SysyCC/tests/dialects/cli_dialect_option_mapping)
- [tests/dialects/complier_option_context_sync](/Users/caojunze424/code/SysyCC/tests/dialects/complier_option_context_sync)

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
- [tests/preprocess/predefined_standard_limit_conditionals](/Users/caojunze424/code/SysyCC/tests/preprocess/predefined_standard_limit_conditionals)
- [tests/preprocess/stringize_concatenation_combo](/Users/caojunze424/code/SysyCC/tests/preprocess/stringize_concatenation_combo)
- [tests/preprocess/variadic_macro_empty_args](/Users/caojunze424/code/SysyCC/tests/preprocess/variadic_macro_empty_args)
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
- comma-operator parsing
- parser-extension acceptance
- GNU-style function attribute parsing
- GNU asm-label function prototype parsing
- variadic function prototype parsing
- member-access parsing
- builtin `double` type parsing
- builtin `_Float16` type parsing
- parser-seeded bootstrap typedef-name parsing for system-header aliases such
  as `size_t`, `ptrdiff_t`, and `va_list`
- grouped function-pointer field declarators such as `void (*routine)(void *)`
- pointer-return function prototypes such as `void *memchr(...)`
- `extern` variable declarations
- compatible file-scope global redeclarations
- builtin `signed char` / `short` / `unsigned short` declaration forms
- builtin `unsigned long` type parsing
- builtin `long int` type parsing
- builtin `long long int` type parsing
- `union` declarations and inline anonymous union declarations
- named `struct` forward declarations such as `struct Node;`
- leading GNU attribute function declarations such as
  `__attribute__((__always_inline__)) int foo(void);`
- declaration-only function prototypes
- `inline` declaration-only function prototypes
- unnamed prototype parameters
- unnamed pointer prototype parameters
- unnamed typedef-name prototype parameters
- `const char *`-style prototype parameters
- declaration-side and pointer-side `volatile` qualifiers, including GNU
  `__volatile` / `__volatile__` spellings
- pointer-side `restrict` / `__restrict` / `__restrict__` prototype parameters
- pointer-side `_Nullable` / `_Nonnull` / `_Null_unspecified` prototype
  annotations
- nullability-marked function-pointer parameter prototypes with Darwin-style
  pointer annotation spellings such as `_LIBC_COUNT(__n)`, `_LIBC_CSTR`, and
  bare pointer-side qualifiers such as `_LIBC_UNSAFE_INDEXABLE`
- `struct` bit-field declarators such as `unsigned value : 5`
- GNU `__const` / `__const__` spellings inside top-level `extern const`
  variable declarations
- suffix GNU attributes on function prototypes
- variadic function definitions and calls
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
- [tests/parser/bootstrap_typedef_names](/Users/caojunze424/code/SysyCC/tests/parser/bootstrap_typedef_names)
- [tests/parser/function_pointer_field_decl](/Users/caojunze424/code/SysyCC/tests/parser/function_pointer_field_decl)
- [tests/parser/function_prototype_decl](/Users/caojunze424/code/SysyCC/tests/parser/function_prototype_decl)
- [tests/parser/gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/parser/gnu_attribute_prototype)
- [tests/parser/gnu_asm_function_prototype](/Users/caojunze424/code/SysyCC/tests/parser/gnu_asm_function_prototype)
- [tests/parser/variadic_function_prototype](/Users/caojunze424/code/SysyCC/tests/parser/variadic_function_prototype)
- [tests/parser/inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/parser/inline_function_prototype)
- [tests/parser/const_char_pointer_prototype](/Users/caojunze424/code/SysyCC/tests/parser/const_char_pointer_prototype)
- [tests/parser/long_int_type](/Users/caojunze424/code/SysyCC/tests/parser/long_int_type)
- [tests/parser/long_long_int_type](/Users/caojunze424/code/SysyCC/tests/parser/long_long_int_type)
- [tests/parser/long_double_type](/Users/caojunze424/code/SysyCC/tests/parser/long_double_type)
- [tests/parser/pointer_return_prototype](/Users/caojunze424/code/SysyCC/tests/parser/pointer_return_prototype)
- [tests/parser/const_struct_object_decl](/Users/caojunze424/code/SysyCC/tests/parser/const_struct_object_decl)
- [tests/parser/leading_attribute_function_decl](/Users/caojunze424/code/SysyCC/tests/parser/leading_attribute_function_decl)
- [tests/parser/suffix_gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/parser/suffix_gnu_attribute_prototype)
- [tests/parser/struct_forward_decl](/Users/caojunze424/code/SysyCC/tests/parser/struct_forward_decl)
- [tests/parser/unnamed_typedef_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/unnamed_typedef_parameter_prototype)
- [tests/parser/signed_short_builtin_types](/Users/caojunze424/code/SysyCC/tests/parser/signed_short_builtin_types)
- [tests/parser/ternary_expr](/Users/caojunze424/code/SysyCC/tests/parser/ternary_expr)
- [tests/parser/union_decl](/Users/caojunze424/code/SysyCC/tests/parser/union_decl)
- [tests/parser/unsigned_long_type](/Users/caojunze424/code/SysyCC/tests/parser/unsigned_long_type)
- [tests/parser/unnamed_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/unnamed_parameter_prototype)
- [tests/parser/unnamed_pointer_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/unnamed_pointer_parameter_prototype)
- [tests/parser/parser_error_diagnostic](/Users/caojunze424/code/SysyCC/tests/parser/parser_error_diagnostic)
- [tests/parser/nullable_function_pointer_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/parser/nullable_function_pointer_parameter_prototype)

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
- `_Float16` cast and arithmetic lowering through LLVM `half`
- `extern` variable declaration lowering
- builtin `signed char` / `short` / `unsigned short` type lowering
- bootstrap typedef-name lowering for parser-seeded aliases such as `size_t`
  and `va_list`
- builtin `unsigned long` type lowering
- grouped function-pointer field lowering
- pointer-return function prototype lowering
- builtin `long int` type lowering
- builtin `long long int` type lowering
- union declaration lowering
- unary bitwise-not width preservation for promoted integer operands
- declaration-only function prototype lowering
- GNU-style function attribute lowering
- pointers to forward-declared `struct` types
- implicit `ret void` insertion for empty `void` functions
- `inline` declaration-only function prototype lowering
- unnamed prototype parameter lowering
- unnamed pointer prototype parameter lowering
- pointer-target cast preservation such as `(int *)value` and
  `(const char *)buffer`
- `const char *` prototype parameter lowering with preserved pointee qualifiers
- pointer-side `restrict` prototype parameter lowering with preserved pointer
  qualifiers
- `long double` declaration lowering
- `long double` cast and arithmetic lowering through LLVM `fp128`

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
- [tests/ast/ast_function_pointer_field](/Users/caojunze424/code/SysyCC/tests/ast/ast_function_pointer_field)
- [tests/ast/ast_function_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_function_prototype)
- [tests/ast/ast_gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_gnu_attribute_prototype)
- [tests/ast/ast_inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_inline_function_prototype)
- [tests/ast/ast_const_char_pointer_prototype](/Users/caojunze424/code/SysyCC/tests/ast/ast_const_char_pointer_prototype)
- [tests/ast/ast_pointer_target_cast_expr](/Users/caojunze424/code/SysyCC/tests/ast/ast_pointer_target_cast_expr)
- [tests/ast/ast_typedef_name_type](/Users/caojunze424/code/SysyCC/tests/ast/ast_typedef_name_type)
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
- aggregate-member qualifier propagation
- switch/case/default checks
- constant-expression checks
- pointer arithmetic and array decay
- internal `ptrdiff_t`-width pointer-difference IR preservation
- integer coercion at assignment, initializer, and call-argument sites
- builtin `double` variables and return types
- builtin `_Float16` declaration types
- `_Float16` cast-expression semantic acceptance
- bootstrap typedef-name semantic resolution for system-header aliases such as
  `size_t`, `ptrdiff_t`, and `va_list`
- `extern` variable declarations
- grouped function-pointer field declarations and typedef-backed field use
- pointer-return function prototypes
- builtin `signed char` / `short` / `unsigned short` declaration types
- builtin `unsigned long` declaration types
- builtin `long int` declaration types
- builtin `long long int` declaration types
- union declarations and union-member access through `.` / `->`
- unary bitwise-not integer-promotion coverage across narrow and wide integer
  operands
- unnamed pointer prototype parameters
- `const char *` prototype parameters
- `volatile int * volatile` prototype parameters
- pointer-side `restrict` prototype parameters
- nullability-marked function-pointer parameter prototypes
- qualification-preserving and qualification-dropping pointer call checks
- incompatible pointer returns that must warn without aborting semantic
  analysis
- runtime pointer arithmetic through `pointer + integer`, `pointer - integer`,
  and `pointer - pointer`
- variadic function calls with fixed-parameter semantic checking
- declaration-only function prototypes
- declaration-only `inline` function prototypes
- GNU-style function attribute prototypes
- unsupported recognized GNU function attributes
- unnamed prototype parameters
- pointer-target cast syntax such as `(int *)value`
- `long double` declaration-only prototypes
- `long double` cast-expression semantic acceptance

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
- [tests/semantic/semantic_bootstrap_typedef_names](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_bootstrap_typedef_names)
- [tests/semantic/semantic_unary_bitwise_not_integer_promotion](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unary_bitwise_not_integer_promotion)
- [tests/semantic/semantic_function_pointer_field](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_function_pointer_field)
- [tests/semantic/semantic_duplicate_case](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_duplicate_case)
- [tests/semantic/semantic_pointer_arithmetic](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_arithmetic)
- [tests/ir/ir_ptrdiff_pointer_difference](/Users/caojunze424/code/SysyCC/tests/ir/ir_ptrdiff_pointer_difference)
- [tests/semantic/semantic_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_source_file)
- [tests/semantic/semantic_logical_source_file](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_logical_source_file)
- [tests/semantic/semantic_conditional_condition](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_conditional_condition)
- [tests/semantic/semantic_double_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_double_type)
- [tests/semantic/semantic_function_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_function_prototype)
- [tests/semantic/semantic_gnu_attribute_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_gnu_attribute_prototype)
- [tests/semantic/semantic_variadic_function_call](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_variadic_function_call)
- [tests/semantic/semantic_string_literal_array_initializer](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_string_literal_array_initializer)
- [tests/semantic/semantic_enum_value_flow](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_enum_value_flow)
- [tests/semantic/semantic_function_pointer_call](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_function_pointer_call)
- [tests/semantic/semantic_inline_function_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_inline_function_prototype)
- [tests/semantic/semantic_long_double_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_long_double_type)
- [tests/semantic/semantic_pointer_return_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_return_prototype)
- [tests/semantic/semantic_signed_short_builtin_types](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_signed_short_builtin_types)
- [tests/semantic/semantic_unsigned_long_type](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unsigned_long_type)
- [tests/semantic/semantic_const_pointer_call_ok](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_pointer_call_ok)
- [tests/semantic/semantic_const_pointer_call_reject](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_const_pointer_call_reject)
- [tests/semantic/semantic_pointer_nullability_annotation](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_nullability_annotation)
- [tests/semantic/semantic_pointer_target_cast_expr](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_pointer_target_cast_expr)
- [tests/semantic/semantic_usual_arithmetic_conversions](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_usual_arithmetic_conversions)
- [tests/semantic/semantic_union_decl](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_union_decl)
- [tests/semantic/semantic_unsupported_attribute](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unsupported_attribute)
- [tests/semantic/semantic_unnamed_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_unnamed_parameter_prototype)
- [tests/semantic/semantic_incompatible_pointer_return_warning](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_incompatible_pointer_return_warning)
- [tests/semantic/semantic_volatile_struct_member_address_initializer](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_volatile_struct_member_address_initializer)

### `tests/ir/`

IR-lowering regressions, including:

- integer arithmetic and comparisons
- short-circuit boolean lowering
- loop and branch lowering
- direct call lowering
- integer ternary lowering
- function-level `__always_inline__` lowering to LLVM `alwaysinline`
- `const char *` parameter lowering through qualifier-stripping pointer IR
- `volatile int * volatile` parameter lowering through qualifier-stripping
  pointer IR
- `const char * __restrict` parameter lowering through qualifier-stripping
- GNU-const extern globals lowered through the shared external-global path
  pointer IR
- `extern` global declaration lowering
- bootstrap typedef-backed global lowering such as `size_t`
- initialized global variable definition lowering
- pointer-target cast lowering through LLVM `inttoptr`
- floating comparison and floating-truthiness lowering across
  `float` / `_Float16` / `double` / `long double`
- variadic function definitions and calls, including default argument
  promotions for extra operands
- supported aggregate-return function definitions and comma-expression calls
  whose aggregate result is discarded
- unary bitwise-not width preservation for `long long` operands
- csmith safe-math preprocess parity for limit-macro-controlled helper bodies

Representative paths:

- [tests/ir/ir_binary_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_binary_expr)
- [tests/ir/ir_short_circuit](/Users/caojunze424/code/SysyCC/tests/ir/ir_short_circuit)
- [tests/ir/ir_conditional_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_conditional_expr)
- [tests/ir/ir_always_inline_attribute](/Users/caojunze424/code/SysyCC/tests/ir/ir_always_inline_attribute)
- [tests/ir/ir_implicit_void_return](/Users/caojunze424/code/SysyCC/tests/ir/ir_implicit_void_return)
- [tests/ir/ir_struct_return_call_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_struct_return_call_expr)
- [tests/ir/ir_string_literal_array_initializer](/Users/caojunze424/code/SysyCC/tests/ir/ir_string_literal_array_initializer)
- [tests/ir/ir_enum_value_flow](/Users/caojunze424/code/SysyCC/tests/ir/ir_enum_value_flow)
- [tests/ir/ir_function_pointer_call](/Users/caojunze424/code/SysyCC/tests/ir/ir_function_pointer_call)
- [tests/ir/ir_struct_forward_decl](/Users/caojunze424/code/SysyCC/tests/ir/ir_struct_forward_decl)
- [tests/ir/ir_const_char_pointer_param](/Users/caojunze424/code/SysyCC/tests/ir/ir_const_char_pointer_param)
- [tests/ir/ir_pointer_nullability_erasure](/Users/caojunze424/code/SysyCC/tests/ir/ir_pointer_nullability_erasure)
- [tests/ir/ir_extern_global_decl](/Users/caojunze424/code/SysyCC/tests/ir/ir_extern_global_decl)
- [tests/ir/ir_global_variable_definition](/Users/caojunze424/code/SysyCC/tests/ir/ir_global_variable_definition)
- [tests/ir/ir_integer_literal_suffix](/Users/caojunze424/code/SysyCC/tests/ir/ir_integer_literal_suffix)
- [tests/ir/ir_size_t_bootstrap_type](/Users/caojunze424/code/SysyCC/tests/ir/ir_size_t_bootstrap_type)
- [tests/ir/ir_pointer_target_cast_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_pointer_target_cast_expr)
- [tests/ir/ir_floating_comparison_and_condition](/Users/caojunze424/code/SysyCC/tests/ir/ir_floating_comparison_and_condition)
- [tests/ir/ir_variadic_function_definition](/Users/caojunze424/code/SysyCC/tests/ir/ir_variadic_function_definition)
- [tests/ir/ir_nullable_function_pointer_parameter_prototype](/Users/caojunze424/code/SysyCC/tests/ir/ir_nullable_function_pointer_parameter_prototype)
- [tests/ir/ir_unary_bitwise_not_long_long](/Users/caojunze424/code/SysyCC/tests/ir/ir_unary_bitwise_not_long_long)
- [tests/ir/ir_unsigned_bit_field_integer_promotion](/Users/caojunze424/code/SysyCC/tests/ir/ir_unsigned_bit_field_integer_promotion)

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
- floating comparisons and floating truthiness in control flow for the current
  supported runtime subset
- variadic function calls where the callee only consumes fixed parameters
- incompatible pointer returns that must warn while still lowering through IR
- supported aggregate-return calls whose result is discarded before control
  continues with scalar work
- csmith safe-math helper parity when preprocess-selected branches depend on
  system-header numeric limit macros
- unary bitwise-not runtime parity for `long long` operands
- `volatile struct` member address initializers that must lower through
  constant-address global initialization and runtime loads
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
- [tests/run/run_variadic_function_call](/Users/caojunze424/code/SysyCC/tests/run/run_variadic_function_call)
- [tests/run/run_struct_return_call_expr](/Users/caojunze424/code/SysyCC/tests/run/run_struct_return_call_expr)
- [tests/run/run_string_literal_array_initializer](/Users/caojunze424/code/SysyCC/tests/run/run_string_literal_array_initializer)
- [tests/run/run_enum_value_flow](/Users/caojunze424/code/SysyCC/tests/run/run_enum_value_flow)
- [tests/run/run_function_pointer_call](/Users/caojunze424/code/SysyCC/tests/run/run_function_pointer_call)
- [tests/run/run_incompatible_pointer_return](/Users/caojunze424/code/SysyCC/tests/run/run_incompatible_pointer_return)
- [tests/run/run_volatile_struct_member_address_initializer](/Users/caojunze424/code/SysyCC/tests/run/run_volatile_struct_member_address_initializer)
- [tests/run/run_unsigned_bit_field_integer_promotion](/Users/caojunze424/code/SysyCC/tests/run/run_unsigned_bit_field_integer_promotion)
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

It appends new cases after the highest existing numeric case directory under
`tests/fuzz/`. For example, if `tests/fuzz/005/` already exists, running the
script with `10` creates `006/` through `015/` instead of overwriting
`001/` through `010/`.

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

For testing or local automation, the script also honors these environment
overrides:

- `SYSYCC_FUZZ_CASE_ROOT`: alternate case-output directory
- `SYSYCC_CSMITH_BIN`: alternate `csmith` executable path
- `SYSYCC_CLANG_BIN`: alternate `clang` executable name/path
- `SYSYCC_CSMITH_RUNTIME_DIR`: alternate Csmith runtime include directory
- `SYSYCC_CSMITH_BUILD_RUNTIME_DIR`: alternate built-runtime include directory

Example with generation only:

```bash
bash tests/fuzz/generate_and_build_csmith_cases.sh --generate-only 3
```

In an empty `tests/fuzz/` workspace this produces:

```text
tests/fuzz/001/fuzz_001.c
tests/fuzz/002/fuzz_002.c
tests/fuzz/003/fuzz_003.c
```

Example with generation and compilation:

```bash
bash tests/fuzz/generate_and_build_csmith_cases.sh 2
```

In an empty `tests/fuzz/` workspace this produces:

```text
tests/fuzz/001/fuzz_001.c
tests/fuzz/001/fuzz_001.out
tests/fuzz/002/fuzz_002.c
tests/fuzz/002/fuzz_002.out
```

Example with incremental numbering from an existing local workspace:

```bash
bash tests/fuzz/generate_and_build_csmith_cases.sh 10
```

If `tests/fuzz/005/` already exists, this appends:

```text
tests/fuzz/006/fuzz_006.c
tests/fuzz/006/fuzz_006.out
...
tests/fuzz/015/fuzz_015.c
tests/fuzz/015/fuzz_015.out
```

Generated numbered directories under `tests/fuzz/` are ignored by Git so the
script can be used as a local fuzz-input workspace.

A dedicated script regression now also covers this incremental numbering
behavior:

- [tests/fuzz/generate_and_build_csmith_cases_incremental](/Users/caojunze424/code/SysyCC/tests/fuzz/generate_and_build_csmith_cases_incremental)

`run_csmith_cases.sh` executes generated fuzz cases as a differential test
between the host toolchain and `SysyCC`, then archives process logs per
numbered directory. By default it does not keep `SysyCC` frontend or IR dump
files in the case directory. It runs the requested cases in parallel by default,
using the detected logical CPU count unless `RUN_FUZZ_JOBS` overrides it, and
prints a simple completed-case progress line while workers finish. It supports:

- `all` to run every numbered case directory under `tests/fuzz/`
- no arguments to run every numbered case directory under `tests/fuzz/`
- one or more specific case ids such as `001` or `1 4 7`
- either plain decimal or zero-padded case ids such as `8` and `008`

Numbered case discovery now accepts any pure-decimal directory name and sorts
them numerically, so mixed-width layouts such as
`001 ... 200 0201 ... 0999 1000 ... 1200` are all included by `all` and by the
empty-argument default. Explicit decimal requests are resolved back to the
existing on-disk directory name, so `201` correctly selects `0201`.

For each requested case, the script:

1. compiles `fuzz_<id>.c` with `clang`
2. runs the `clang` binary with the case-local input file if present, or an
   empty input file otherwise
3. invokes `SysyCC` with `--dump-ir`
4. if `SysyCC` succeeds, compiles the emitted LLVM IR with `clang`
5. runs the `SysyCC`-produced executable with the same input
6. compares stdout, stderr, and exit code

If `SysyCC` compilation fails, the script keeps the compiler error output in the
same case directory and records the run in a Markdown summary file that also
captures run metadata plus per-status totals:

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
- `fuzz_<id>.sysycc.link.stdout.txt`
- `fuzz_<id>.sysycc.link.stderr.txt`
- `fuzz_<id>.sysycc.link.exit.txt`
- `fuzz_<id>.sysycc.stdout.txt`
- `fuzz_<id>.sysycc.stderr.txt`
- `fuzz_<id>.sysycc.exit.txt`
- `fuzz_<id>.compare.txt`

This makes it possible to inspect both compiler paths and the final output
comparison for one specific numbered case directory without retaining compiler
intermediate dumps by default.

`run_csmith_cases.sh` can also print a per-case terminal summary that lists the
recorded artifact paths for the host-toolchain logs, `SysyCC` diagnostics, and
any optional copied frontend/IR intermediates. The script does not echo the
full contents of those files by default. The summary mode is controlled by
`PRINT_INTERMEDIATE_MODE`:

- `never`: do not print the per-case report
- `failure`: print the report only for non-`MATCH` outcomes
- `always`: print the report for every requested case

Additional environment:

- `RUN_FUZZ_JOBS=<n>`: override automatic logical-CPU parallelism
- `SYSYCC_FUZZ_RESULT_FILE=<path>`: write the Markdown report to a custom file
- `SYSYCC_FUZZ_CASE_ROOT=<dir>`: override the fuzz case root directory
- `SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=none|full`: keep no `SysyCC` dump files by
  default, or preserve copied `.preprocessed.sy`, `.tokens.txt`, `.parse.txt`,
  `.ast.txt`, and `.ll` artifacts in the case directory when set to `full`

`PRINT_INTERMEDIATE_MODE=never` and
`SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=none` are the default modes.

Example:

```bash
bash tests/fuzz/run_csmith_cases.sh
bash tests/fuzz/run_csmith_cases.sh 001
bash tests/fuzz/run_csmith_cases.sh all
PRINT_INTERMEDIATE_MODE=always bash tests/fuzz/run_csmith_cases.sh 001
SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=full bash tests/fuzz/run_csmith_cases.sh 001
```

Representative script regression:

- [tests/fuzz/run_csmith_cases_multi_width_ids](/Users/caojunze424/code/SysyCC/tests/fuzz/run_csmith_cases_multi_width_ids)
- [tests/fuzz/run_csmith_cases_capture_mode](/Users/caojunze424/code/SysyCC/tests/fuzz/run_csmith_cases_capture_mode)

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
- variadic-call lowering syntax for user-defined functions and `stdio.h`
  declarations such as `printf`

Representative paths:

- [tests/ir/ir_minimal](/Users/caojunze424/code/SysyCC/tests/ir/ir_minimal)
- [tests/ir/ir_modulo](/Users/caojunze424/code/SysyCC/tests/ir/ir_modulo)
- [tests/ir/ir_function_call](/Users/caojunze424/code/SysyCC/tests/ir/ir_function_call)
- [tests/ir/ir_assignment_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_assignment_integer_conversion)
- [tests/ir/ir_initializer_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_initializer_integer_conversion)
- [tests/ir/ir_call_argument_integer_conversion](/Users/caojunze424/code/SysyCC/tests/ir/ir_call_argument_integer_conversion)
- [tests/ir/ir_uint32_t_bootstrap_type](/Users/caojunze424/code/SysyCC/tests/ir/ir_uint32_t_bootstrap_type)
- [tests/parser/float_literal_suffix](/Users/caojunze424/code/SysyCC/tests/parser/float_literal_suffix)
- [tests/parser/hex_float_literal](/Users/caojunze424/code/SysyCC/tests/parser/hex_float_literal)
- [tests/parser/compound_assignment_expr](/Users/caojunze424/code/SysyCC/tests/parser/compound_assignment_expr)
- [tests/ir/ir_float_literal_suffix](/Users/caojunze424/code/SysyCC/tests/ir/ir_float_literal_suffix)
- [tests/ir/ir_hex_float_literal](/Users/caojunze424/code/SysyCC/tests/ir/ir_hex_float_literal)
- [tests/ast/ast_compound_assignment_expr](/Users/caojunze424/code/SysyCC/tests/ast/ast_compound_assignment_expr)
- [tests/semantic/semantic_compound_assignment_expr](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_compound_assignment_expr)
- [tests/ir/ir_compound_assignment_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_compound_assignment_expr)
- [tests/run/run_float_literal_suffix](/Users/caojunze424/code/SysyCC/tests/run/run_float_literal_suffix)
- [tests/run/run_hex_float_literal](/Users/caojunze424/code/SysyCC/tests/run/run_hex_float_literal)
- [tests/run/run_compound_assignment_expr](/Users/caojunze424/code/SysyCC/tests/run/run_compound_assignment_expr)
- [tests/parser/static_function_definition](/Users/caojunze424/code/SysyCC/tests/parser/static_function_definition)
- [tests/parser/parenthesized_function_macro_name](/Users/caojunze424/code/SysyCC/tests/parser/parenthesized_function_macro_name)
- [tests/ast/ast_static_top_level_decls](/Users/caojunze424/code/SysyCC/tests/ast/ast_static_top_level_decls)
- [tests/ast/ast_parenthesized_function_macro_name](/Users/caojunze424/code/SysyCC/tests/ast/ast_parenthesized_function_macro_name)
- [tests/semantic/semantic_static_top_level_decls](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_static_top_level_decls)
- [tests/semantic/semantic_parenthesized_function_macro_name](/Users/caojunze424/code/SysyCC/tests/semantic/semantic_parenthesized_function_macro_name)
- [tests/ir/ir_static_top_level_decls](/Users/caojunze424/code/SysyCC/tests/ir/ir_static_top_level_decls)
- [tests/ir/ir_parenthesized_function_macro_name](/Users/caojunze424/code/SysyCC/tests/ir/ir_parenthesized_function_macro_name)
- [tests/ir/ir_short_circuit](/Users/caojunze424/code/SysyCC/tests/ir/ir_short_circuit)
- [tests/ir/ir_switch](/Users/caojunze424/code/SysyCC/tests/ir/ir_switch)
- [tests/ir/ir_variadic_function_definition](/Users/caojunze424/code/SysyCC/tests/ir/ir_variadic_function_definition)
- [tests/ir/ir_stdio_printf_variadic_call](/Users/caojunze424/code/SysyCC/tests/ir/ir_stdio_printf_variadic_call)
- [tests/ir/ir_char_literal_expr](/Users/caojunze424/code/SysyCC/tests/ir/ir_char_literal_expr)

Representative `tests/run/` execution regressions also cover host-linked
`stdio.h` variadic printing now:

- [tests/run/run_stdio_printf_hello_world](/Users/caojunze424/code/SysyCC/tests/run/run_stdio_printf_hello_world)
- [tests/run/run_stdio_printf_int](/Users/caojunze424/code/SysyCC/tests/run/run_stdio_printf_int)
- [tests/run/run_stdio_printf_string](/Users/caojunze424/code/SysyCC/tests/run/run_stdio_printf_string)
- [tests/run/run_stdio_printf_string_and_int](/Users/caojunze424/code/SysyCC/tests/run/run_stdio_printf_string_and_int)
- [tests/run/run_char_literal_expr](/Users/caojunze424/code/SysyCC/tests/run/run_char_literal_expr)

### `tests/asm/`

Native Linux AArch64 asm regressions, including:

- recursive calls, branches, and integer arithmetic in emitted `.s`
- `--stop-after=core-ir` behavior when the native backend is selected
- invalid option combinations such as `--backend=aarch64-native --dump-ir`
- fail-fast coverage for unsupported native-backend features such as float
  stack slots

## Shared Helpers

- [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh)
  provides common build, artifact, link, and runtime-output assertions for
  success-path tests, prefers Ninja plus `ccache` when available, and now
  serializes concurrent local `cmake` / compiler invocations per build
  directory so later test runs wait for the active build instead of colliding.
- [tests/run_all.sh](/Users/caojunze424/code/SysyCC/tests/run_all.sh)
  recursively discovers executable `tests/<stage>/<case>/run.sh`, defaults to
  the tier-1 regression lane (`run`, `cli`, `dialects`), supports
  `--layer tier1|tier2|all`, and writes a Markdown summary to
  `build/test_result.md`.
- [tests/run_tier1.sh](/Users/caojunze424/code/SysyCC/tests/run_tier1.sh)
  is the explicit day-to-day fast lane for `run`, `cli`, and `dialects`
  regressions.
- [tests/run_tier2.sh](/Users/caojunze424/code/SysyCC/tests/run_tier2.sh)
  runs the stage-focused second layer covering `asm`, `ast`, `fuzz`, `ir`,
  `lexer`, `object`, `parser`, `preprocess`, and `semantic`.
- [tests/run_full.sh](/Users/caojunze424/code/SysyCC/tests/run_full.sh)
  preserves the old full-suite behavior by running both layers together.

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

Parser-, AST-, and semantic-focused regression scripts now commonly invoke
`SysyCC` with `--stop-after=parse`, `--stop-after=ast`, or
`--stop-after=semantic` so those tests can validate the intended stage without
being blocked by fail-fast IR diagnostics from intentionally unsupported later
constructs.
