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

## Project Overview

SysyCC is a small SysY22 compiler front-end prototype organized around a pass
pipeline. The current implementation focuses on these stages:

- command line parsing
- compiler option assembly
- pass scheduling
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
      -> LexerPass
      -> ParserPass
```

## Module Map

- [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md): current class ownership and collaboration graph
- [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md): command line parsing and option mapping
- [common.md](/Users/caojunze424/code/SysyCC/doc/modules/common.md): shared lightweight value types
- [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md): compiler core objects and pass scheduling
- [frontend.md](/Users/caojunze424/code/SysyCC/doc/modules/frontend.md): lexer, parser, grammar templates, and parse runtime
- [manual.md](/Users/caojunze424/code/SysyCC/doc/modules/manual.md): external manuals and language references
- [scripts.md](/Users/caojunze424/code/SysyCC/doc/modules/scripts.md): developer helper scripts
- [tests.md](/Users/caojunze424/code/SysyCC/doc/modules/tests.md): test inputs and one-click run scripts
- [legacy-pass.md](/Users/caojunze424/code/SysyCC/doc/modules/legacy-pass.md): legacy compatibility files under `src/pass/`

## Current Status

- The project can tokenize and parse a subset of SysY22.
- Token dumps are written to `build/intermediate_results/*.tokens.txt`.
- Parse tree dumps are written to `build/intermediate_results/*.parse.txt`.
- A local HTML graph page can be generated from parse output.

## Recommended Reading Order

1. [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md)
2. [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md)
3. [frontend.md](/Users/caojunze424/code/SysyCC/doc/modules/frontend.md)
4. [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md)
