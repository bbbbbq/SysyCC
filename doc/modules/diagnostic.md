# Diagnostic Module

## Scope

The diagnostic module provides one shared diagnostic data model and collection
interface for compiler passes.

## Main Files

- [diagnostic.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.hpp)
- [diagnostic.cpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.cpp)
- [diagnostic_engine.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)
- [diagnostic_engine.cpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.cpp)
- [diagnostic_formatter.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_formatter.hpp)
- [diagnostic_formatter.cpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_formatter.cpp)

## Responsibilities

- define one pass-independent diagnostic record
- classify diagnostics by level and compiler stage
- collect diagnostics emitted by multiple passes
- preserve stage-local note chains such as preprocess include traces alongside
  primary errors
- format shared diagnostics for CLI-oriented human-readable output
- provide a single storage point through [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)

## Key Types

### `Diagnostic`

`Diagnostic` represents one compiler diagnostic entry. It stores:

- one `DiagnosticLevel`
- one `DiagnosticStage`
- one message string
- one [SourceSpan](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

### `DiagnosticEngine`

`DiagnosticEngine` is the shared collector owned by
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp).
It provides:

- `add_error(...)`
- `add_warning(...)`
- `add_note(...)`
- `add_diagnostic(...)`
- `has_error()`
- `clear()`
- `get_diagnostics()`

### `DiagnosticFormatter`

`DiagnosticFormatter` provides the current CLI-oriented formatting policy for
shared diagnostics. It is intentionally thin:

- it first resolves one explicit `DiagnosticCliFormatPolicy` for each
  diagnostic instead of hard-coding stage branches directly inside the print
  loop
- that policy is now split into:
  - one message policy
  - one span policy
  so later warning/note rendering can evolve without rewriting the formatter
  control flow
- semantic diagnostics with a source span are rendered as
  `semantic error: ... at <span>`
- warnings with a source span are rendered as
  `<stage> warning: ... at <span>`
- diagnostics that already carry a user-ready message, such as preprocess
  include-trace notes or lexer invalid-token messages, are passed through as-is
- the executable entry can print one whole `DiagnosticEngine` through a single
  formatter entry point instead of keeping per-stage formatting logic in
  `main.cpp`

## Current Integration

The current compiler pipeline keeps its existing `PassResult` return path, but
passes now also emit unified diagnostics into the shared diagnostic engine:

- [PreprocessPass](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)
- [LexerPass](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.cpp)
- [ParserPass](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.cpp)
- [AstPass](/Users/caojunze424/code/SysyCC/src/frontend/ast/ast_pass.cpp)
- [SemanticPass](/Users/caojunze424/code/SysyCC/src/frontend/semantic/semantic_pass.cpp)

This means later tooling can inspect one shared diagnostic list without having
to special-case every compiler stage.

The executable entry in [main.cpp](/Users/caojunze424/code/SysyCC/src/main.cpp)
now prefers this shared diagnostic list when a compilation fails. That keeps
existing `PassResult` control flow in place while allowing structured
preprocess note chains, such as nested `included from ...` context, to reach
CLI output without packing every stage-specific detail into one flat error
string. Successful compilations now also print collected non-fatal diagnostics
such as preprocess `#warning` entries through the same formatter path.
