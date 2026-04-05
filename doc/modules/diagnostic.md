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
- preserve optional warning identities such as `sign-compare` and
  `unused-variable`
- collect diagnostics emitted by multiple passes
- apply shared warning policy controls such as suppression and warning-to-error
  promotion
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
- one optional warning option name
- one promotion marker for warnings that became errors through `-Werror`

### `DiagnosticEngine`

`DiagnosticEngine` is the shared collector owned by
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp).
It provides:

- `add_error(...)`
- `add_warning(...)`
- `add_note(...)`
- `add_diagnostic(...)`
- `set_warning_policy(...)`
- `has_error()`
- `clear()`
- `get_diagnostics()`

`DiagnosticEngine` now also owns one shared warning-policy object. When a
warning diagnostic is added, the engine can:

- suppress it because of `-Wno-...`
- enable it only when its catalog level is active through `-Wall`, `-Wextra`,
  or `-W<option>`
- keep it as a warning
- promote it to an error because of `-Werror` or `-Werror=...`

### `DiagnosticFormatter`

`DiagnosticFormatter` now provides one GCC-like default CLI rendering policy
for shared diagnostics:

- diagnostics with a source span render as
  `path:line:col: error|warning|note: message`
- diagnostics without a source span render as
  `error|warning|note: message`
- diagnostics with a warning identity append one GCC-like option suffix:
  - warnings use `[-W<option>]`
  - warning promotions use `[-Werror=<option>]`
- `Error` and `Warning` diagnostics with a single-line source span also print:
  - one source excerpt line
  - one caret / underline line
- `Note` diagnostics stay header-only by default
- multi-line spans currently fall back to header-only output
- tabs in source excerpts are expanded to fixed-width spaces before caret
  placement

This keeps the formatter stage-agnostic: stage producers now emit raw messages,
while CLI presentation is centralized in one place.

## Current Integration

The current compiler pipeline keeps its existing `PassResult` return path, but
passes now also emit unified diagnostics into the shared diagnostic engine:

- [PreprocessPass](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)
- [LexerPass](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.cpp)
- [ParserPass](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.cpp)
- [AstPass](/Users/caojunze424/code/SysyCC/src/frontend/ast/ast_pass.cpp)
- [SemanticPass](/Users/caojunze424/code/SysyCC/src/frontend/semantic/semantic_pass.cpp)

This means later tooling can inspect one shared diagnostic list without having
to special-case every compiler stage, while the CLI now presents a uniform
GCC/Clang-like surface.

The executable entry in [main.cpp](/Users/caojunze424/code/SysyCC/src/main.cpp)
now prefers this shared diagnostic list when a compilation fails. That keeps
existing `PassResult` control flow in place while allowing structured
preprocess note chains, such as nested include traces, to reach CLI output in
the same GCC-like note form as other diagnostics. Successful compilations now
also print collected non-fatal diagnostics such as preprocess `#warning`
entries through the same formatter path. After warning-policy promotion,
successful pass execution is no longer enough for an overall process success:
the entrypoint now also checks `DiagnosticEngine::has_error()` so promoted
warnings correctly force a non-zero exit code.
