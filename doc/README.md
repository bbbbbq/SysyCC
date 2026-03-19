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
    ├── frontend.md
    ├── manual.md
    ├── scripts.md
    ├── tests.md
    └── legacy-pass.md
```
Tests live inside per-case directories under `tests/`, each bundling the `.sy` input and an executable `run.sh`.

## Project Overview

SysyCC is a small SysY22 compiler front-end prototype organized around a pass
pipeline. The current implementation focuses on these stages:

- command line parsing
- compiler option assembly
- pass scheduling
- preprocessing
- lexical analysis
- syntax analysis
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
```

## Module Map

- [roadmap.md](/Users/caojunze424/code/SysyCC/roadmap.md): current syntax support matrix and near-term implementation priorities
- [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md): current class ownership and collaboration graph
- [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md): command line parsing and option mapping
- [common.md](/Users/caojunze424/code/SysyCC/doc/modules/common.md): shared lightweight value types
- [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md): compiler core objects and pass scheduling
- [frontend.md](/Users/caojunze424/code/SysyCC/doc/modules/frontend.md): lexer, parser, grammar templates, and parse runtime
- [manual.md](/Users/caojunze424/code/SysyCC/doc/modules/manual.md): external manuals and language references
- [scripts.md](/Users/caojunze424/code/SysyCC/doc/modules/scripts.md): developer helper scripts
- [tests.md](/Users/caojunze424/code/SysyCC/doc/modules/tests.md): test directories, helper scripts, per-case assets, and targeted bug reproducers, now covering include-path, expression, function-like macro, comment-literal, parser-extension, and preprocess-regression tests
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
- The parser now accepts a broader C-style subset including `float`, pointer declarators, `for`, `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`, and `->`.
- A local HTML graph page can be generated from parse output.

## Recommended Reading Order

1. [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md)
2. [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md)
3. [frontend.md](/Users/caojunze424/code/SysyCC/doc/modules/frontend.md)
4. [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md)
