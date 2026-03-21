# IR Module

## Scope

The IR module prepares the backend stage after semantic analysis. Its current
goal is to expose a modular IR-generation skeleton that can target LLVM IR now
without hard-wiring the pass to LLVM-specific interfaces.

## Directory Layout

```text
src/backend/ir/
├── ir.hpp
├── ir_kind.hpp
├── ir_result.hpp
├── ir_backend.hpp
├── ir_backend_factory.hpp
├── ir_backend_factory.cpp
├── gnu_function_attribute_lowering_handler.hpp
├── gnu_function_attribute_lowering_handler.cpp
├── ir_pass.hpp
├── ir_pass.cpp
├── ir_builder.hpp
├── ir_builder.cpp
├── llvm/
│   ├── llvm_ir_backend.hpp
│   └── llvm_ir_backend.cpp
└── detail/
    ├── ir_context.hpp
    ├── ir_context.cpp
    ├── symbol_value_map.hpp
    └── symbol_value_map.cpp
```

## Responsibilities

- expose `IRGenPass` as the public backend pass entry
- keep IR-generation orchestration separate from one concrete IR backend
- store IR results in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- prepare a backend interface that currently maps to LLVM IR but can be
  replaced later

## Current Design

The current IR module is intentionally a skeleton:

- `IRGenPass` is connected to the main pipeline after `SemanticPass`
- `IRBuilder` coordinates IR generation through an abstract `IRBackend`
- `IRBackend` defines backend-independent emission hooks
- `LlvmIrBackend` is the first concrete backend implementation
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
- `IRResult` stores:
  - `IrKind`
  - IR text output
- `detail/IRContext` and `detail/SymbolValueMap` provide transient state and
  value bookkeeping slots for future lowering work

## Current Status

At this stage the IR module provides the structural foundation plus one growing
LLVM IR lowering path:

- `CompilerContext` can store one `IRResult`
- `CompilerContext` can store one IR dump file path
- `CompilerContext` now tracks the `--dump-ir` switch
- `IRGenPass` runs after semantic analysis
- `IRBuilder` currently lowers a focused AST subset:
  - `TranslationUnit`
  - `FunctionDecl` with supported scalar or void return type
  - `ParamDecl`
  - `BlockStmt`
  - `DeclStmt` containing supported scalar or aggregate `VarDecl`
  - `ExprStmt`
  - `IntegerLiteralExpr`
  - `IdentifierExpr`
  - `BinaryExpr` for `+ - * / % < <= > >= == != && ||`
  - `CastExpr` for scalar `int`/`double` conversions
  - `ConditionalExpr` for integer ternary `?:`
  - `AssignExpr` with identifier and member-expression targets
  - `CallExpr` whose callee is an identifier
  - `UnaryExpr` for `&` and `*`
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
- `LlvmIrBackend` now emits minimal textual LLVM IR for:
  - `int main() { return 0; }`
  - `void main() { return; }`
  - integer local-variable allocation, store, and load
  - local aggregate allocation for supported `struct` and `union` objects
  - integer arithmetic instructions
  - integer comparisons lowered as `icmp` + `zext`
  - short-circuit lowering for `&&` and `||` through dedicated rhs/true/end
    blocks
  - scalar cast lowering for `int -> double` (`sitofp`) and
    `double -> int` (`fptosi`)
  - integer ternary lowering for `?:` through dedicated true/false/end blocks
  - `double` parameter passing, local-variable allocation, loads/stores, and
    direct returns
  - function-level `__always_inline__` lowered from semantic function
    attributes to LLVM `alwaysinline`
  - qualifier-stripping type lowering for pointer parameters such as
    `const char *`, which are emitted as ordinary LLVM `ptr`
  - top-level `extern` global declarations emitted as LLVM
    `@name = external global <type>`
  - top-level global definitions emitted as LLVM
    `@name = global <type> <initializer>`
  - on-demand external-global references for `extern` declarations that are
    only visible inside one function scope
  - dialect-owned GNU function-attribute lowering through
    `GnuFunctionAttributeLoweringHandler`, currently mapping semantic
    `AlwaysInline` to backend-independent `IRFunctionAttribute::AlwaysInline`
  - label creation, unconditional branches, and conditional branches
  - loop back edges and loop-exit branches for `while`, `for`, `do-while`,
    `break`, and `continue`
  - `switch` compare chains over integer `case` labels with `default`
    fallthrough and `switch.end` exits for `break`
  - direct function calls with integer parameters
  - aggregate member-address lowering for `.` and `->`
  - pointer-address lowering for `&x`, `*p`, and `p[i]`
  - pointer arithmetic lowering for `pointer + integer`,
    `pointer - integer`, and `pointer - pointer`
  - internal `ptrdiff_t`-width pointer-difference lowering, currently emitted
    as LLVM `i64`
  - integer-width/sign coercion at `return`, assignment, local-initializer,
    and direct call-argument sites when semantic analysis allows a narrower or
    wider supported integer result than the target type
  - top-level `declare` emission for builtin runtime-style calls that do not
    have one user-defined body in the current translation unit
- `--dump-ir` writes `build/intermediate_results/*.ll`

## Notes

- The active design goal is “LLVM IR first, but not LLVM-only forever”.
- `IRGenPass` depends on semantic output and therefore belongs after
  [SemanticPass](/Users/caojunze424/code/SysyCC/src/frontend/semantic/semantic_pass.hpp).
- The current lowering is intentionally conservative: unsupported functions are
  skipped instead of emitting partial or malformed IR.
- Arrays, richer scalar-type lowering, and richer runtime-library IR emission
  are still pending.
- Integer coercion is still limited to the currently modeled builtin integer
  family; it is not yet a complete ISO C99 implicit-conversion
  implementation.
