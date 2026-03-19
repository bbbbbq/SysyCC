# SysyCC Documentation

## Tree

```text
doc/
├── README.md
└── modules/
    ├── class-relationships.md
    ├── cli.md
    ├── common.md
    ├── compiler.md
    ├── ast.md
    ├── lexer.md
    ├── manual.md
    ├── parser.md
    ├── preprocess.md
    ├── scripts.md
    ├── tests.md
    └── legacy-pass.md
```
Tests live inside per-case directories under `tests/`, each bundling the `.sy` input and an executable `run.sh`.
Shared assertions for success-path test scripts live in [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh).
The top-level regression entry [tests/run_all.sh](/Users/caojunze424/code/SysyCC/tests/run_all.sh) now also writes a summary table to `build/test_result.md`.

## Project Overview

SysyCC is a small SysY22 compiler front-end prototype organized around a pass
pipeline. The current implementation focuses on these stages:

- command line parsing
- compiler option assembly
- pass scheduling
- preprocessing
- lexical analysis
- syntax analysis
- AST lowering
- intermediate result dumping

The executable entry is [main.cpp](/Users/caojunze424/code/SysyCC/src/main.cpp).
Its high-level flow is:

```text
main
  -> Cli
  -> Complier
  -> PassManager
      -> PreprocessPass
      -> LexerPass
      -> ParserPass
      -> AstPass
```

## Module Map

- [roadmap.md](/Users/caojunze424/code/SysyCC/roadmap.md): current syntax support matrix and near-term implementation priorities
- [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md): current class ownership and collaboration graph
- [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md): command line parsing and option mapping
- [common.md](/Users/caojunze424/code/SysyCC/doc/modules/common.md): shared lightweight value types
- [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md): compiler core objects and pass scheduling
- [ast.md](/Users/caojunze424/code/SysyCC/doc/modules/ast.md): AST node hierarchy, AST pass, and parse-tree lowering helpers
- [lexer.md](/Users/caojunze424/code/SysyCC/doc/modules/lexer.md): lexical analysis pass, flex template, and token output behavior
- [manual.md](/Users/caojunze424/code/SysyCC/doc/modules/manual.md): external manuals and language references
- [parser.md](/Users/caojunze424/code/SysyCC/doc/modules/parser.md): syntax analysis pass, bison grammar, and parse runtime
- [preprocess.md](/Users/caojunze424/code/SysyCC/doc/modules/preprocess.md): preprocessing pass, internal helper components, and intermediate source generation
- [scripts.md](/Users/caojunze424/code/SysyCC/doc/modules/scripts.md): developer helper scripts
- [tests.md](/Users/caojunze424/code/SysyCC/doc/modules/tests.md): test directories, helper scripts, per-case assets, and targeted bug reproducers, all runnable through the top-level regression entry, now covering include-path, nested preprocess conditionals, expression and AST lowering, pointer/member-access AST checks, AST completeness guarding, function-like macro, comment-literal, parser-extension, lexer-diagnostic, exact-token-kind, operator-mix, and lexer-structure tests
- [legacy-pass.md](/Users/caojunze424/code/SysyCC/doc/modules/legacy-pass.md): legacy compatibility files under `src/pass/`

## Current Status

- Preprocessed source dumps are written to `build/intermediate_results/*.preprocessed.sy`.
- The project can tokenize and parse a subset of SysY22.
- The preprocess stage strips `//` and `/* ... */` comments with string/character literal awareness, supports object macros, `#include "..."` with current-directory and `-I` search paths, plus `#ifdef/#ifndef/#elif/#else/#endif`.
- The preprocess stage also supports fixed-arity function-like macros such as `#define ADD(a, b) ((a) + (b))`, including `#` stringification and `##` token pasting.
- The preprocess stage evaluates simple `#if/#elif` constant expressions including identifiers, `defined(...)`, `&&`, and arithmetic such as `1 + 2`.
- The CLI can collect `-I` include directories into compiler options and the preprocess stage now consumes them for include-path resolution.
- Token dumps are written to `build/intermediate_results/*.tokens.txt`.
- Parse tree dumps are written to `build/intermediate_results/*.parse.txt`.
- AST dumps are written to `build/intermediate_results/*.ast.txt`.
- The parser now accepts a broader C-style subset including `float`, pointer declarators, `for`, `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`, and `->`.
- The AST stage now lowers core declaration, expression, and control-flow nodes such as parameters, declarations, assignments, calls, `if`, `while`, `for`, `do ... while`, `switch/case/default`, pointer declarators, `->` member access, plus parsed `struct`, `enum`, and `typedef` declarations into a compiler-facing tree.
- `AstPass` now records AST completeness in `CompilerContext` and rejects incomplete ASTs when `--dump-ast` explicitly requests AST output.
- A local HTML graph page can be generated from parse output.

## Recommended Reading Order

1. [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md)
2. [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md)
3. [preprocess.md](/Users/caojunze424/code/SysyCC/doc/modules/preprocess.md)
4. [lexer.md](/Users/caojunze424/code/SysyCC/doc/modules/lexer.md)
5. [parser.md](/Users/caojunze424/code/SysyCC/doc/modules/parser.md)
6. [ast.md](/Users/caojunze424/code/SysyCC/doc/modules/ast.md)
7. [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md)
