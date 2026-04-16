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
- [dialect_manager.hpp](/Users/caojunze424/code/SysyCC/src/frontend/dialects/core/dialect_manager.hpp)

## Key Objects

### `Complier`

The compiler orchestrator. It owns:

- one [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)
- one [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- one `PassManager`
- one shared internal option-to-context synchronization step used by the
  constructor, `set_option()`, and `Run()` before pass execution

### `ComplierOption`

The run configuration for one compilation task. It stores:

- input file
- output file
- user include search directories
- default system include search directories
- CLI-supplied system include search directories merged ahead of defaults
- dump options
- stop-after stage

### `CompilerContext`

The shared data container for passes. It stores:

- input file
- user include search directories
- system include search directories
- one [SourceManager](/Users/caojunze424/code/SysyCC/src/common/source_manager.hpp)
  for stable `SourceFile` identities across one compiler run
- one [SourceLineMap](/Users/caojunze424/code/SysyCC/src/common/source_line_map.hpp)
  storing one logical source position per emitted preprocessed output line so
  later stages can inherit preprocess `#line` remapping
- one [DialectManager](/Users/caojunze424/code/SysyCC/src/frontend/dialects/core/dialect_manager.hpp)
  registering the default `c99`, `gnu-c`, `clang`, and
  `extended-builtin-types` dialect packs, aggregating their preprocess/stage
  registries plus the first handler registries, and surfacing registration
  conflicts such as incompatible keyword ownership
- token list
- parse tree root
- ast root
- semantic model
- ir result
- diagnostic engine
- stop-after stage
- ir dump output path
- dump output paths

### `PassManager`

The scheduler and owner of pass objects. It is responsible for:

- adding passes
- grouping Core IR passes into ordinary steps or fixed-point regions
- running passes in order
- stopping cleanly after the configured pass stage when
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  requests `preprocess`, `lex`, `parse`, `ast`, `semantic`, or `ir`

## Pipeline

At the current stage, the initialized pipeline is:

```text
PreprocessPass -> LexerPass -> ParserPass -> AstPass -> SemanticPass ->
BuildCoreIrPass -> CoreIrCanonicalizePass -> CoreIrSimplifyCfgPass ->
CoreIrLoopSimplifyPass -> CoreIrInstCombinePass ->
CoreIrStackSlotForwardPass -> CoreIrDeadStoreEliminationPass ->
CoreIrInstCombinePass -> CoreIrMem2RegPass ->
[ CoreIrCopyPropagationPass -> CoreIrInstCombinePass -> CoreIrSccpPass ->
  CoreIrSimplifyCfgPass -> CoreIrLicmPass -> CoreIrLocalCsePass ->
  CoreIrGvnPass -> CoreIrInstCombinePass -> CoreIrConstFoldPass ->
  CoreIrDcePass -> CoreIrSimplifyCfgPass ] x<=4 -> LowerIrPass
```

## Notes

- The file and class names currently use `Complier` instead of `Compiler`.
- `PassResult` carries pass success state and a short message.
- [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  also owns one shared diagnostic engine so passes can emit stage-tagged
  diagnostics through one uniform interface.
- [main.cpp](/Users/caojunze424/code/SysyCC/src/main.cpp) now prefers printing
  the shared diagnostic engine on failure and only falls back to
  `PassResult.message` when no diagnostics were recorded.
- [main.cpp](/Users/caojunze424/code/SysyCC/src/main.cpp) now delegates that
  rendering to
  [DiagnosticFormatter](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_formatter.hpp)
  instead of keeping stage-specific formatting rules inline.
- the executable now also prints non-fatal shared diagnostics, such as
  preprocess `#warning`, on successful compilation runs.
- CLI-provided `-isystem` directories are merged into
  [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)
  ahead of the default system include directories and then copied into
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  for preprocess include resolution.
- The backend stage now branches after optimized Core IR: the default path still
  emits textual LLVM IR for the supported subset, while
  `-S --backend=aarch64-native` or `-S --backend=riscv64-native` run native
  Linux backends over the same lowered LLVM IR artifacts.
- The public driver now also carries an explicit optimization level. `-O0`
  keeps only the minimum Core IR normalization required by later lowering,
  while `-O1` enables the current canonicalize/const-fold/DCE batch over the
  built Core IR before lowering continues.
- That hot path now also carries staged pointer arithmetic, pointer
  differences, top-level constant global-address initializers, union-backed
  aggregate storage, and variadic default-argument promotions before LLVM text
  emission.
- The executable hot path is now `BuildCoreIrPass -> CoreIrCanonicalizePass ->
  CoreIrSimplifyCfgPass -> CoreIrLoopSimplifyPass ->
  CoreIrInstCombinePass -> CoreIrStackSlotForwardPass ->
  CoreIrDeadStoreEliminationPass -> CoreIrInstCombinePass ->
  CoreIrMem2RegPass -> post-SSA fixed-point group -> LowerIrPass ->
  native codegen pass`.
  That fixed-point group is
  `CopyPropagation -> InstCombine -> Sccp -> SimplifyCfg -> Licm ->
  LocalCse -> Gvn -> InstCombine -> ConstFold -> Dce -> SimplifyCfg`,
  iterated up to four rounds until one full round makes no Core IR changes.
  `LowerIrPass` is now LLVM-specific and materializes stable `.ll` / `.bc`
  artifacts before the native backend adapters run. The native AArch64 path
  lives under
  [src/backend/asm_gen/aarch64](/Users/caojunze424/code/SysyCC/src/backend/asm_gen/aarch64),
  while the standalone RISC-V64 path lives under
  [src/backend/asm_gen/riscv64](/Users/caojunze424/code/SysyCC/src/backend/asm_gen/riscv64).
  The legacy `IRBuilder -> IRBackend -> LlvmIrBackend` stack remains in tree as
  a reference implementation during the migration.
- `CoreIrCanonicalizePass` now only keeps the pre-SSA hard-structure
  normalization that later memory / CFG passes depend on, mainly explicit `i1`
  branch conditions plus direct stack-slot load/store address forms.
- `CoreIrSimplifyCfgPass` now owns the conservative CFG cleanup portion of the
  staged Core IR pipeline, including constant / redundant branch collapse,
  jump trampoline removal, unreachable-block cleanup, and conservative linear
  block merging, and it is now `phi`-aware so it can safely repeat after SSA
  passes inside the fixed-point lane.
- The staged optimization lane now also includes:
  - `CoreIrLoopSimplifyPass` for pre-SSA loop preheader / latch / dedicated
    exit normalization
  - `CoreIrInstCombinePass` for repeated value-level cleanup, local
    fold/combine, trivial-phi elimination, compare/cast cleanup, boolean
    wrapper cleanup, and stack-slot access-shape normalization
  - `CoreIrStackSlotForwardPass` for conservative direct stack-slot
    store-to-load forwarding
  - `CoreIrDeadStoreEliminationPass` for conservative overwritten direct
    stack-slot store cleanup
  - `CoreIrMem2RegPass` for SSA promotion plus block-head phi insertion
  - `CoreIrCopyPropagationPass` for copy-like duplicate-value reuse only,
    leaving trivial phi / identity-cast cleanup to `CoreIrInstCombinePass`
  - `CoreIrSccpPass` for SSA sparse conditional constant propagation without
    direct CFG rewrites
  - `CoreIrLicmPass` for hoist-only loop-invariant code motion over simplified
    SSA loops, including conservative invariant-load hoisting
  - `CoreIrLocalCsePass` for cheap block-local pure-value cleanup ahead of GVN
  - `CoreIrGvnPass` for dominator-based global reuse of pure SSA
    computations
  - `CoreIrConstFoldPass` for CFG-facing constant branch folding only, leaving
    pure value folding to `CoreIrInstCombinePass`
- `CoreIrBuildResult` now also carries a staged Core IR analysis manager for
  function-level CFG, dominator, dominance-frontier, and promotable-stack-slot
  queries, with transform passes invalidating cached analyses conservatively
  after IR mutation.
- `LowerIrPass` now fails fast when the active IR backend cannot lower a
  required function body, function declaration, or global object. Unsupported
  IR is reported through the shared diagnostic engine and stops compilation
  instead of silently emitting a partial `.ll` file.
- `LowerIrPass` now also treats post-validation emission failures as fatal. If
  a validated function or global later fails during backend emission, the
  compiler records a compiler-stage diagnostic and aborts IR generation
  instead of returning a truncated module.
- `ComplierOption` and
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  now also carry backend-selection state, native-asm emission intent, one
  optional target triple, and one optimized-Core-IR dump switch in addition to
  the older LLVM IR dump flag.
- [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  now also constructs one shared
  [SourceLocationService](/Users/caojunze424/code/SysyCC/src/common/source_location_service.hpp),
  which in turn builds downstream
  [SourceMappingView](/Users/caojunze424/code/SysyCC/src/common/source_mapping_view.hpp)
  instances for lexer/parser scanner sessions. Those views now expose explicit
  physical and logical location queries.
- [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  now also owns one shared
  [DialectManager](/Users/caojunze424/code/SysyCC/src/frontend/dialects/core/dialect_manager.hpp)
  so C99/GNU/Clang/builtin-type distinctions can be queried through one shared
  front-end service instead of being rediscovered independently inside each
  stage, and so preprocess capability flags now travel through the same
  dialect-oriented aggregation path as lexer/parser/AST/semantic/IR feature
  registries. The same service now also exposes the first preprocess-probe,
  preprocess-directive, attribute-semantic, builtin-type-semantic, and
  IR-extension handler ownership registries.
- `Complier::Run()` now validates dialect registration state before pass
  execution. If keyword or handler ownership conflicts were recorded during
  dialect registration, the compiler emits compiler-stage diagnostics and
  fails fast instead of starting the pipeline with an inconsistent dialect
  configuration.
- `ComplierOption` now also carries dialect-pack selection flags, and
  `Complier` reconfigures the compiler context's dialect set per invocation
  before validation and pass execution.
- `ComplierOption` and
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  now also carry one stop-after stage, and
  [PassManager](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.hpp)
  stops the pipeline immediately after that pass succeeds. This keeps
  parser/AST/semantic-focused tests from being blocked by intentionally
  unsupported later-stage IR.
