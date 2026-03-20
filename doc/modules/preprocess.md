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
- resolve `#include_next <...>` by continuing the current system include
  search chain after the header that was already selected
- fall back from quoted includes to system include directories after exhausting
  local and user-provided include search paths
- support conditional directives (`#ifdef`, `#ifndef`, `#elif`, `#else`,
  `#endif`)
- support simple `#if/#elif` constant expressions including identifiers,
  `defined(...)`, arithmetic, bitwise, shifts, and logical operators
- tolerate `__has_include(...)` and `__has_include_next(...)` checks in
  preprocessor conditions by treating them as unavailable during expression
  evaluation
- split preprocessing logic across focused internal classes instead of one large
  pass implementation

## Key Files

- [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp)
- [preprocess.cpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)
- [preprocess_session.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_session.hpp)
- [directive_parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/directive_parser.hpp)
- [macro_expander.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/macro_expander.hpp)
- [file_loader.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/file_loader.hpp)

## Supported Syntax

- directives
  - `#define NAME value`
  - `#define ADD(a, b) ((a) + (b))`
  - `#define LOG(...) __VA_ARGS__`
  - `#error message`
  - `#line 123`
  - `#line 123 "file.h"`
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
  - angle includes search default system include directories
  - `#include_next <...>` continues from the next matching system include
    directory after the current header
- `#if/#elif` expression subset
  - integer literals
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

## Unsupported Syntax

- `#if/#elif` expression forms
  - full system-header builtin probing semantics beyond the current minimal
    `__has_include` handling
- other behavior gaps
  - complete C preprocessor compatibility
  - full downstream source-location remapping for accepted `#line` directives
  - pragma-specific semantics beyond `#pragma once`
  - comment-preserving source-location mapping into later stages

## Output Artifacts

- preprocessed source files in `build/intermediate_results`

## Notes

- The public surface of this module is [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp).
- The `detail/` directory is intentionally internal and should not be treated as
  a cross-module public API.
