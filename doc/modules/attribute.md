# Attribute Module

## Scope

The attribute module provides a parser-facing representation for GNU-style
`__attribute__((...))` syntax and a small attribute parser that lowers
attribute parse-tree fragments into structured attribute records.

## Directory Layout

```text
src/frontend/attribute/
├── attribute_analyzer.hpp
├── attribute_analyzer.cpp
├── attribute.hpp
├── attribute_parser.hpp
└── attribute_parser.cpp
```

## Responsibilities

- define structured attribute data types independent of the raw parse tree
- parse GNU-style `__attribute__((...))` syntax from parser-produced subtrees
- preserve attribute names, raw argument text, attachment sites, and source spans
- analyze preserved function attributes into supported semantic attributes or
  semantic errors
- provide a reusable interface that AST lowering can call without embedding
  attribute parsing logic directly inside the bison grammar or semantic rules

## Key Files

- [attribute_analyzer.hpp](/Users/caojunze424/code/SysyCC/src/frontend/attribute/attribute_analyzer.hpp)
- [attribute_analyzer.cpp](/Users/caojunze424/code/SysyCC/src/frontend/attribute/attribute_analyzer.cpp)
- [attribute.hpp](/Users/caojunze424/code/SysyCC/src/frontend/attribute/attribute.hpp)
- [attribute_parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/attribute/attribute_parser.hpp)
- [attribute_parser.cpp](/Users/caojunze424/code/SysyCC/src/frontend/attribute/attribute_parser.cpp)

## Current Status

- the module currently supports GNU-style `__attribute__((...))`
- parser grammar recognizes attribute specifier sequences on function
  declarations and function definitions
- AST lowering preserves parsed attribute lists on `FunctionDecl`
- semantic analysis now routes preserved function attributes through
  `AttributeAnalyzer`
- function-level `__always_inline__` is currently the one supported semantic
  attribute
- other recognized GNU function attributes currently produce semantic errors
- IR generation currently consumes function-level `__always_inline__` and lowers
  it to LLVM `alwaysinline`

## Notes

- this module intentionally separates syntax preservation from semantic meaning
- semantic analysis now adds a dedicated attribute analyzer without changing the
  parser-to-AST data flow
