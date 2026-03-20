# Common Module

## Scope

The common module stores lightweight shared value types that may be reused by
multiple compilation stages.

## Main Files

- [source_span.hpp](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)
- [diagnostic.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.hpp)
- [diagnostic_engine.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)

## Responsibilities

- represent individual source code positions
- represent source code location ranges
- provide a reusable span type for lexer, parser, AST, semantic analysis, and diagnostics
- provide pass-independent diagnostic records and a shared diagnostic collector

## Current Types

### `SourcePosition`

`SourcePosition` models one concrete source location using:

- line
- column

### `SourceSpan`

`SourceSpan` models a source location range using:

- begin `SourcePosition`
- end `SourcePosition`

It is currently used by [Token](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp), parse-tree nodes, AST nodes, and semantic diagnostics.

### `Diagnostic`

`Diagnostic` stores:

- diagnostic level
- compiler stage
- message
- [SourceSpan](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

### `DiagnosticEngine`

`DiagnosticEngine` stores the diagnostics collected during one compiler run and
is currently owned by
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp).
