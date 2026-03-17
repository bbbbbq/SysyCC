# Frontend Module

## Scope

The frontend module contains the SysY22 lexer, parser, grammar templates, and
the small runtime used to build parse trees. The top-level frontend structure
is now organized by language stage.

## Directory Layout

```text
src/frontend/
├── lexer/
│   ├── lexer.hpp
│   ├── lexer.cpp
│   ├── lexer.l
│   └── lexer_scanner.cpp
├── parser/
│   ├── parser.hpp
│   ├── parser.cpp
│   ├── parser_runtime.hpp
│   ├── parser_runtime.cpp
│   ├── parser.y
│   ├── parser_generated.cpp
│   └── parser.tab.h
└── preprocess/
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

## Submodules

### `lexer`

This directory contains the lexer pass and the flex source template.

Main responsibilities:

- open input files
- collect token streams
- invoke `yylex()`
- dump token results
- assume comments were already removed by preprocess

### `parser`

This directory contains the parser pass, parser runtime, and the bison source
template.

Main responsibilities:

- collect parse trees
- invoke `yyparse()`
- dump intermediate files
- accept the current SysY22 core grammar plus a growing subset of C-style extensions
- parse float / char / string literals, pointer declarators, `for`, `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`, and `->`

### `preprocess`

This directory contains the active preprocessing stage. The public surface is
[preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp),
while the `detail/` directory contains internal helper classes.

Main responsibilities:

- handle object macro preprocessing state
- handle function-like macro definitions and fixed-arity invocation expansion
- support stringification (`#`) and token pasting (`##`) in function-like macros
- strip `//` and `/* ... */` comments before lexical analysis
- write preprocessed intermediate source files before lexical analysis
- run the preprocessing pass before lexer analysis
- resolve `#include "..."` against the including file's current directory and `-I` include search paths
- support conditional directives (`#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`)
- support simple `#if/#elif` constant expressions including identifiers, `defined(...)`, `&&`, and arithmetic
- split preprocessing logic across focused internal classes instead of one large pass implementation

## Key Files

- [lexer.hpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp)
- [lexer.l](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.l)
- [parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.hpp)
- [parser.y](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.y)
- [parser_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.hpp)
- [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp)

## Output Artifacts

- preprocessed source files in `build/intermediate_results`
- token dump text files
- parse dump text files
- parse tree runtime nodes

## Notes

- `src/frontend` no longer contains a `driver/` layer.
- The generated parser header is [parser.tab.h](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.tab.h).
- The generated lexer and parser C++ files are [lexer_scanner.cpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer_scanner.cpp) and [parser_generated.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_generated.cpp).
