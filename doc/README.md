# SysyCC Documentation

## Tree

```text
doc/
├── README.md
└── modules/
    ├── attribute.md
    ├── class-relationships.md
    ├── cli.md
    ├── common.md
    ├── compiler.md
    ├── dialect-refactor-plan.md
    ├── dialects.md
    ├── ast.md
    ├── diagnostic.md
    ├── ir.md
    ├── lexer.md
    ├── manual.md
    ├── parser.md
    ├── preprocess.md
    ├── semantic.md
    ├── scripts.md
    ├── tests.md
    └── legacy-pass.md
```
Tests now live inside stage-specific case directories under `tests/<stage>/<case>/`, each bundling the `.sy` input and an executable `run.sh`.
The test tree now also includes [tests/dialects](/Users/caojunze424/code/SysyCC/tests/dialects) for small architecture-focused regressions around the shared dialect manager and registry behavior.
The runtime stage additionally provides `tests/run/support/runtime_stub.c` so execution-oriented cases can compile emitted LLVM IR and validate stdin/stdout behavior across direct I/O, loops, `switch`, short-circuit control flow, and a growing set of scalarized data-structure scenarios such as stacks, queues, ring buffers, linked-list traversal, BST lookup, and map-style dispatch.
Each runtime case stores copied intermediate artifacts and the final linked test executable under `tests/run/<case>/build/`.
The separate [tests/fuzz](/Users/caojunze424/code/SysyCC/tests/fuzz) workspace now provides `generate_and_build_csmith_cases.sh` plus `run_csmith_cases.sh`: the first appends new numbered fuzz-input directories after the highest existing numeric case id, optionally compiles them, and supports local path overrides through environment variables, while the second performs a differential run between host `clang` and `SysyCC` for either one chosen case like `001`, every numbered directory via `all`, or all numbered directories when invoked with no arguments, accepts both plain decimal and zero-padded case ids such as `8` and `008`, now discovers any pure-numeric fuzz directory in numeric order so mixed-width layouts such as `001 ... 200 0201 ... 0999 1000 ... 1200` are included, resolves explicit decimal requests such as `201` back to the existing on-disk directory name like `0201`, runs those requested cases in parallel by default using the detected logical CPU count unless `RUN_FUZZ_JOBS` overrides it, archives compiler logs, runtime stdout/stderr, exit codes, and a Markdown summary report in `tests/fuzz/result.md` by default or `SYSYCC_FUZZ_RESULT_FILE`, keeps `SysyCC` frontend and IR dump artifacts disabled by default unless `SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=full` explicitly opts in, and can optionally print a concise per-case artifact summary in the terminal.
Shared assertions for success-path test scripts live in [tests/test_helpers.sh](/Users/caojunze424/code/SysyCC/tests/test_helpers.sh).
That shared helper now also prefers Ninja plus `ccache` when available and
coordinates one local build per `build/` directory at a time, so overlapping
test runs wait for the in-flight compile instead of racing.
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
- semantic analysis
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
      -> SemanticPass
      -> BuildCoreIrPass
      -> CoreIrCanonicalizePass
      -> CoreIrConstFoldPass
      -> CoreIrDcePass
      -> LowerIrPass
```

## Module Map

- [attribute.md](/Users/caojunze424/code/SysyCC/doc/modules/attribute.md): GNU-style attribute parsing and structured attribute records
- [roadmap.md](/Users/caojunze424/code/SysyCC/roadmap.md): current implemented state and staged delivery milestones
- [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md): current class ownership and collaboration graph
- [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md): command line parsing and option mapping
- [common.md](/Users/caojunze424/code/SysyCC/doc/modules/common.md): shared lightweight value types
- [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md): compiler core objects and pass scheduling
- [dialect-refactor-plan.md](/Users/caojunze424/code/SysyCC/doc/modules/dialect-refactor-plan.md): staged architecture plan for dialect-oriented frontend modularization
- [dialects.md](/Users/caojunze424/code/SysyCC/doc/modules/dialects.md): shared front-end dialect manager, dialect packs, and stage feature registries
- [ast.md](/Users/caojunze424/code/SysyCC/doc/modules/ast.md): AST node hierarchy, AST pass, and parse-tree lowering helpers
- [diagnostic.md](/Users/caojunze424/code/SysyCC/doc/modules/diagnostic.md): shared diagnostic records and the compiler-wide diagnostic engine
- [ir.md](/Users/caojunze424/code/SysyCC/doc/modules/ir.md): modular IR-generation skeleton with an abstract backend and an initial LLVM IR target
- [lexer.md](/Users/caojunze424/code/SysyCC/doc/modules/lexer.md): lexical analysis pass, flex template, and token output behavior
- [manual.md](/Users/caojunze424/code/SysyCC/doc/modules/manual.md): external manuals and language references
- [parser.md](/Users/caojunze424/code/SysyCC/doc/modules/parser.md): syntax analysis pass, bison grammar, and parse runtime
- [preprocess.md](/Users/caojunze424/code/SysyCC/doc/modules/preprocess.md): preprocessing pass, internal helper components, and intermediate source generation
- [semantic.md](/Users/caojunze424/code/SysyCC/doc/modules/semantic.md): semantic pass, semantic model, scope management, builtin symbol installation, and first semantic rules
- [scripts.md](/Users/caojunze424/code/SysyCC/doc/modules/scripts.md): developer helper scripts
- [tests.md](/Users/caojunze424/code/SysyCC/doc/modules/tests.md): stage-grouped test directories, helper scripts, and per-case assets, all runnable through the top-level regression entry
- [legacy-pass.md](/Users/caojunze424/code/SysyCC/doc/modules/legacy-pass.md): legacy compatibility files under `src/pass/`

## Current Status

- Preprocessed source dumps are written to `build/intermediate_results/*.preprocessed.sy`.
- The project can tokenize and parse a subset of SysY22.
- The preprocess stage strips `//` and `/* ... */` comments with string/character literal awareness, supports object macros, `#include "..."`, `#include <...>`, `#include_next <...>`, and `#error` with current-directory, `-I`, `-isystem`, and default system include search paths, where angle includes now search `-I` before system directories, plus `#ifdef/#ifndef/#elif/#else/#endif`.
- Internally, the preprocess module is now being split by responsibility, with dedicated `detail/conditional/` helpers for builtin probe handling plus a non-standard extension manager/provider layer, `detail/directive/` helpers for directive execution, a dedicated `PreprocessContext` that centralizes mutable preprocessing state, and a `detail/source/` mapper that tracks include nesting and preprocess-local `#line` remapping.
- The preprocess stage also supports fixed-arity and variadic function-like macros such as `#define ADD(a, b) ((a) + (b))` and `#define LOG(...) __VA_ARGS__`, including continued macro definitions with trailing `\`, `#` stringification, and `##` token pasting.
- The preprocess stage now seeds a minimal host/compiler predefined macro set
  for system-header compatibility, distinguishes when `#if` versus `#elif`
  conditions should be evaluated inside inactive conditional regions, and
  tolerates conflicting macro redefinitions only when both definitions come
  from system-header paths. That seeded set now also includes standard
  integer limit macros and builtin numeric spellings used by host
  `limits.h` implementations.
- The preprocess stage evaluates simple `#if/#elif` constant expressions including identifiers, `defined(...)`, arithmetic, bitwise operators, shifts, and logical operators such as `&&`, tolerates `__has_include(...)` / `__has_include_next(...)` plus common clang-style builtin probes such as `__has_feature(...)`, `__has_attribute(...)`, and `__has_cpp_attribute(...)` in system-header guards, remaps preprocess-local diagnostics through `#line` logical file and line state, and now exports emitted-line logical source positions through a shared `SourceLineMap` so later lexer/parser/semantic spans can inherit preprocess file/line remapping.
- The compiler core now also owns a shared `SourceManager` plus
  `SourceLocationService`, and downstream lexer/parser scanner sessions consume
  one shared `SourceMappingView` instead of manually pairing a physical file
  with a preprocess line map.
- The compiler core now also owns one shared `DialectManager`, which registers
  the default `c99`, `gnu-c`, `clang`, and `extended-builtin-types` packs and
  centralizes stage-specific keyword and feature classification, with lexer and
  parser scanner sessions now consuming one shared runtime keyword registry for
  identifier-to-keyword classification, plus the first preprocess-probe,
  preprocess-directive, attribute-semantic, builtin-type-semantic, and
  IR-extension handler ownership registries.
- Parser and AST passes now also consume that shared dialect service as runtime
  feature validators, and the compiler refuses to start the pipeline when
  dialect registration produced ownership conflicts.
- CLI and compiler options can now also reconfigure the active dialect-pack
  set per invocation, including strict C99 mode and explicit GNU/Clang/
  builtin-type pack toggles.
- The CLI can collect `-I` include directories and `-isystem` system include directories into compiler options and the preprocess stage now consumes them for include-path resolution.
- The top-level [Makefile](/Users/caojunze424/code/SysyCC/Makefile) now provides `make check`, which runs `clang-tidy`, `cppcheck`, and `include-what-you-use` through helper scripts under [scripts/](/Users/caojunze424/code/SysyCC/scripts).
- The static-check pipeline excludes generated parser headers from blocking `clang-tidy` diagnostics and keeps `cppcheck` focused on warning/performance/portability findings.
- Token dumps are written to `build/intermediate_results/*.tokens.txt`.
- Parse tree dumps are written to `build/intermediate_results/*.parse.txt`.
- AST dumps are written to `build/intermediate_results/*.ast.txt`.
- semantic results are stored in memory as a `SemanticModel` attached to `CompilerContext`.
- pass-independent diagnostics are stored in memory as a `DiagnosticEngine` attached to `CompilerContext`.
- the executable now prefers those shared diagnostics for failure output, which
  lets preprocess include-trace notes surface in CLI diagnostics alongside the
  primary error, and the rendering policy now lives in a dedicated
  `DiagnosticFormatter`
- successful runs now also surface shared non-fatal diagnostics such as
  preprocess `#warning` through the same formatter path
- IR results are now stored in memory as an `IRResult` attached to `CompilerContext`.
- The backend tree now also contains a first standalone Core IR foundation
  under `src/backend/ir/shared/core/` and a raw textual printer under
  `src/backend/ir/shared/printer/`. These classes are currently regression-tested
  directly and are intended to become the future optimization boundary, while
  the production compiler path still emits LLVM text through `IRBuilder` and
  `IRBackend`.
- That staged Core IR tree now also includes a first `CoreIrBuilder`, which
  can already lower small frontend-complete function bodies, top-level and
  local scalar variables, direct calls, staged string literals, integer
  comparisons, unary integer expressions, and structured `if` / `while` /
  `do-while` / `for` control flow into raw Core IR for dedicated regression
  tests, and it now also forms the executable backend hot path.
- That staged Core IR tree now also models explicit function/global/stack
  addresses, local pointer dereference stores, and indirect function-pointer
  calls, so a larger part of ordinary C address semantics is already covered
  on the production IR hot path.
- That staged Core IR tree now also lowers array indexing and scalar struct
  member access through explicit address derivation, and its aggregate Core IR
  struct types now support placeholder construction so recursive semantic
  aggregate types can be represented safely before real optimization work
  begins.
- The staged backend architecture now also includes
  `src/backend/ir/` for explicit top-level Core IR build, optimization,
  and lowering stages, with the retargetable target backends nested under
  `src/backend/ir/lower/lowering/`. The staged LLVM backend already
  lowers the current Core IR subset into LLVM IR, while the staged AArch64
  backend is present as an explicit diagnostic-only placeholder until real ARM
  lowering begins.
- The parser now accepts a broader C-style subset including `float`, `_Float16`, pointer declarators, `for`, `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`, ordinary ternary `?:`, both `.` / `->` member access, declaration-only `extern` / `inline` function prototypes, `extern` variable declarations, declaration-side builtin forms such as `signed char`, `short`, `unsigned char`, and `unsigned short`, and GNU-style function attributes in declaration-specifier position.
- The lexer and ordinary front-end constant handling now also accept standard integer literal suffixes such as `u`, `UL`, and `LL` in decimal, octal, and hexadecimal literals.
- The AST stage now lowers core declaration, expression, and control-flow nodes such as parameters, declarations, assignments, conditional `?:`, calls, `if`, `while`, `for`, `do ... while`, `switch/case/default`, pointer declarators, `.` / `->` member access, plus parsed `struct`, `enum`, and `typedef` declarations into a compiler-facing tree.
- `AstPass` now records AST completeness in `CompilerContext` and rejects incomplete ASTs when `--dump-ast` explicitly requests AST output.
- `SemanticPass` now installs builtin runtime-library symbols, creates a semantic model, records symbol/type bindings and foldable integer constant-expression values over complete ASTs, rejects semantic errors such as undefined identifiers, redefinitions, non-function call targets, call arity/type mismatches, assignment type/lvalue mismatches, return mismatches, missing return paths in non-void functions, invalid binary/condition/index/unary operands, invalid ternary conditions or incompatible ternary branch types, invalid `break` / `continue` / `case` / `default` placement, duplicate `case` / `default` labels inside one `switch`, non-constant array dimensions and `case` labels, array-to-pointer decay mismatches, invalid pointer arithmetic, invalid null-pointer assignments, and invalid or missing `.` / `->` member access, and skips strict checking when AST lowering is still incomplete.
- Semantic analysis now models string literals as `char[N]`, allows
  function-designator decay in call/initializer compatibility, accepts
  pointer-to-function call targets, and lets enum-typed objects flow through
  assignment and return sites as ordinary integer-like values.
- Unary bitwise-not now follows C integer promotions end to end, so narrow
  integer operands still promote to `int` while `long long` operands keep
  their full width through semantic analysis and LLVM IR lowering.
- Simple parameter-side `const` qualifiers such as `const char *` are now
  preserved through AST and semantic type construction, with semantic pointer
  compatibility allowing `char * -> const char *` but rejecting
  `const char * -> char *`.
- The backend is now split into explicit top-level passes:
  `BuildCoreIrPass -> CoreIrCanonicalizePass -> CoreIrConstFoldPass ->
  CoreIrDcePass -> LowerIrPass`. `CompilerContext` stores both the staged
  `CoreIrBuildResult` and the final `IRResult`, and the current lowering path
  still targets LLVM IR for the supported subset of integer/void functions,
  integer locals, arithmetic and comparisons, short-circuit logical
  expressions, integer ternary expressions, assignments, direct function
  calls, and basic `if` / `while` / `for` / `do-while` / `switch` /
  `break` / `continue` control flow.
- The current LLVM IR path now also lowers enum storage through `i32`,
  supports local/global character-array initialization from string literals,
  and supports indirect calls through lowered function-pointer values.
- Function-level GNU-style `__always_inline__` attributes preserved by the AST
  now lower to LLVM `alwaysinline` in emitted IR through semantic
  function-attribute bindings, while other currently recognized GNU function
  attributes are rejected during semantic analysis.
- The LLVM IR backend now also emits top-level `declare` statements for runtime-style external calls such as builtin `getint`, `putint`, and `putch`, which lets runtime regression cases compile emitted `.ll` into host executables.
- LLVM IR dumps now also start with host target-module headers
  (`target datalayout` and `target triple`), and variadic call sites emit
  explicit function-type signatures such as `call i32 (ptr, ...) @printf(...)`
  so host-linked `stdio.h` calls execute correctly on Darwin AArch64.
- IR dumps are written to `build/intermediate_results/*.ll` when `--dump-ir` is enabled.
- A local HTML graph page can be generated from parse output.

## Recommended Reading Order

1. [compiler.md](/Users/caojunze424/code/SysyCC/doc/modules/compiler.md)
2. [class-relationships.md](/Users/caojunze424/code/SysyCC/doc/modules/class-relationships.md)
3. [diagnostic.md](/Users/caojunze424/code/SysyCC/doc/modules/diagnostic.md)
4. [preprocess.md](/Users/caojunze424/code/SysyCC/doc/modules/preprocess.md)
5. [lexer.md](/Users/caojunze424/code/SysyCC/doc/modules/lexer.md)
6. [parser.md](/Users/caojunze424/code/SysyCC/doc/modules/parser.md)
7. [ast.md](/Users/caojunze424/code/SysyCC/doc/modules/ast.md)
8. [semantic.md](/Users/caojunze424/code/SysyCC/doc/modules/semantic.md)
9. [ir.md](/Users/caojunze424/code/SysyCC/doc/modules/ir.md)
10. [cli.md](/Users/caojunze424/code/SysyCC/doc/modules/cli.md)
