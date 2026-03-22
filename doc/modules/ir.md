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
    ├── aggregate_layout.hpp
    ├── aggregate_layout.cpp
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

## Current Status

At this stage the IR module provides the structural foundation plus one growing
LLVM IR lowering path:

- `CompilerContext` can store one `IRResult`
- `CompilerContext` can store one IR dump file path
- `CompilerContext` now tracks the `--dump-ir` switch
- `IRGenPass` runs after semantic analysis
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
  - `CallExpr` whose callee is an identifier
  - `UnaryExpr` for `&` and `*`
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
  - `unsigned long` local-variable allocation, store, and load
  - local aggregate allocation for supported `struct` and `union` objects
  - direct aggregate returns for supported `struct` and `union` functions
  - direct aggregate-return calls, including value-discarded uses such as the
    left-hand side of the comma operator
  - explicit variadic call-site signatures such as
    `call i32 (ptr, ...) @printf(...)`, which are required for ABI-correct
    lowering of variadic arguments on Darwin AArch64
  - integer arithmetic instructions
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
  - struct global initializers that pack bit-field members into their shared
    storage elements before emitting one LLVM aggregate initializer
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
  - on-demand external-global references for `extern` declarations that are
    only visible inside one function scope
  - dialect-owned GNU function-attribute lowering through
    `GnuFunctionAttributeLoweringHandler`, currently mapping semantic
    `AlwaysInline` to backend-independent `IRFunctionAttribute::AlwaysInline`
  - label creation, unconditional branches, and conditional branches
  - floating comparison lowering through `fcmp olt/ole/ogt/oge/oeq/une` across
    the currently supported floating family
  - floating truthiness lowering for `if (x)` / `while (x)` / `x ? a : b`
    through `fcmp une <ty> <value>, 0.0` followed by `br i1`
  - pointer truthiness lowering for `if (p)` / `while (p)` through
    `icmp ne ptr <value>, null`
  - implicit `ret void` insertion for supported `void` functions whose body
    falls off the end without an explicit `return;`
  - default argument promotions for variadic call operands, currently
    promoting `float -> double` and small integer types to `int` before
    emission
  - loop back edges and loop-exit branches for `while`, `for`, `do-while`,
    `break`, and `continue`
  - `switch` compare chains over integer `case` labels with `default`
    fallthrough and `switch.end` exits for `break`
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

## Notes

- The active design goal is “LLVM IR first, but not LLVM-only forever”.
- `IRGenPass` depends on semantic output and therefore belongs after
  [SemanticPass](/Users/caojunze424/code/SysyCC/src/frontend/semantic/semantic_pass.hpp).
- The current lowering is intentionally conservative: unsupported functions are
  skipped instead of emitting partial or malformed IR.
  - Arrays, richer aggregate/object lowering, and richer runtime-library IR
  emission are still pending.
- Integer coercion is still limited to the currently modeled builtin integer
  family; it is not yet a complete ISO C99 implicit-conversion
  implementation.
