# Dialects Module

## Scope

The dialects module introduces a shared front-end dialect skeleton for
classifying standard C99 support, GNU C extensions, Clang-oriented extension
packs, and extended builtin-type packs without scattering that classification
logic across lexer, parser, AST, semantic, and IR entry points.

## Directory Layout

```text
src/frontend/dialects/
├── ast_feature_registry.hpp
├── ast_feature_registry.cpp
├── attribute_semantic_handler_registry.hpp
├── attribute_semantic_handler_registry.cpp
├── builtin_type_semantic_handler_registry.hpp
├── builtin_type_semantic_handler_registry.cpp
├── dialect.hpp
├── dialect_manager.hpp
├── dialect_manager.cpp
├── ir_extension_lowering_registry.hpp
├── ir_extension_lowering_registry.cpp
├── ir_feature_registry.hpp
├── ir_feature_registry.cpp
├── lexer_keyword_registry.hpp
├── lexer_keyword_registry.cpp
├── parser_feature_registry.hpp
├── parser_feature_registry.cpp
├── preprocess_directive_handler_registry.hpp
├── preprocess_directive_handler_registry.cpp
├── preprocess_feature_registry.hpp
├── preprocess_feature_registry.cpp
├── preprocess_probe_handler_registry.hpp
├── preprocess_probe_handler_registry.cpp
├── semantic_feature_registry.hpp
├── semantic_feature_registry.cpp
├── builtin_types/
│   ├── builtin_type_extension_pack.hpp
│   └── builtin_type_extension_pack.cpp
├── c99/
│   ├── c99_dialect.hpp
│   └── c99_dialect.cpp
├── clang/
│   ├── clang_dialect.hpp
│   └── clang_dialect.cpp
└── gnu/
    ├── gnu_dialect.hpp
    └── gnu_dialect.cpp
```

## Responsibilities

- define the abstract `FrontendDialect` contract
- aggregate enabled dialect packs through one shared `DialectManager`
- centralize preprocess feature ownership through `PreprocessFeatureRegistry`
- centralize preprocess probe-handler ownership through
  `PreprocessProbeHandlerRegistry`
- centralize lexer keyword ownership through `LexerKeywordRegistry`
- centralize parser/AST/semantic/IR feature flags through stage-specific
  feature registries
- centralize attribute semantic-handler ownership through
  `AttributeSemanticHandlerRegistry`
- centralize builtin-type semantic-handler ownership through
  `BuiltinTypeSemanticHandlerRegistry`
- provide one default dialect set for the current compiler context:
  - `C99Dialect`
  - `GnuDialect`
  - `ClangDialect`
  - `BuiltinTypeExtensionPack`

## Current Status

The current implementation has moved beyond a pure skeleton:

- `CompilerContext` owns one shared `DialectManager`
- the context constructor registers the default dialect packs
- `C99Dialect` contributes current standard-C keywords and baseline parser/AST/
  semantic features
- `GnuDialect` contributes current GNU-oriented pieces such as
  `__attribute__` and `__signed`
- `ClangDialect` currently exists as a named extension pack placeholder for
  clang-oriented behavior
- `BuiltinTypeExtensionPack` contributes extended builtin-type support such as
  `_Float16`

The registries are intentionally lightweight today:

- `PreprocessFeatureRegistry`
- `PreprocessProbeHandlerRegistry`
- `PreprocessDirectiveHandlerRegistry`
- `LexerKeywordRegistry`
- `ParserFeatureRegistry`
- `AstFeatureRegistry`
- `SemanticFeatureRegistry`
- `AttributeSemanticHandlerRegistry`
- `BuiltinTypeSemanticHandlerRegistry`
- `IrFeatureRegistry`

This means the module now already provides one shared ownership/configuration
layer plus the first concrete main-path consumers, instead of staying purely
descriptive.

The current skeleton now already provides:

- one shared preprocess-side feature registry aggregated by `DialectManager`
- one shared preprocess probe-handler registry aggregated by `DialectManager`
- one shared preprocess directive-handler registry aggregated by
  `DialectManager`
- explicit keyword-conflict recording inside `LexerKeywordRegistry`
- runtime identifier-to-keyword classification through `LexerKeywordRegistry`
  in both lexer-only and parser-attached scanner sessions
- one shared attribute semantic-handler registry aggregated by `DialectManager`
- one shared builtin-type semantic-handler registry aggregated by
  `DialectManager`
- first runtime semantic-feature consumers in `ConversionChecker`, where
  `ExtendedBuiltinTypes` and `QualifiedPointerConversions` now gate
  extension-builtin and qualifier-aware pointer rules
- one shared IR extension-lowering handler registry aggregated by
  `DialectManager`
- per-invocation dialect-set reconfiguration through compiler options and CLI
- dialect-registration error aggregation inside `DialectManager`
- first concrete consumption of shared dialect registries in preprocess,
  lexer, parser, AST, semantic, and IR main-path code

The current skeleton still does **not** yet provide:

- broader semantic/IR handler registries beyond the first attribute and GNU IR
  lowering bridges
- general IR extension lowering registries beyond the first GNU function
  attribute bridge

Those pieces are intentionally treated as the next architecture step and are
tracked in the related refactor plan below, rather than being implied as
already implemented by the current code.

## Key Files

- [dialect.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/dialect.hpp)
- [dialect_manager.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/dialect_manager.hpp)
- [preprocess_feature_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/preprocess_feature_registry.hpp)
- [preprocess_probe_handler_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/preprocess_probe_handler_registry.hpp)
- [preprocess_directive_handler_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/preprocess_directive_handler_registry.hpp)
- [lexer_keyword_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/lexer_keyword_registry.hpp)
- [parser_feature_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/parser_feature_registry.hpp)
- [attribute_semantic_handler_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/attribute_semantic_handler_registry.hpp)
- [builtin_type_semantic_handler_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/builtin_type_semantic_handler_registry.hpp)
- [ir_extension_lowering_registry.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/ir_extension_lowering_registry.hpp)
- [c99_dialect.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/c99/c99_dialect.hpp)
- [gnu_dialect.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/gnu/gnu_dialect.hpp)
- [clang_dialect.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/clang/clang_dialect.hpp)
- [builtin_type_extension_pack.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/builtin_types/builtin_type_extension_pack.hpp)

## Related Plan

- [dialect-refactor-plan.md](/Users/caojunze424/code/SysyCC/doc/modules/dialect-refactor-plan.md):
  architecture rationale, module layering, and phased implementation steps for
  the next dialect-oriented frontend refactor stages
