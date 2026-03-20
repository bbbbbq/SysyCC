# Diagnostic Module

## Scope

The diagnostic module provides one shared diagnostic data model and collection
interface for compiler passes.

## Main Files

- [diagnostic.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.hpp)
- [diagnostic.cpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic.cpp)
- [diagnostic_engine.hpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)
- [diagnostic_engine.cpp](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.cpp)

## Responsibilities

- define one pass-independent diagnostic record
- classify diagnostics by level and compiler stage
- collect diagnostics emitted by multiple passes
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
