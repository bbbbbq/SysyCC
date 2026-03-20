# Common Module

## Scope

The common module stores lightweight shared value types that may be reused by
multiple compilation stages.

## Main Files

- [source_span.hpp](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

## Responsibilities

- represent individual source code positions
- represent source code location ranges
- provide a reusable span type for lexer, parser, AST, and diagnostics

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
