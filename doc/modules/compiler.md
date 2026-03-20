# Compiler Module

## Scope

The compiler module is the project core. It owns the compiler options, the
shared context, and the pass manager.

## Main Files

- [complier.hpp](/Users/caojunze424/code/SysyCC/src/compiler/complier.hpp)
- [complier.cpp](/Users/caojunze424/code/SysyCC/src/compiler/complier.cpp)
- [complier_option.hpp](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)
- [compiler_context.hpp](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- [pass.hpp](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.hpp)
- [pass.cpp](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.cpp)

## Key Objects

### `Complier`

The compiler orchestrator. It owns:

- one [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)
- one [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- one `PassManager`

### `ComplierOption`

The run configuration for one compilation task. It stores:

- input file
- output file
- user include search directories
- default system include search directories
- dump options

### `CompilerContext`

The shared data container for passes. It stores:

- input file
- user include search directories
- system include search directories
- token list
- parse tree root
- ast root
- semantic model
- ir result
- diagnostic engine
- ir dump output path
- dump output paths

### `PassManager`

The scheduler and owner of pass objects. It is responsible for:

- adding passes
- rejecting duplicate `PassKind`
- running passes in order

## Pipeline

At the current stage, the initialized pipeline is:

```text
PreprocessPass -> LexerPass -> ParserPass -> AstPass -> SemanticPass -> IRGenPass
```

## Notes

- The file and class names currently use `Complier` instead of `Compiler`.
- `PassResult` carries pass success state and a short message.
- [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  also owns one shared diagnostic engine so passes can emit stage-tagged
  diagnostics through one uniform interface.
- The backend stage currently emits textual LLVM IR dumps for the supported AST
  subset, including multi-branch `switch` lowering.
