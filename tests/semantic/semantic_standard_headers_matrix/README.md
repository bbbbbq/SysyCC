# System-Header Compatibility Matrix

This matrix is a semantic-stage ownership map for Dev B system-header work.
Each listed smoke runs `compiler --stop-after=semantic` so failures stay
attributable to preprocess, lexer/parser, AST lowering, or semantic analysis
instead of backend lowering.

| Header | Status | Semantic smoke | Current attribution |
| --- | --- | --- | --- |
| `stdlib.h` | `PASS` | `tests/semantic/semantic_stdlib_h_smoke` | Frontend accepts transitive declarations and `malloc` / `free` prototypes. |
| `string.h` | `PASS` | `tests/semantic/semantic_string_h_smoke` | Frontend accepts `memcpy` declaration and host builtin macro path. |
| `stddef.h` | `PASS` | `tests/semantic/semantic_stddef_h_smoke` | Frontend accepts `ptrdiff_t`, `size_t`, and `NULL`. |
| `assert.h` | `PASS` | `tests/semantic/semantic_standard_headers_first_batch_smoke` | Frontend accepts assert macro expansion in the first-batch aggregate gate. |
| `ctype.h` | `PASS` | `tests/semantic/semantic_standard_headers_first_batch_smoke` | Frontend accepts `isdigit` macro/function path in the first-batch aggregate gate. |
| `float.h` | `PASS` | `tests/semantic/semantic_float_h_smoke` | Frontend accepts public macros backed by predefined floating builtins. |
| `math.h` | `PASS_FRONTEND` | `tests/semantic/semantic_math_h_smoke`, `tests/semantic/semantic_math_h_isnan_macro_bug` | Frontend accepts `isnan`; full run remains blocked later in non-Dev-B Core IR/backend lowering. |
| `time.h` | `PASS` | `tests/semantic/semantic_time_h_smoke` | Frontend accepts `time_t`, `struct tm`, and asm-labeled declarations. |
| `stdalign.h` | `PASS` | `tests/semantic/semantic_stdalign_h_smoke` | Frontend accepts `alignas` / `_Alignas` on objects and fields. |
| `stdbool.h` | `PASS` | `tests/semantic/semantic_stdbool_h_smoke` | Frontend accepts `bool`, `true`, and `false` through `_Bool` bootstrap typing. |
| `stdint.h` | `PASS` | `tests/semantic/semantic_stdint_h_smoke` | Frontend accepts fixed-width and pointer-width typedefs. |
| `limits.h` | `PASS` | `tests/semantic/semantic_limits_h_smoke` | Frontend accepts `INT_MAX` and `CHAR_BIT` macro expansion. |
| `errno.h` | `PASS` | `tests/semantic/semantic_errno_h_smoke` | Frontend accepts `errno` macro/declaration path and common error macros. |

Second-batch aggregate gate:
`tests/semantic/semantic_standard_headers_second_batch_smoke`.
