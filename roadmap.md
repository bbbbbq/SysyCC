# SysyCC Roadmap

## Purpose

This file records the current implemented state of `SysyCC` and the next
stage-by-stage milestones. It should stay aligned with the real codebase and
regression coverage rather than an aspirational architecture.

## Project Direction

- `SysyCC` is intended to support both `SysY22` and `C` language workflows.
- Near-term implementation choices should prefer shared infrastructure that can
  serve both targets instead of hard-coding the project around one narrow
  subset.
- Feature work should continue to be driven by focused regression tests before
  implementation and verification.

## Current Pipeline

```text
source file
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

## Current Implemented State

### Compiler core and tooling

- CMake builds one `SysyCC` executable and generates the lexer/parser with
  Flex/Bison.
- The compiler core owns one shared `CompilerContext` carrying diagnostics,
  source mapping, dialect configuration, semantic results, Core IR build
  results, and final IR output.
- A shared `DialectManager` now centralizes keyword, feature, and handler
  ownership, and the compiler fails fast when dialect registration leaves the
  frontend in an invalid conflict state.
- CLI/compiler options already support include-path configuration, dump flags,
  stop-after-stage execution, and dialect-pack toggles.
- The project has stage-grouped regression suites, runtime cases, dialect
  architecture regressions, static-check entry points, and a differential fuzz
  workspace.

### Preprocess

- Comment stripping works for both `//` and `/* ... */` while preserving string
  and character literal contents.
- Supported directives include `#define`, `#undef`, `#include`,
  `#include_next`, `#error`, `#line`, `#pragma once`, ordinary `#pragma`, and
  the `#if/#elif/#elifdef/#elifndef/#ifdef/#ifndef/#else/#endif` family.
- Object-like and function-like macros already support variadics,
  backslash-continued definitions, nested expansion, stringification, and token
  pasting.
- Conditional-expression evaluation already covers identifiers, `defined(...)`,
  arithmetic, bitwise, shifts, logical operators, the comma operator, ternary
  `?:`, and builtin probes such as `__has_include(...)` and common clang-style
  feature probes.
- Include resolution already uses current-directory, `-I`, `-isystem`, default
  system search paths, and preprocess-local source remapping through a shared
  `SourceLineMap`.
- The preprocess stage now seeds a minimal predefined macro set for better
  system-header compatibility, including common integer-limit spellings.

### Lexer and dialect classification

- The lexer already handles identifiers, decimal/octal/hex integer literals
  with standard suffixes, floating literals, character literals, string
  literals, and the current C-style operator/punctuator set.
- Token kinds and source spans are preserved for downstream passes and dump
  output.
- Keyword and feature classification now flows through the shared dialect
  manager instead of being rediscovered independently inside each stage.

### Parser

- The parser accepts mixed translation units containing declarations and
  function definitions.
- Supported declaration forms now cover scalars, arrays, pointer declarators,
  initializer lists, `struct`, `union`, `enum`, `typedef`, declaration-only
  `extern` / `inline` prototypes, variadic signatures, grouped function-pointer
  declarators, `extern` variables, and several builtin type spellings used by
  real headers.
- Supported statements and expressions include `if`, `while`, `for`,
  `do ... while`, `switch/case/default`, `break`, `continue`, `return`, calls,
  indexing, comma expressions, `.` / `->` member access, prefix/postfix `++ --`,
  shifts, bitwise operators, C-style casts, and ordinary ternary `?:`.
- The current C-compatible grammar also accepts GNU-style attributes, GNU
  asm-labeled prototypes, qualifier-rich pointer declarators, and parser-seeded
  bootstrap typedef names such as `size_t` and `va_list`.
- Parse trees can be dumped to `build/intermediate_results/*.parse.txt`.

### AST

- The AST pass lowers core declaration, expression, and control-flow nodes into
  a compiler-facing tree, including parameters, declarations, initializer
  lists, assignments, calls, conditional `?:`, loops, `switch/case/default`,
  pointer declarators, and `struct` / `union` / `enum` / `typedef`
  declarations.
- Source spans are propagated from parse nodes into AST nodes.
- `AstPass` records AST completeness in `CompilerContext` and rejects explicit
  `--dump-ast` requests when the lowered AST is incomplete.
- AST dumps are written to `build/intermediate_results/*.ast.txt`.

### Semantic

- `SemanticPass` installs builtin runtime-library symbols, creates a
  `SemanticModel`, and records symbol/type bindings plus foldable integer
  constant-expression values.
- The semantic layer already diagnoses undefined identifiers, redefinitions,
  invalid call targets, arity/type mismatches, invalid assignments, invalid
  returns, bad unary/binary/index/member operands, invalid ternary branches,
  invalid `break` / `continue` / `case` / `default` placement, duplicate switch
  labels, missing returns, non-constant array dimensions and case labels,
  pointer-arithmetic misuse, and null-pointer assignment mistakes.
- Implemented semantic behavior already includes string literals as `char[N]`,
  array-to-pointer and function-designator decay, enum/union-aware type
  modeling, file-scope linkage tracking, pointer-compatibility warnings for
  mismatched pointee types, qualifier/nullability preservation on pointer
  types, and end-to-end unary bitwise-not integer promotions.
- Semantic diagnostics now flow through the shared compiler diagnostic engine.

### Core IR and lowering

- The production backend hot path is now the explicit pass sequence
  `BuildCoreIrPass -> CoreIrCanonicalizePass -> CoreIrConstFoldPass ->
  CoreIrDcePass -> LowerIrPass`.
- `src/backend/ir/shared/core/` provides a staged Core IR object model for
  modules, globals, functions, blocks, stack slots, types, constants, and
  instructions, plus a stable raw printer for regression tests.
- `CoreIrBuilder` already lowers a meaningful staged subset including top-level
  scalar and pointer globals, local scalar/array/struct/union declarations,
  character arrays, direct and indirect calls, pointer arithmetic and pointer
  differences, array indexing, scalar struct/union member access,
  `goto`/labels, `switch`, compound assignments, and the current supported
  control-flow forms.
- The canonicalization, constant-fold, and DCE passes are now real explicit
  stages rather than placeholders, with conservative cleanup over branch
  conditions, cast chains, GEP chains, CFG structure, and stack-slot address
  forms.
- The staged LLVM backend already lowers the supported Core IR subset into LLVM
  IR text, including direct/indirect calls, variadic-call signatures and
  default-argument promotions, pointer-difference lowering, string literals,
  enum storage, union-backed aggregate storage, and top-level constant address
  initializers.
- Unsupported IR is handled fail-fast: validation or later emission errors now
  surface as compiler diagnostics instead of silently producing partial
  modules.
- The AArch64 target backend exists as an explicit placeholder that reports a
  diagnostic rather than silently pretending lowering is implemented.
- IR dumps are written to `build/intermediate_results/*.ll` when `--dump-ir` is
  enabled.

### Testing and verification

- Tests are grouped by stage under `tests/preprocess`, `tests/lexer`,
  `tests/parser`, `tests/ast`, `tests/semantic`, `tests/ir`, `tests/run`,
  `tests/dialects`, and `tests/fuzz`.
- Runtime tests compile emitted LLVM IR together with runtime support and verify
  stdin/stdout behavior.
- Dialect-focused regressions cover registration, feature gating, handler
  ownership, and CLI-to-dialect option mapping.
- The top-level `tests/run_all.sh` entry builds once, runs discovered cases in
  parallel, verifies expected artifacts, and writes `build/test_result.md`.
- The fuzz workspace supports differential execution against host `clang` and
  archives per-case artifacts and summaries.

## Known Gaps

- Full C preprocessor compatibility is still incomplete, especially around full
  builtin-probe parity, pragma semantics beyond `#pragma once`, and industrial
  strength downstream logical-location remapping.
- Some accepted front-end syntax is still not uniformly end-to-end through AST,
  semantic analysis, Core IR, and final lowering.
- Richer qualifier semantics, declaration compatibility, conversion rules,
  aggregate initialization rules, and overflow-sensitive constant evaluation are
  still incomplete.
- Broader integer-width/signedness coverage, fuller array/aggregate/object
  lowering, and fuller floating/runtime integration are still incomplete.
- The compiler still stops at LLVM IR text emission; a real assembly/object/link
  driver pipeline is not yet in place.
- The staged AArch64 backend remains diagnostic-only.

## Stage-by-Stage Milestones

### Milestone 1 — Preprocess compatibility hardening

Goal: unblock more real headers and generated C inputs.

Concrete work items:

- Expand predefined macro coverage and builtin-probe behavior used by current
  Darwin/libc headers, especially `__has_*`-style queries and compiler/target
  compatibility spellings.
- Turn each newly discovered header-compatibility failure into a dedicated
  regression under `tests/preprocess` or a minimized reproducer under
  `tests/fuzz`.
- Finish logical source-location propagation from `#line` and nested include
  stacks so later lexer/parser/semantic diagnostics consistently report logical
  file and line information.
- Make pragma behavior explicit: implement the pragmas we want to honor, and
  diagnose-or-ignore the rest consistently instead of leaving them as ad hoc
  compatibility cases.
- Add targeted header-smoke regressions around representative real-header
  snippets so compatibility improvements do not silently regress.

### Milestone 2 — Front-end completeness

Goal: make parser-accepted syntax consistently lower through the front-end.

Concrete work items:

- Use `UnknownDecl`, `UnknownStmt`, `UnknownExpr`, and `UnknownTypeNode` cases as
  the queue for AST-completeness work, and retire them case by case.
- Close remaining parser-to-AST gaps around declarators, type names, casts,
  qualifiers, grouped function declarators, and pointer-heavy declaration
  shapes already accepted by the grammar.
- For every construct that moves from parser-only to lowered support, add
  matching regressions in both `tests/parser` and `tests/ast`.
- Keep dialect registries aligned with implementation: newly supported
  extensions should also gain explicit parser/AST/semantic ownership and a
  gating regression under `tests/dialects`.
- Update support documentation when a construct becomes front-end complete so
  roadmap and module docs stay synchronized.

### Milestone 3 — Semantic correctness

Goal: make the front-end trustworthy for richer real-world C programs.

Concrete work items:

- Extend `ConversionChecker`, `TypeResolver`, `IntegerConversionService`, and
  `ConstantEvaluator` for the remaining implicit-conversion and
  usual-arithmetic-conversion edge cases.
- Tighten function/global redeclaration compatibility and qualifier-aware
  assignment, return, and call checking.
- Expand constant-expression coverage for array dimensions, enumerators,
  initializers, and `switch` labels, including range- and overflow-sensitive
  diagnostics.
- Broaden aggregate initializer analysis for arrays, structs, and unions,
  including element-count, shape, and type-compatibility checks.
- Keep warning-vs-error policy explicit with regression coverage for pointer
  compatibility, qualifier dropping, and other intentional C-compatibility
  diagnostics.

### Milestone 4 — Core IR coverage expansion

Goal: move more ordinary C code onto the staged Core IR path.

Concrete work items:

- Extend `CoreIrBuilder` and staged lowering for the remaining integer-width and
  signedness paths that are already semantically accepted but not yet emitted
  end to end.
- Implement fuller array and aggregate lowering, especially nested initializers,
  aggregate assignment flow, and richer global-object emission.
- Expand floating lowering and runtime coverage for `_Float16`, `float`,
  `double`, and `long double` through both `tests/ir` and `tests/run`.
- Keep each newly supported construct covered at three levels: semantic
  acceptance, staged/raw IR regression, and executable runtime behavior where
  applicable.
- Preserve fail-fast diagnostics for unsupported nodes so partial lowering never
  looks like successful compilation.

### Milestone 5 — Backend and driver maturity

Goal: turn LLVM IR emission into a more complete compilation flow.

Concrete work items:

- Add explicit compile / emit / assemble / link driver steps around the current
  LLVM IR emission path instead of stopping at `.ll` output.
- Decide how temporary artifacts, stop-after stages, and dump outputs should
  behave once assembly/object generation exists.
- Broaden runtime-library and ABI handling for external declarations,
  aggregates, floating arguments/results, and variadic calls.
- Add end-to-end binary-production tests in addition to current `.ll` and
  runtime-from-LLVM coverage.
- Keep the backend boundary centered on `CoreIrTargetBackend` so new driver work
  does not re-entangle frontend code with one concrete emitter.

### Milestone 6 — Optimization and retargetability

Goal: make Core IR the durable optimization boundary.

Concrete work items:

- Grow canonicalization, constant folding, and DCE one transform family at a
  time, each backed by stable raw-Core-IR golden tests.
- Define the expected post-pass invariants for Core IR so optimization work has
  a clear contract.
- Add optimization regressions covering CFG cleanup, address simplification,
  constant propagation, dead-branch removal, and redundant memory traffic.
- Start replacing the AArch64 placeholder with real lowering for function
  skeletons, integer operations, branches, and calls.
- Retire the legacy reference-only lowering stack once the staged Core IR path
  covers the same supported surface and the regression suite no longer depends
  on the legacy path.

## Working Rules

- Update this roadmap whenever implemented status or milestone ordering changes.
- Prefer test-backed milestone increments over large speculative rewrites.
- If a feature is parser-only, compatibility-only, or not yet end-to-end, call
  that out explicitly until the gap is closed.
