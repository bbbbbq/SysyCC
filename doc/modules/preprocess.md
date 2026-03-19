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
- handle function-like macro definitions and fixed-arity invocation expansion
- support stringification (`#`) and token pasting (`##`) in function-like macros
- strip `//` and `/* ... */` comments before lexical analysis without
  corrupting string or character literals
- write preprocessed intermediate source files before lexical analysis
- run the preprocessing pass before lexer analysis
- resolve `#include "..."` against the including file's current directory and
  `-I` include search paths
- support conditional directives (`#ifdef`, `#ifndef`, `#elif`, `#else`,
  `#endif`)
- support simple `#if/#elif` constant expressions including identifiers,
  `defined(...)`, `&&`, and arithmetic
- split preprocessing logic across focused internal classes instead of one large
  pass implementation

## Key Files

- [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp)
- [preprocess.cpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)
- [preprocess_session.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_session.hpp)
- [directive_parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/directive_parser.hpp)
- [macro_expander.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/macro_expander.hpp)

## Output Artifacts

- preprocessed source files in `build/intermediate_results`

## Notes

- The public surface of this module is [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp).
- The `detail/` directory is intentionally internal and should not be treated as
  a cross-module public API.
