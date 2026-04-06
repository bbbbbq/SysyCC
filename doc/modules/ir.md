# IR Module

## Scope

The IR module prepares the backend stage after semantic analysis. Its current
goal is to expose a modular IR-generation skeleton that can target LLVM IR now
without hard-wiring the pass to LLVM-specific interfaces.

## Directory Layout

```text
src/backend/ir/
├── analysis/
│   ├── analysis_manager.hpp
│   ├── analysis_manager.cpp
│   ├── cfg_analysis.hpp
│   ├── cfg_analysis.cpp
│   ├── core_ir_analysis_kind.hpp
│   ├── dominance_frontier_analysis.hpp
│   ├── dominance_frontier_analysis.cpp
│   ├── dominator_tree_analysis.hpp
│   ├── dominator_tree_analysis.cpp
│   ├── promotable_stack_slot_analysis.hpp
│   └── promotable_stack_slot_analysis.cpp
├── build/
│   ├── build_core_ir_pass.hpp
│   └── build_core_ir_pass.cpp
├── canonicalize/
│   ├── core_ir_canonicalize_pass.hpp
│   └── core_ir_canonicalize_pass.cpp
├── copy_propagation/
│   ├── core_ir_copy_propagation_pass.hpp
│   └── core_ir_copy_propagation_pass.cpp
├── const_fold/
│   ├── core_ir_const_fold_pass.hpp
│   └── core_ir_const_fold_pass.cpp
├── dead_store_elimination/
│   ├── core_ir_dead_store_elimination_pass.hpp
│   └── core_ir_dead_store_elimination_pass.cpp
├── dce/
│   ├── core_ir_dce_pass.hpp
│   └── core_ir_dce_pass.cpp
├── gvn/
│   ├── core_ir_gvn_pass.hpp
│   └── core_ir_gvn_pass.cpp
├── local_cse/
│   ├── core_ir_local_cse_pass.hpp
│   └── core_ir_local_cse_pass.cpp
├── mem2reg/
│   ├── core_ir_mem2reg_pass.hpp
│   └── core_ir_mem2reg_pass.cpp
├── sccp/
│   ├── core_ir_sccp_pass.hpp
│   └── core_ir_sccp_pass.cpp
├── simplify_cfg/
│   ├── core_ir_simplify_cfg_pass.hpp
│   └── core_ir_simplify_cfg_pass.cpp
├── stack_slot_forward/
│   ├── core_ir_stack_slot_forward_pass.hpp
│   └── core_ir_stack_slot_forward_pass.cpp
├── lower/
│   ├── lower_ir_pass.hpp
│   ├── lower_ir_pass.cpp
│   ├── legacy/
│   │   ├── ir_backend.hpp
│   │   ├── ir_backend_factory.hpp
│   │   ├── ir_backend_factory.cpp
│   │   ├── ir_builder.hpp
│   │   ├── ir_builder.cpp
│   │   ├── gnu_function_attribute_lowering_handler.hpp
│   │   ├── gnu_function_attribute_lowering_handler.cpp
│   │   └── llvm/
│   └── lowering/
│       ├── core_ir_target_backend.hpp
│       ├── core_ir_target_backend_factory.hpp
│       ├── core_ir_target_backend_factory.cpp
│       ├── aarch64/
│       │   ├── core_ir_aarch64_target_backend.hpp
│       │   └── core_ir_aarch64_target_backend.cpp
│       └── llvm/
│           ├── core_ir_llvm_target_backend.hpp
│           └── core_ir_llvm_target_backend.cpp
└── shared/
    ├── core/
    │   ├── core_ir_builder.hpp
    │   ├── core_ir_builder.cpp
    │   ├── ir_basic_block.hpp
    │   ├── ir_constant.hpp
    │   ├── ir_context.hpp
    │   ├── ir_function.hpp
    │   ├── ir_global.hpp
    │   ├── ir_instruction.hpp
    │   ├── ir_module.hpp
    │   ├── ir_stack_slot.hpp
    │   ├── ir_type.hpp
    │   └── ir_value.hpp
    ├── detail/
    │   ├── aggregate_layout.hpp
    │   ├── aggregate_layout.cpp
    │   ├── ir_context.hpp
    │   ├── ir_context.cpp
    │   ├── symbol_value_map.hpp
    │   └── symbol_value_map.cpp
    ├── printer/
    │   ├── core_ir_raw_printer.hpp
    │   └── core_ir_raw_printer.cpp
    ├── ir_kind.hpp
    └── ir_result.hpp
```

## Responsibilities

- expose explicit top-level backend passes for build, optimize, and lower
- keep IR-generation orchestration separate from one concrete IR backend
- store IR results in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- prepare a backend interface that currently maps to LLVM IR but can be
  replaced later

## Current Design

The current IR module is intentionally a staged optimization pipeline:

- `BuildCoreIrPass`, `CoreIrCanonicalizePass`, `CoreIrSimplifyCfgPass`,
  `CoreIrLoopSimplifyPass`, `CoreIrInstCombinePass`,
  `CoreIrStackSlotForwardPass`, `CoreIrDeadStoreEliminationPass`,
  `CoreIrMem2RegPass`, one post-SSA fixed-point group containing
  `CoreIrCopyPropagationPass`, `CoreIrInstCombinePass`, `CoreIrSccpPass`,
  `CoreIrSimplifyCfgPass`, `CoreIrLicmPass`, `CoreIrLocalCsePass`,
  `CoreIrGvnPass`, `CoreIrConstFoldPass`, `CoreIrDcePass`, and a trailing
  `CoreIrSimplifyCfgPass`, plus `LowerIrPass`, are connected to the main
  pipeline after `SemanticPass`
- `analysis/CoreIrAnalysisManager` now owns function-level Core IR analysis
  caches for the current staged analyses, while the outer `PassManager` now
  invalidates them from pass-reported `CoreIrPassEffects` /
  `CoreIrPreservedAnalyses`
- `analysis/CoreIrCfgAnalysis` now centralizes predecessor/successor and
  reachability facts for one `CoreIrFunction`
- `analysis/CoreIrDominatorTreeAnalysis` now computes a first function-local
  dominator view over the current reachable CFG
- `analysis/CoreIrDominanceFrontierAnalysis` now computes dominance-frontier
  edges over that reachable CFG for SSA construction
- `analysis/CoreIrPromotableStackSlotAnalysis` now classifies promotable
  whole-slot and constant-path stack-slot subobject units for `mem2reg`
- `CoreIrCanonicalizePass` is now intentionally narrow and only preserves
  pre-SSA structural invariants such as explicit `i1` branch conditions plus
  direct stack-slot load/store address forms that later memory passes depend on
- `CoreIrSimplifyCfgPass` now owns conservative CFG cleanup after shape
  canonicalization, including constant / redundant branch collapse, trampoline
  removal, unreachable-block cleanup, and single-predecessor linear block
  merging, and it now keeps successor `phi` nodes in sync when CFG edges or
  blocks change
- `CoreIrInstCombinePass` now acts as the value-level normalization center for
  Core IR, folding trivial `phi`, cast, compare, boolean-wrapper, GEP, and
  small algebraic patterns without changing CFG structure
- `CoreIrStackSlotForwardPass` now forwards block-local direct stack-slot
  stores into later direct stack-slot loads until a conservative barrier is
  seen
- `CoreIrCopyPropagationPass` now reuses duplicate block-local direct loads and
  duplicate address materializations so later passes see fewer redundant
  temporaries, but it now leaves trivial `phi` / identity-cast cleanup to
  `CoreIrInstCombinePass`
- `CoreIrSccpPass` now performs value-only sparse conditional constant
  propagation over SSA `phi`, `binary`, `unary`, `compare`, `cast`, and
  branch-condition users, leaving later CFG cleanup to `CoreIrConstFoldPass`
  and `CoreIrDcePass`
- `CoreIrLicmPass` now performs hoist-only loop-invariant code motion over the
  simplified SSA pipeline, moving pure invariant computations and
  memory-safe invariant loads into loop preheaders
- `CoreIrLocalCsePass` now acts as a cheap block-local cleanup lane ahead of
  global value numbering, still limited to pure `binary`, `unary`, `compare`,
  `cast`, and `gep` instructions
- `CoreIrGvnPass` now reuses dominated pure SSA computations globally over the
  dominator tree for `binary`, `unary`, `compare`, `cast`, and `gep`
- `CoreIrConstFoldPass` now only performs CFG-facing constant branch folding
  plus successor-`phi` incoming cleanup, leaving pure value folding to
  `CoreIrInstCombinePass`
- `CoreIrDeadStoreEliminationPass` now removes overwritten direct stack-slot
  stores when no intervening read or barrier makes the older store observable
- `shared/core/` now also supports `CoreIrPhiInst`, so staged Core IR can
  represent SSA merge values directly
- `CoreIrMem2RegPass` now promotes eligible stack-slot memory into SSA values
  plus block-head `phi` instructions before the post-SSA optimization lane
- zero-initialized local/global aggregate objects now collapse through one
  canonical `zeroinitializer` constant/store path instead of eagerly
  expanding every element into repeated zero payloads
- `IRBuilder` coordinates IR generation through an abstract `IRBackend`
- `IRBackend` defines backend-independent emission hooks
- `LlvmIrBackend` is the first concrete backend implementation
- `shared/core/` now provides a first backend-independent Core IR object model for
  modules, functions, basic blocks, values, instructions, constants, globals,
  stack slots, and types
- `shared/core/CoreIrBuilder` can now build a first staged Core IR module directly
  from the completed AST plus semantic model, without going through the
  production LLVM-text backend path
- the optimization boundary now lives directly in the top-level compiler pass
  sequence, so Core IR construction, optimization, and target lowering are all
  visible to the outer `PassManager`
- `lower/lowering/CoreIrTargetBackend` now defines the retargetable Core-IR backend
  boundary, with one staged LLVM backend plus one explicit AArch64
  placeholder backend
- `shared/printer/CoreIrRawPrinter` can dump that Core IR into a stable textual
  representation for regression tests and future raw/optimized IR dumps
- `IRBuilder` now validates that every top-level function/global requiring
  lowering is supported by the active backend before emission starts, and it
  reports a compiler-stage diagnostic instead of silently skipping unsupported
  IR work
- `detail::AggregateLayoutInfo` centralizes shared struct/union layout facts so
  both generic lowering and LLVM emission use the same element indexes,
  padding, and bit-field storage units
- `IRBuilder` and `LlvmIrBackend` now share one semantic-side
  `IntegerConversionService` to classify supported integer coercions before
  emitting backend casts
- `IRBackend` now also owns top-level global declaration/definition emission,
  so the generic IR builder can lower `extern` declarations and global
  definitions without embedding LLVM-specific module syntax
- semantic function attributes can now flow from `SemanticModel` into
  backend-independent IR emission
- GNU function-attribute lowering now flows through a dedicated lowering
  handler selected by the shared dialect-managed IR extension registry
- IR function-attribute lowering is now also gated by the shared
  `IrFeatureRegistry`, so the lowering handler is only consulted when the
  dialect layer explicitly enables function-attribute IR support
- GNU asm-labeled function prototypes now lower through their external symbol
  spelling, so calls to declarations such as
  `int strerror(int) __asm("_strerror");` emit `_strerror` in LLVM IR
- variadic function declarations and definitions now lower through LLVM `...`
  signatures for both `declare` and `define`
- `IRResult` stores:
  - `IrKind`
  - IR text output
- `detail/IRContext` and `detail/SymbolValueMap` provide transient state and
  value bookkeeping slots for future lowering work
- the shared `common/string_literal` helper now feeds both semantic sizing and
  LLVM byte emission, so string-literal object size and emitted bytes stay in
  sync
- the new Core IR foundation now owns the executable hot path through explicit
  top-level backend passes, while the legacy `IRBuilder -> IRBackend ->
  LlvmIrBackend` path remains in tree as a reference implementation during the
  migration

## Current Status

At this stage the IR module provides the structural foundation plus one growing
LLVM IR lowering path:

- `CompilerContext` can store one `IRResult`
- `CompilerContext` can store one IR dump file path
- `CompilerContext` now tracks the `--dump-ir` switch
- `shared/core/` can already represent:
  - modules
  - globals
  - functions
  - parameters
  - stack slots
  - basic blocks
  - integer, float, pointer, array, struct, and function types
  - integer, null, byte-string, and aggregate constants
  - `phi`, binary, unary, compare, load, store, call, jump,
    conditional-jump, and return instructions
- `CoreIrValue` now records use lists as operands are attached to
  `CoreIrInstruction`
- `CoreIrBasicBlock` now reports whether its final instruction is a terminator
- `CoreIrRawPrinter` now prints stable textual Core IR for:
  - globals and constant initializers
  - function signatures
  - stack-slot declarations
  - block labels
  - binary instructions
  - unary instructions
  - compare instructions
  - function-address materialization
  - global-address materialization
  - stack-slot-address materialization
  - `getelementptr`-style address derivation
  - stack-slot and address-based loads and stores
  - direct and indirect calls
  - unconditional jumps
  - conditional branches
  - returns
- `CompilerContext` now stores one `CoreIrBuildResult` between backend stages
- `CoreIrBuildResult` now also owns one `CoreIrAnalysisManager` alongside the
  staged module/context so function analyses stay tied to one Core IR build
- `CoreIrCanonicalizePass`, `CoreIrSimplifyCfgPass`,
  `CoreIrLoopSimplifyPass`, `CoreIrInstCombinePass`,
  `CoreIrStackSlotForwardPass`, `CoreIrDeadStoreEliminationPass`,
  `CoreIrMem2RegPass`, one post-SSA fixed-point group, and `LowerIrPass` now
  run as explicit top-level compiler passes over one built Core IR module
- `CoreIrCanonicalizePass` currently handles:
  - branch condition normalization for compare-like and integer-valued
    `CondJump` conditions until every branch sees an explicit `i1` producer
  - direct stack-slot load/store canonicalization even when the address still
    carries a trivial zero-index `GEP` wrapper chain
  - plain stack-slot address load/store canonicalization
- post-canonicalization callers may now rely on these invariants:
  - every `CondJump` condition is an explicit `i1` SSA value
  - stack-slot loads and stores no longer hide behind trivial address wrappers
- `CoreIrSimplifyCfgPass` currently handles:
  - constant-condition branch collapse before later CFG cleanup
  - redundant `condbr x, B, B` collapse
  - non-entry unconditional jump trampoline elimination
  - non-entry unreachable block cleanup after branch simplification and target
    rewrites, including successor-`phi` incoming cleanup
  - conservative single-predecessor linear block merging with trivial
    single-predecessor `phi` elimination
- `CoreIrInstCombinePass` currently handles:
  - trivial `phi` cleanup such as `phi(x)` and `phi(x, x, ...)`
  - identity-cast elimination and safe integer cast-chain collapse
  - integer `binary`, `unary`, `compare`, and `cast` constant folding
  - compare orientation normalization and boolean-wrapper cleanup
  - zero-index GEP elimination and structurally-safe nested GEP flattening
  - direct stack-slot load/store address-shape cleanup through trivial wrapper
    addresses
- `CoreIrStackSlotForwardPass` currently handles:
  - block-local forwarding from one direct stack-slot `store` into later direct
    stack-slot `load`
  - conservative invalidation of tracked stack-slot values on calls and
    unknown-address stores
- `CoreIrCopyPropagationPass` currently handles:
  - block-local duplicate direct-stack-slot load reuse
  - block-local duplicate `addr_of_function`, `addr_of_global`, and
    `addr_of_stackslot` reuse
- `CoreIrSccpPass` currently handles:
  - SSA lattice propagation over `phi`, `binary`, `unary`, `compare`, and
    `cast`
  - executable-block and executable-edge discovery from branch conditions
  - replacement of constant SSA users, leaving CFG reshaping to later passes
- `CoreIrLicmPass` currently handles:
  - hoist-only loop-invariant code motion into loop preheaders
  - pure `binary`, `unary`, `compare`, `cast`, `addr_of_*`, and `gep`
    instructions once all operands are loop-invariant
  - direct-stack-slot and address-based `load` hoisting only when the address
    is loop-invariant, alias analysis can classify the location, `MemorySSA`
    finds no loop-internal clobber, and loop-internal memory writers do not
    alias the loaded location
- `CoreIrLocalCsePass` currently handles:
  - block-local common-subexpression elimination for pure `binary`, `unary`,
    `compare`, `cast`, and `gep` instructions as a cheap pre-GVN cleanup
- `CoreIrGvnPass` currently handles:
  - dominator-tree-scoped reuse of pure `binary`, `unary`, `compare`, `cast`,
    and `gep` instructions
  - conservative scope boundaries so non-dominating values are not reused
- `CoreIrConstFoldPass` currently handles:
  - constant `CondJump -> Jump` folding
  - successor-`phi` incoming cleanup for the removed CFG edge
- `CoreIrDeadStoreEliminationPass` currently handles:
  - conservative removal of overwritten direct stack-slot stores inside one
    block when no intervening read or barrier makes the older store observable
- `CoreIrCfgAnalysis` currently reports:
  - one entry block per function
  - per-block predecessor and successor lists
  - per-block reachability from the entry block
- `CoreIrDominatorTreeAnalysis` currently reports:
  - `dominates(a, b)` over reachable blocks
  - one immediate dominator per reachable non-entry block
- `CoreIrDominanceFrontierAnalysis` currently reports:
  - dominance-frontier sets for reachable blocks
  - frontier-edge membership queries for SSA placement
- `CoreIrPromotableStackSlotAnalysis` currently reports:
  - promotable whole-slot units
  - promotable constant-path aggregate subobject units
  - rejected slot categories such as escaped addresses, dynamic indices, and
    overlapping paths
- `CoreIrMem2RegPass` currently handles:
  - whole-slot scalar/pointer/float promotion
  - constant-path aggregate subobject promotion for non-escaping stack slots
  - insertion of block-head `phi` nodes from dominance frontiers
  - rename over the dominator tree
  - cleanup of fully promoted direct load/store traffic
- `CoreIrTargetBackend` now exposes one backend-independent lowering boundary
  from optimized Core IR into an `IRResult`
- `CoreIrLlvmTargetBackend` now lowers the current staged subset into LLVM IR
  text, including integer globals, stack slots through `alloca`, integer
  binary and unary operations, integer comparisons, direct and indirect calls,
  jumps, conditional branches, direct returns, staged function/global/stack
  addresses through LLVM value references, staged string literals through
  direct LLVM global references plus `getelementptr`, dynamic array indexing,
  scalar struct-member address derivation, multi-branch `switch` compare
  chains, pointer-difference lowering through `ptrtoint/sub/sdiv`, internal
  linkage on staged function definitions and declarations, and explicit
  direct-call function signatures for variadic callees such as `printf`
- the native `AArch64AsmGenPass` path now lowers one growing Linux AArch64
  subset directly from `CoreIrModule`, including integer/pointer codegen,
  floating-point scalar call/return lowering, `_Float16` arithmetic through
  float32 legalization, `long double` / fp128 helper-call lowering for common
  constants, casts, arithmetic, and compares, variadic call signatures that do
  not read `va_list`, and aggregate by-value ABI cases through direct-register,
  HFA, and indirect/sret conventions where supported
- `CoreIrBuilder` currently lowers a deliberately small staged subset:
  - `TranslationUnit`
  - top-level scalar `VarDecl` with constant initializer or zero-initialized
    storage
  - top-level `FunctionDecl`, including prototype-only declarations
  - top-level `StructDecl`, `UnionDecl`, `EnumDecl`, and `TypedefDecl` as
    accepted type-only declarations that do not themselves emit staged IR
  - `ParamDecl`
  - `BlockStmt`
  - `DeclStmt` containing local scalar, array, and struct `VarDecl`
  - `ExprStmt`
  - `IfStmt`
  - `WhileStmt`
  - `DoWhileStmt`
  - `ForStmt`
  - `BreakStmt`
  - `ContinueStmt`
  - `GotoStmt`
  - `LabelStmt`
  - `SwitchStmt`
  - `ReturnStmt`
  - `IntegerLiteralExpr`
  - `CharLiteralExpr`
  - `IdentifierExpr`
  - `StringLiteralExpr` lowered through staged private globals plus a
    `CoreIrAddressOfGlobalInst` / `CoreIrGetElementPtrInst` pair
  - `IndexExpr` for array and pointer indexing through explicit staged `gep`
    indices
  - `MemberExpr` for non-bit-field scalar struct members through shared
    aggregate layout
  - `UnaryExpr` for `& * + - ! ~`
  - `PrefixExpr` / `PostfixExpr` for `++` and `--`
  - `AssignExpr`, including compound assignments such as `+=`, `>>=`, and
    `&=`, with identifier, unary-dereference, index, and struct-member targets
  - `CallExpr` for direct function-designator calls and loaded
    pointer-to-function values
  - `BinaryExpr` for `+ - * / % & | ^ << >>`
  - pointer arithmetic for `pointer +/- integer` and `pointer - pointer`
  - `BinaryExpr` for `== != < <= > >=`
- staged global-scalar identifiers now lower through explicit
  `addr_of_global` plus address-based `load/store`, while local scalars still
  lower through explicit `addr_of_stackslot` plus address-based `load/store`
- staged top-level pointer globals can now lower constant address initializers
  such as `&g`, `&items[1]`, `&pair.right`, and null-pointer constants such as
  `(void*)0`
- staged top-level pointer globals now also accept canonical array-decay plus
  constant-offset forms such as `items + 1`, reusing the same constant-address
  normalization path as explicit `&items[1]`
- top-level aggregate initializers can now also carry nested pointer constants
  such as `{&g}` and `{&items[1]}`
- top-level character arrays can now lower constant string-literal
  initializers into staged Core IR globals
- local character arrays can now lower string-literal initializers into
  explicit staged element stores
- local/global array and struct aggregate initializers now share the same
  recursive initializer cursoring helpers inside `CoreIrBuilder`, so omitted
  braces, nested aggregates, union-first-field packing, and zero-fill rules
  stay aligned across store-sequence lowering and global-constant emission
- function identifiers now lower through explicit `addr_of_function` values so
  local function-pointer initializers and indirect calls can flow through the
  same Core IR object model as other addresses
- function parameters now also spill into staged stack slots at function entry,
  which makes parameter identifiers, `&param`, and future writable-parameter
  lowering share one uniform storage path
- staged array objects now decay from their lvalue storage address into a first
  element pointer through explicit `CoreIrGetElementPtrInst`, so indexing and
  ordinary C array-to-pointer decay already share one staged address path
- staged struct semantic types now lower into `CoreIrStructType` through the
  shared aggregate layout service, with placeholder construction allowing
  self-referential pointer fields without recursive type-construction failure
- staged union semantic types now lower into storage-oriented
  `CoreIrStructType` carriers that share the same aggregate-layout service as
  struct lowering, which keeps global/local union initializers and union member
  addressing on one shared layout path
- variadic staged calls now apply default argument promotions before LLVM
  lowering, so cases such as `(float)2.5` and `(short)3` lower as `double` and
  promoted integer arguments at the call site
- `CoreIrBuilder` is strict about unsupported nodes and emits compiler-stage
  diagnostics instead of silently dropping unsupported declarations,
  statements, expressions, or semantic types
- `BuildCoreIrPass` runs after semantic analysis, followed by explicit Core IR
  optimization passes and then `LowerIrPass`
- `IRBuilder` currently lowers a focused AST subset:
  - `TranslationUnit`
  - `FunctionDecl` with supported scalar, aggregate, or void return type
  - `ParamDecl`
  - `BlockStmt`
  - `DeclStmt` containing supported scalar or aggregate `VarDecl`
  - `ExprStmt`
  - `IntegerLiteralExpr`
  - `IdentifierExpr`
  - `StringLiteralExpr`
  - `BinaryExpr` for `+ - * / % < <= > >= == != && ||`
    - including the comma operator `,`
  - `CastExpr` for the currently supported scalar cast subset, including
    integer, pointer, `_Float16`, `float`, `double`, and `long double`
    conversions
  - `ConditionalExpr` for integer ternary `?:`
  - `AssignExpr` with identifier and member-expression targets
    - including compound assignments such as `+=`, `>>=`, and `&=`
  - `CallExpr` whose callee is either a direct function designator or a
    lowered pointer-to-function expression
    - including fixed-parameter aggregate arguments such as `union` values
  - `UnaryExpr` for `&`, `*`, `+`, `-`, `!`, and `~`
  - `PrefixExpr` for `++` and `--`
  - `PostfixExpr` for `++` and `--`
  - `IndexExpr`
  - `MemberExpr` for `.` and `->`
  - `IfStmt`
  - `WhileStmt`
  - `ForStmt`
  - `DoWhileStmt`
  - `SwitchStmt`
  - `CaseStmt`
  - `DefaultStmt`
  - `BreakStmt`
  - `ContinueStmt`
  - `return;`
  - `ReturnStmt`
- when the current IR backend cannot lower a required function body, function
  prototype, or global object, IR generation now fails fast with a diagnostic
  instead of emitting a partial module and continuing
- even after top-level support validation succeeds, any later emission-stage
  failure inside global-object lowering or function lowering now aborts IR
  generation immediately with a compiler-stage diagnostic instead of silently
  returning a truncated module
- `LlvmIrBackend` now emits minimal textual LLVM IR for:
  - host-target LLVM module headers through `target datalayout = "..."`
    and `target triple = "..."`, so host `clang` can assemble and link
    variadic libc calls with the correct ABI
  - `int main() { return 0; }`
  - `void main() { return; }`
  - integer local-variable allocation, store, and load
  - private LLVM string-literal globals emitted as
    `private unnamed_addr constant [N x i8] c"...\00"` plus
    `getelementptr` pointers at use sites
  - string-literal-backed character-array initializers for supported
    one-dimensional `char` / `signed char` / `unsigned char` arrays, both for
    top-level globals and local array objects
  - `unsigned long` local-variable allocation, store, and load
  - local aggregate allocation for supported `struct` and `union` objects
  - direct aggregate returns for supported `struct` and `union` functions
  - direct aggregate-return calls, including value-discarded uses such as the
    left-hand side of the comma operator
  - aggregate assignment expressions whose source and target already share the
    same lowered LLVM storage type, so expressions such as
    `box = make_box(7)` can feed later call arguments directly
  - explicit variadic call-site signatures such as
    `call i32 (ptr, ...) @printf(...)`, which are required for ABI-correct
    lowering of variadic arguments on Darwin AArch64
  - integer arithmetic instructions
    - including signedness-aware right shifts, where signed integers lower
      through `ashr` and unsigned integers lower through `lshr`
  - unary integer `+`, unary integer `-`, logical-not, and bitwise-not
    lowering over the current promoted integer result type, including
    `long long`-width `~` expressions
  - prefix/postfix increment and decrement lowering through
    load / update / store, including pointer increments lowered through GEP
  - integer compound-assignment lowering through load / binary-op / store for
    `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `^=`, and `|=`
  - bit-field member loads lowered through shared aggregate layout metadata,
    storage-unit extraction, masking, and signed/unsigned reconstruction
  - bit-field member assignments lowered through read-modify-write over the
    owning storage unit, including truncation to field width
  - `_Float16`, `float`, `double`, and `long double` arithmetic instructions
  - integer comparisons lowered as `icmp` + `zext`
    - including mixed signed/unsigned operands whose LLVM storage width is the
      same, where IR coercion must still retain the target semantic signedness
      so the backend selects `icmp ult/ule/ugt/uge` instead of signed compares
    - including typedef-backed 64-bit integers such as `int64_t` compared with
      `unsigned long` operands like `0UL`, where usual arithmetic conversions
      still require an unsigned LLVM compare even though both operands stay
      in `i64`
    - including narrow `unsigned` bit-field operands wrapped in value-
      preserving expressions such as `(side_effect, bits.value)`, where C
      integer promotions still require the compare to use signed `int`
      semantics when the field width fits in `int`
  - pointer comparisons against null-pointer constants lowered through LLVM
    `null` operands instead of integer `0`
  - short-circuit lowering for `&&` and `||` through dedicated rhs/true/end
    blocks
  - scalar cast lowering for supported integer, pointer, and floating
    combinations, including:
    - integer-width/sign coercions through `trunc`, `sext`, and `zext`
    - integer-to-pointer and pointer-to-integer casts through `inttoptr` and
      `ptrtoint`
    - integer-to-floating casts through `sitofp` / `uitofp`
    - floating-to-integer casts through `fptosi` / `fptoui`
    - floating widening and narrowing through `fpext` / `fptrunc`
  - integer ternary lowering for `?:` through dedicated true/false/end blocks
  - `double`, `_Float16`, and `long double` parameter passing, local-variable
    allocation, loads/stores, and direct returns in the current supported
    scalar subset
  - function-level `__always_inline__` lowered from semantic function
    attributes to LLVM `alwaysinline`
  - qualifier-stripping type lowering for pointer parameters such as
    `const char *`, `volatile int * volatile`, and
    `const char * __restrict`, which are emitted as ordinary LLVM `ptr`
  - pointer-side nullability annotations such as `_Nullable`, `_Nonnull`, and
    `_Null_unspecified`, which are preserved in semantic types but erased to
    ordinary LLVM `ptr` during lowering
- function-pointer parameters that carry pointer-side nullability and
  Darwin-style annotation spellings such as `_LIBC_COUNT(__n)` and
  `_LIBC_UNSAFE_INDEXABLE`, which are lowered through the same erased LLVM
  `ptr` signature path
  - qualifier-stripping lowering for GNU-const global declarations such as
    `extern __const int sys_nerr;` and
    `extern __const char *__const sys_errlist[];`, which continue to lower
    through the existing global/extern object model
  - qualifier-stripping lowering for `volatile` object declarations and
    pointer-qualified parameters, so `static volatile struct S0 g;` and
    `int first(volatile int * volatile p)` lower through the same storage and
    pointer IR paths as their unqualified equivalents
  - top-level `extern` global declarations emitted as LLVM
    `@name = external global <type>`
  - top-level global definitions emitted as LLVM
    `@name = global <type> <initializer>`
  - enum object lowering through the ordinary integer storage path, currently
    mapping enum storage to LLVM `i32` for globals, locals, parameters, and
    returns
  - struct global initializers that pack bit-field members into their shared
    storage elements before emitting one LLVM aggregate initializer
  - union global initializers that pack the first initialized field into one
    shared union storage element before emitting the LLVM aggregate
    initializer, including array elements of union type
  - local union initializer-list lowering that zero-fills the whole union
    storage before storing the first initialized field through the ordinary
    member-address path
  - top-level `static` global declarations and definitions emitted as LLVM
    `internal global`
  - top-level `static` function declarations and definitions emitted with LLVM
    `internal` linkage
  - bootstrap typedef-backed globals and parameters such as `size_t` and
    `uint32_t` lowered through their aliased builtin storage type, currently
    mapping `size_t` to LLVM `i64` and `uint32_t` to LLVM `i32`
  - GNU asm-labeled function prototypes emitted as external declarations before
    use sites
  - call-site declaration fallback for external function prototypes without
    bodies, so declarations such as `int puts(const char *);` are emitted
    before use even when only a prototype is present
  - grouped ordinary function declarators such as
    `static int (safe_add)(int x, int y)` lowered as ordinary LLVM function
    definitions and call targets after preprocessing expands wrapper macros
  - function-designator decay for supported value contexts such as local
    function-pointer initializers and indirect function-pointer calls
  - on-demand external-global references for `extern` declarations that are
    only visible inside one function scope
  - dialect-owned GNU function-attribute lowering through
    `GnuFunctionAttributeLoweringHandler`, currently mapping semantic
    `AlwaysInline` to backend-independent `IRFunctionAttribute::AlwaysInline`
  - label creation, unconditional branches, and conditional branches
  - direct scalar `&&`, `||`, and `?:` value merges through staged `phi`
    nodes when the result is scalar-valued, instead of always round-tripping
    through temporary stack slots
  - floating comparison lowering through `fcmp olt/ole/ogt/oge/oeq/une` across
    the currently supported floating family
  - floating truthiness lowering for `if (x)` / `while (x)` / `x ? a : b`
    through `fcmp une <ty> <value>, 0.0` followed by `br i1`
  - pointer truthiness lowering for `if (p)` / `while (p)` through
    `icmp ne ptr <value>, null`
  - implicit `ret void` insertion for supported `void` functions whose body
    falls off the end without an explicit `return;`
  - default argument promotions for variadic call operands, currently
    promoting `float -> double`, small integer types to `int`, and narrow
    bit-field-valued expressions to the same promoted integer type they would
    receive in ordinary expression semantics before emission
  - loop back edges and loop-exit branches for `while`, `for`, `do-while`,
    `break`, and `continue`
  - `switch` compare chains over integer `case` labels with `default`
    fallthrough and `switch.end` exits for `break`
  - wrapped `switch` bodies whose `case` / `default` entries are nested under
    block wrappers or followed by trailing statements that still belong to the
    active case entry
  - direct function calls with supported scalar parameters and supported
    scalar/aggregate return types
  - aggregate member-address lowering for `.` and `->`
  - pointer-address lowering for `&x`, `*p`, and `p[i]`
  - pointer arithmetic lowering for `pointer + integer`,
    `pointer - integer`, and `pointer - pointer`
  - internal `ptrdiff_t`-width pointer-difference lowering, currently emitted
    as LLVM `i64`
  - integer-width/sign coercion at `return`, assignment, local-initializer,
    and direct call-argument sites when semantic analysis allows a narrower or
    wider supported integer result than the target type
  - direct lowering of float literals as backend-owned floating constants so
    cast and arithmetic chains over `float`, `_Float16`, and `long double`
    can flow through one shared path
  - suffix-aware decimal and hexadecimal floating-literal lowering so `0.5f`
    and `0x1.0p-2f` stay `float`, unsuffixed `0.5` and `0x1.8p+1` stay
    `double`, and `0.5L` / `0x1.0p+0L` lower through a `double -> fp128`
    extension path for the current `long double` model
  - top-level `declare` emission for builtin runtime-style calls that do not
    have one user-defined body in the current translation unit
- `--dump-ir` writes `build/intermediate_results/*.ll`
- `--dump-core-ir` writes `build/intermediate_results/*.core-ir.txt`
- `-S --backend=aarch64-native --target=aarch64-unknown-linux-gnu` now emits a
  native `.s` file directly from optimized `CoreIrModule` without round-tripping
  through LLVM IR text
- the native AArch64 backend now lowers through backend-local machine blocks
  with virtual registers, CFG-aware liveness, interference-driven allocation,
  and spill-backed rewrite before final asm printing
- the native AArch64 backend now also models call instructions as clobber points,
  saves/restores actually used callee-saved scratch/allocation registers in the
  function frame, and lowers integer cast legalization through explicit
  sign-/zero-extension instructions instead of generic `mov` copies
- spill rewrite currently uses a fixed backend-local scratch partition of
  `x24-x27` for integer value rewrite, `x28` for spill-slot address
  materialization, and `v28-v31` for floating / `q` rewrite, so large-offset
  and helper-heavy floating paths share the same frame-address helpers
- parameters, value-producing instructions, and address-producing instructions
  now also prefer canonical machine virtual registers over default frame slots,
  so more native-backend live ranges survive into regalloc/spill instead of
  being forced through immediate store/reload pairs
- the native AArch64 backend now seeds canonical machine virtual registers for
  parameters and value/address-producing Core IR nodes, so many uses flow
  through machine vregs directly instead of defaulting to per-value frame slots
- selected `_Float16`, `double`, and `long double` native-backend asm tests now
  also have optional AArch64 object/link/qemu smoke via `tests/test_helpers.sh`
  and a target-only runtime support file, so host `long double` stubs are no
  longer the only evidence for helper-lowered floating paths

## Notes

- The active design goal is “LLVM IR first, but not LLVM-only forever”.
- `BuildCoreIrPass` and later backend stages depend on semantic output and
  therefore belong after
  [SemanticPass](/Users/caojunze424/code/SysyCC/src/frontend/semantic/semantic_pass.hpp).
- The staged `CoreIrAArch64TargetBackend` placeholder still exists for the old
  lower-to-IR interface, but the active native asm implementation is the
  separate `AArch64AsmGenPass` pipeline that consumes `CoreIrModule`
  directly.
- The current lowering is intentionally conservative: unsupported functions are
  skipped instead of emitting partial or malformed IR.
  - Remaining native-backend gaps are now narrower and mostly sit around
    global floating-point initializers, richer variadic runtime support, and
    more complete cross-toolchain end-to-end verification.
- Integer coercion is still limited to the currently modeled builtin integer
  family; it is not yet a complete ISO C99 implicit-conversion
  implementation.
