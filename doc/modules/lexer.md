# Lexer Module

## Scope

The lexer module contains the lexical-analysis pass, the flex source template,
and the generated scanner source used by the active pipeline.

## Directory Layout

```text
src/frontend/lexer/
├── lexer.hpp
├── lexer.cpp
├── lexer.l
└── lexer_scanner.cpp
```

## Responsibilities

- open the preprocessed source file or original input file
- invoke `yylex()`
- collect token streams with exact token kinds such as `KwInt`, `IntLiteral`,
  `ShiftLeft`, `Question`, `Dot`, and GNU-extension keywords such as
  `KwAttribute`
- classify identifier-like lexemes into keywords through the shared
  [LexerKeywordRegistry](/Users/caojunze424/code/SysyCC/src/frontend/dialects/lexer_keyword_registry.hpp)
  owned by
  [DialectManager](/Users/caojunze424/code/SysyCC/src/frontend/dialects/dialect_manager.hpp)
- remap token source spans through preprocess-exported logical line positions
  so `#line` file/line state can propagate into lexer diagnostics and parse-tree
  terminal nodes
- consume a shared downstream
  [SourceMappingView](/Users/caojunze424/code/SysyCC/src/common/source_mapping_view.hpp)
  built by [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- dump token results into `build/intermediate_results/*.tokens.txt`
- keep lex-only runs free of parse-tree node allocation side effects
- allow empty token streams and leave empty-input policy to later stages
- assume comments were already removed by preprocess

## Key Files

- [lexer.hpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp)
- [lexer.cpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.cpp)
- [lexer.l](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.l)
- [lexer_scanner.cpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer_scanner.cpp)

## Output Artifacts

- token dump text files in `build/intermediate_results`
- token stream entries stored in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)

## Notes

- The lexer now uses a reentrant flex scanner with one [LexerState](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp) instance per scanner session.
- [LexerState](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp)
  now consumes one shared
  [SourceMappingView](/Users/caojunze424/code/SysyCC/src/common/source_mapping_view.hpp)
  from
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  plus one shared
  [LexerKeywordRegistry](/Users/caojunze424/code/SysyCC/src/frontend/dialects/lexer_keyword_registry.hpp)
  from the same compiler context's
  [DialectManager](/Users/caojunze424/code/SysyCC/src/frontend/dialects/dialect_manager.hpp)
  and now exposes explicit logical-vs-physical token position queries while the
  active lexer/token pipeline continues to consume logical positions by
  default.
- The flex scanner no longer hardcodes the project keyword list in one fixed
  block; identifier-like lexemes now flow through runtime keyword
  classification so default C99/GNU/builtin-type dialect packs and test-only
  custom registries use the same path.
- The lexer pass disables scanner-side parse-tree terminal-node creation during
  lex-only runs.
