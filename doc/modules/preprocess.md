# Preprocess Module

## Scope

The preprocess module contains the active preprocessing pass and its internal
helper classes. It is the first front-end stage in the current pipeline.

## Directory Layout

```text
src/frontend/preprocess/
├── preprocess.hpp
├── preprocess.cpp
└── detail/
    ├── conditional/
    │   ├── builtin_probe_evaluator.hpp
    │   ├── clang_extension_provider.hpp
    │   ├── clang_extension_provider.cpp
    │   ├── gnu_extension_provider.hpp
    │   ├── gnu_extension_provider.cpp
    │   ├── nonstandard_extension_manager.hpp
    │   ├── builtin_probe_evaluator.cpp
    │   └── nonstandard_extension_manager.cpp
    ├── directive/
    │   ├── directive_executor.hpp
    │   └── directive_executor.cpp
    ├── source/
    │   ├── source_mapper.hpp
    │   └── source_mapper.cpp
    ├── conditional_stack.hpp
    ├── conditional_stack.cpp
    ├── directive_parser.hpp
    ├── directive_parser.cpp
    ├── file_loader.hpp
    ├── file_loader.cpp
    ├── include_resolver.hpp
    ├── include_resolver.cpp
    ├── macro_expander.hpp
    ├── macro_expander.cpp
    ├── macro_table.hpp
    ├── macro_table.cpp
    ├── preprocess_context.hpp
    ├── preprocess_context.cpp
    ├── preprocess_runtime.hpp
    ├── preprocess_runtime.cpp
    ├── preprocess_session.hpp
    └── preprocess_session.cpp
```

## Responsibilities

- handle object macro preprocessing state
- handle function-like macro definitions and fixed-arity or variadic invocation expansion
- reject malformed function-like macro parameter lists, including invalid
  parameter identifiers, duplicate names, and variadic parameters that do not
  appear last
- support stringification (`#`) and token pasting (`##`) in function-like macros
- strip `//` and `/* ... */` comments before lexical analysis without
  corrupting string or character literals
- write preprocessed intermediate source files before lexical analysis
- run the preprocessing pass before lexer analysis
- resolve `#include "..."` against the including file's current directory and
  `-I` include search paths
- resolve `#include <...>` against default system include search paths
- resolve `#include <...>` against CLI `-I` directories before explicit and
  default system include search paths
- resolve `#include_next <...>` by continuing the current system include
  search chain after the header that was already selected
- fall back from quoted includes to system include directories after exhausting
  local and user-provided include search paths
- support conditional directives (`#ifdef`, `#ifndef`, `#elif`, `#else`,
  `#endif`)
- support simple `#if/#elif` constant expressions including identifiers,
  `defined(...)`, arithmetic, bitwise, shifts, and logical operators
- probe `__has_include(...)` and `__has_include_next(...)` against the active
  include search paths during preprocessor condition evaluation
- parse common clang preprocessor builtin probes such as `__has_feature(...)`,
  `__has_extension(...)`, `__has_builtin(...)`, `__has_attribute(...)`, and
  `__has_cpp_attribute(...)`, and `__building_module(...)`
- route builtin probe parsing through a dedicated internal helper instead of
  keeping every probe special-case inside the core constant expression parser
- route non-standard probe families through a manager/provider split so clang-
  specific and GNU-specific compatibility can evolve independently
- route directive execution through a dedicated internal helper instead of
  keeping all directive semantics inside the preprocessing session driver
- centralize shared preprocessing state inside a dedicated preprocess context
  instead of scattering mutable state directly across the session and helpers
- centralize include-stack tracking and `#line` logical-file remapping inside a
  dedicated source mapper
- preserve the logical include chain for nested preprocess failures so
  diagnostics can append `included from ...` context
- export one logical source position per emitted preprocessed line so later
  lexer/parser/semantic stages can inherit preprocess `#line` file and line
  remapping
- accept standard integer literal suffixes such as `U`, `L`, `UL`, `LL`, and
  `ULL` in `#if/#elif` expressions
- annotate line-local preprocess failures with `file:line: message`
- split preprocessing logic across focused internal classes instead of one large
  pass implementation

## Key Files

- [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp)
- [preprocess.cpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)
- [preprocess_session.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_session.hpp)
- [preprocess_context.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_context.hpp)
- [source_mapper.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/source/source_mapper.hpp)
- [directive_parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/directive_parser.hpp)
- [directive_executor.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/directive/directive_executor.hpp)
- [builtin_probe_evaluator.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/conditional/builtin_probe_evaluator.hpp)
- [nonstandard_extension_manager.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/conditional/nonstandard_extension_manager.hpp)
- [clang_extension_provider.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/conditional/clang_extension_provider.hpp)
- [gnu_extension_provider.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/conditional/gnu_extension_provider.hpp)
- [macro_expander.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/macro_expander.hpp)
- [file_loader.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/file_loader.hpp)

## Current Internal Shape

The active internal collaboration path is:

```text
PreprocessPass
  -> PreprocessSession
      -> PreprocessContext
          -> SourceMapper
      -> DirectiveParser
      -> DirectiveExecutor
      -> ConstantExpressionEvaluator
          -> BuiltinProbeEvaluator
              -> NonStandardExtensionManager
                  -> ClangExtensionProvider
                  -> GnuExtensionProvider
      -> MacroExpander
      -> IncludeResolver
      -> FileLoader
```

`PreprocessSession` now acts primarily as the run driver, while
`PreprocessContext` owns the mutable state that needs to survive across helper
calls during one preprocessing pass. `SourceMapper` is the location-focused
part of that shared state and is responsible for physical include nesting plus
`#line`-driven logical remapping during preprocessing. It also records the
logical include site for each nested file so preprocess errors can emit nested
include traces. `PreprocessRuntime` is now intentionally narrower and only
keeps emitted output,
[SourceLineMap](/Users/caojunze424/code/SysyCC/src/common/source_line_map.hpp)
data for emitted-line logical positions, comment state, and file-skip metadata
such as `#pragma once` bookkeeping.

## Supported Syntax

- directives
  - optional whitespace between `#` and the directive keyword
  - `#define NAME value`
  - `#define ADD(a, b) ((a) + (b))`
  - `#define LOG(...) __VA_ARGS__`
- `#error message`
- `#warning message`
- empty `#error` / `#warning` directives use default directive-triggered messages
- non-empty `#error` / `#warning` directives preserve the full trimmed
  remainder text
- `#line 123`
- `#line 123 "file.h"`
- `#line 123 "file name with spaces.h"`
  - trailing tokens after the optional quoted file name are rejected
  - `#pragma once`
  - `#pragma anything-else`
  - `#undef NAME`
  - `#include "file.h"`
  - `#include <file.h>`
  - `#include_next <file.h>`
  - `#ifdef NAME`
  - `#ifndef NAME`
  - `#if expr`
  - `#elif expr`
  - `#elifdef NAME`
  - `#elifndef NAME`
  - `#else`
  - `#endif`
- macro features
  - object-like replacement
  - fixed-arity function-like replacement
  - variadic function-like replacement with `...` and `__VA_ARGS__`
  - validation for function-like macro parameter identifiers and placement
  - multi-line macro definitions using trailing `\`
  - nested expansion in ordinary source lines
  - stringification with `#`
  - token pasting with `##`
- include search behavior
  - quoted includes search the including file directory first
  - quoted includes then search CLI `-I` directories
  - quoted includes finally fall back to default system include directories
  - angle includes search CLI `-I` directories first
  - angle includes then search explicit and default system include directories
  - `#include_next <...>` continues from the next matching system include
    directory after the current header
- `#if/#elif` expression subset
  - integer literals, including standard unsigned and long suffixes
  - identifiers after macro replacement
  - `defined(NAME)`
  - unary `!`, unary `~`, unary `+`, unary `-`
  - `*`, `/`, `%`, `+`, `-`, `<<`, `>>`
  - `&`, `^`, `|`
  - `<`, `<=`, `>`, `>=`
  - `==`, `!=`
  - `&&`, `||`
  - ternary `?:`
  - comma operator
  - parentheses
  - `__has_include(...)`
  - `__has_include_next(...)`
  - `__has_feature(...)`
  - `__has_extension(...)`
  - `__has_builtin(...)`
  - `__has_attribute(...)`
  - `__has_cpp_attribute(...)`
  - `__building_module(...)`

## Unsupported Syntax

- `#if/#elif` expression forms
  - full system-header builtin probing semantics beyond the current minimal
    `__has_include` handling
- other behavior gaps
  - complete C preprocessor compatibility
  - exact column-preserving and macro-expansion-aware downstream source-location
    remapping beyond the current emitted-line file/line mapping
  - pragma-specific semantics beyond `#pragma once`
  - comment-preserving source-location mapping into later stages

## Output Artifacts

- preprocessed source files in `build/intermediate_results`

## Notes

- The public surface of this module is [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp).
- The `detail/` directory is intentionally internal and should not be treated as
  a cross-module public API.
