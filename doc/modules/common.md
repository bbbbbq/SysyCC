# Common Module

## Scope

The common module stores lightweight shared value types that may be reused by
multiple compilation stages.

## Main Files

- [source_manager.hpp](/Users/caojunze424/code/SysyCC/src/common/source_manager.hpp)
- [source_location_service.hpp](/Users/caojunze424/code/SysyCC/src/common/source_location_service.hpp)
- [source_line_map.hpp](/Users/caojunze424/code/SysyCC/src/common/source_line_map.hpp)
- [source_mapping_view.hpp](/Users/caojunze424/code/SysyCC/src/common/source_mapping_view.hpp)
- [source_span.hpp](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)
- [integer_literal.hpp](/Users/caojunze424/code/SysyCC/src/common/integer_literal.hpp)
- [string_literal.hpp](/Users/caojunze424/code/SysyCC/src/common/string_literal.hpp)
- [diagnostic.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.hpp)
- [diagnostic_engine.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)

## Responsibilities

- intern shared source-file identities
- represent individual source code positions
- represent source code location ranges
- represent one logical source position per emitted logical line
- provide one shared front-end source-location service over file identity plus
  preprocess remapping state
- provide one shared downstream view over physical files plus preprocess line remapping
- provide one shared integer-literal parser for ordinary front-end literal text
- provide one shared string-literal decoder for semantic sizing and LLVM byte
  emission
- provide a reusable span type for lexer, parser, AST, semantic analysis, and diagnostics
- provide pass-independent diagnostic records and a shared diagnostic collector

## Current Types

### `SourceFile`

`SourceFile` models one stable source-file identity using:

- source path

Its instances are currently interned through
[source_manager.hpp](/Users/caojunze424/code/SysyCC/src/common/source_manager.hpp),
so later source positions and spans can keep lightweight file pointers without
copying path strings into every location object.

### `SourcePosition`

`SourcePosition` models one concrete source location using:

- source file
- line
- column

### `SourceSpan`

`SourceSpan` models a source location range using:

- begin `SourcePosition`
- end `SourcePosition`

It is currently used by [Token](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp), parse-tree nodes, AST nodes, and semantic diagnostics.

Current user-visible formatting now commonly renders spans as:

- `<path>:<line_begin>:<col_begin>-<line_end>:<col_end>`

### `SourceLocationService`

`SourceLocationService` owns no source data by itself. Instead, it ties
together:

- one shared [SourceManager](/Users/caojunze424/code/SysyCC/src/common/source_manager.hpp)
- one shared [SourceLineMap](/Users/caojunze424/code/SysyCC/src/common/source_line_map.hpp)

It is currently owned by
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
and is the single place that constructs downstream
[SourceMappingView](/Users/caojunze424/code/SysyCC/src/common/source_mapping_view.hpp)
instances.

### `SourceLineMap`

`SourceLineMap` models one line-granularity mapping from emitted logical lines
to their logical
[SourcePosition](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp).

It is currently used to carry preprocess `#line` remapping through
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
and into lexer/parser scanner sessions.

### `SourceMappingView`

`SourceMappingView` models one downstream-facing source-mapping service view
that combines:

- one physical `SourceFile`
- one optional `SourceLineMap`

It is currently constructed by
[SourceLocationService](/Users/caojunze424/code/SysyCC/src/common/source_location_service.hpp)
and consumed by
[LexerState](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp).

It now exposes explicit physical and logical queries so later front-end stages
do not have to guess whether a position lookup is meant to preserve the emitted
preprocess file location or the remapped `#line` location.

### `SourceManager`

`SourceManager` owns the stable `SourceFile` identities for one compiler run
and is currently stored in
[CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp),
then exposed through
[SourceLocationService](/Users/caojunze424/code/SysyCC/src/common/source_location_service.hpp).

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

### `parse_integer_literal(...)`

[integer_literal.hpp](/Users/caojunze424/code/SysyCC/src/common/integer_literal.hpp)
now provides one shared helper that parses ordinary decimal, octal, and
hexadecimal integer literals, including standard `U`/`L`/`LL` suffix
combinations.

It is currently used by:

- semantic integer-literal constant binding
- semantic integer constant folding
- IR integer-literal lowering

### `decode_string_literal_token(...)`

[string_literal.hpp](/Users/caojunze424/code/SysyCC/src/common/string_literal.hpp)
now provides one shared helper that decodes one tokenized string literal body
into its byte sequence, including the currently modeled simple escape set.

It is currently used by:

- semantic string-literal type sizing for `char[N]`
- LLVM string-literal global emission
- IR array initialization from string literals
