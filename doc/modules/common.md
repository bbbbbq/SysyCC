# Common Module

## Scope

The common module stores lightweight shared value types that may be reused by
multiple compilation stages.

## Main Files

- [source_span.hpp](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

## Responsibilities

- represent source code location ranges
- provide a reusable span type for lexer, parser, AST, and diagnostics

## Current Types

### `SourceSpan`

`SourceSpan` models a half-open source location idea using:

- line begin
- column begin
- line end
- column end

It is currently used by [Token](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp).

