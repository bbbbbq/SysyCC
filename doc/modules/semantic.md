# Semantic Module

## Scope

The semantic module provides the first semantic-analysis skeleton after AST
lowering. It consumes the compiler-facing AST, installs builtin runtime
symbols, manages lexical scopes, and records semantic results in a separate
semantic model.

## Directory Layout

```text
src/frontend/semantic/
├── model/
│   ├── semantic_diagnostic.hpp
│   ├── semantic_diagnostic.cpp
│   ├── semantic_function_attribute.hpp
│   ├── semantic_model.hpp
│   ├── semantic_model.cpp
│   ├── semantic_symbol.hpp
│   ├── semantic_symbol.cpp
│   ├── semantic_type.hpp
│   └── semantic_type.cpp
├── analysis/
│   ├── semantic_analyzer.hpp
│   ├── semantic_analyzer.cpp
│   ├── decl_analyzer.hpp
│   ├── decl_analyzer.cpp
│   ├── stmt_analyzer.hpp
│   ├── stmt_analyzer.cpp
│   ├── expr_analyzer.hpp
│   └── expr_analyzer.cpp
├── support/
│   ├── builtin_symbols.hpp
│   ├── builtin_symbols.cpp
│   ├── scope_stack.hpp
│   ├── scope_stack.cpp
│   ├── semantic_context.hpp
│   └── semantic_context.cpp
├── type_system/
│   ├── constant_evaluator.hpp
│   ├── constant_evaluator.cpp
│   ├── conversion_checker.hpp
│   ├── conversion_checker.cpp
│   ├── integer_conversion_service.hpp
│   ├── integer_conversion_service.cpp
│   ├── type_resolver.hpp
│   └── type_resolver.cpp
├── semantic.hpp
├── semantic_pass.hpp
├── semantic_pass.cpp
```

## Responsibilities

- expose `SemanticPass` as the only public pass entry for semantic analysis
- keep semantic data separate from AST node classes through `model/`
- install builtin runtime-library functions before user AST traversal
- manage nested lexical scopes through `support/`
- bind AST nodes to semantic symbols and semantic types
- separate orchestration, support infrastructure, and type-system helpers so
  the directory tree itself reflects semantic module dependencies

## Current Status

The current implementation has a first batch of real semantic rules:

- `SemanticPass` is connected to the main pipeline
- `analysis/SemanticAnalyzer` now acts as an orchestrator
- declaration rules live in `analysis/DeclAnalyzer`
- statement rules live in `analysis/StmtAnalyzer`
- expression and operator rules live in `analysis/ExprAnalyzer`
- type construction lives in `type_system/TypeResolver`
- type-compatibility and operand-category checks live in
  `type_system/ConversionChecker`
- integer width/sign conversion planning lives in
  `type_system/IntegerConversionService`
- foldable integer constant-expression queries live in
  `type_system/ConstantEvaluator`
- transient analysis state and builtin installation live in `support/`
- semantic output data structures live in `model/`
- builtin functions such as `getint`, `putint`, `putfloat`, `putch`,
  `starttime`, and `stoptime` are registered into the initial scope alongside
  bootstrap typedef aliases
- builtin scalar type names now include `double` alongside `int`, `float`,
  `char`, and `void`
- declaration-side builtin scalar types now also include `long double`
- declaration-side builtin scalar types now also include `_Float16`
- declaration-side builtin integer forms now also include `long int`
- declaration-side builtin integer forms now also include `long long int`
- declaration-side builtin integer forms now also include `unsigned int` and
  `unsigned long long`
- declaration-side builtin integer forms now also include `unsigned long`
- declaration-side builtin integer forms now also include `signed char`,
  `short`, `unsigned char`, and `unsigned short`
- bootstrap typedef names such as `size_t`, `ptrdiff_t`, `va_list`,
  `__builtin_va_list`, `wchar_t`, and common fixed-width or pointer-width
  aliases such as `uint32_t`, `int32_t`, `uint64_t`, `intptr_t`, and
  `uintptr_t` are preinstalled as `TypedefName` symbols so system-header
  typedef chains resolve through the same semantic path as user-authored
  aliases
- function-pointer declaration types now resolve through
  `PointerSemanticType(FunctionSemanticType(...))` when parser lowering sees
  grouped declarators such as `void (*routine)(void *)`
- grouped ordinary function declarators such as
  `static int (safe_add)(int x, int y)` are accepted through the same
  declaration path, so macro-expanded wrapper names still bind one ordinary
  function symbol
- top-level pointer-return prototypes such as `void *memchr(...)` now resolve
  through the same return-type path as other supported function declarations
- GNU asm-labeled function prototypes such as
  `char *strerror(int) __asm("_strerror");` are accepted semantically and keep
  their external symbol spelling available for downstream IR lowering
- variadic function declarations and definitions now preserve their `...`
  marker in `FunctionSemanticType`, so semantic call checking can enforce the
  fixed parameter prefix while allowing extra arguments after `...`
- string literals now bind as `ArraySemanticType(char, {N})` instead of
  stage-local `char *`, so declaration analysis and later IR lowering see the
  C object type before decay happens in value contexts
- function designators now decay to `PointerSemanticType(FunctionSemanticType)`
  in assignment and call-argument compatibility checks, so declarations such
  as `int (*fp)(int) = inc;` and fixed-parameter calls such as `apply(inc, 4)`
  are accepted through the same semantic conversion path as array decay
- call analysis now accepts both direct function designators and
  pointer-to-function callees, so `fn(value)` is no longer rejected when `fn`
  is a function pointer parameter or local variable
- enum-typed variables, parameters, and returns now participate in the same
  integer-like assignability path as other modeled arithmetic types, so
  enumerators can initialize enum objects and flow through enum-returning
  functions without ad hoc special cases
- integer constant-expression flow for `ConditionalExpr` is now covered
  through semantic analysis, so array dimensions, `case` labels, and
  enumerator values can all reuse the selected branch constant once the
  condition folds
- declaration analysis accepts `extern` variable declarations
- file-scope variable analysis now preserves one `VariableSemanticInfo` record
  per bound variable symbol, tracking:
  - whether the variable uses global storage
  - whether external linkage was seen
  - whether internal linkage was seen
  - whether a tentative definition was seen
  - whether an initialized definition was seen
- compatible file-scope redeclaration patterns such as
  `extern int g; int g; extern int g;` are now accepted and rebound to one
  shared semantic symbol
- file-scope `static` variables now preserve internal linkage through
  `VariableSemanticInfo`
- top-level `static` function declarations and definitions are now accepted so
  downstream IR lowering can emit internal-linkage functions
- file-scope and static-storage objects now also run through one shared
  static-initializer classifier in `ConstantEvaluator`, so semantic analysis
  can reject runtime-valued initializers earlier instead of deferring those
  failures to Core IR construction
- folded scalar constant expressions such as `static double g = 1.0 + 2.0`
  and address-shaped static initializers such as `static int *p = arr + 1`
  now reuse that same semantic-side static-initializer path
- declaration analysis now rejects non-list variable initializers whose
  expression type is not assignable to the declared type
- declaration analysis now also accepts one-dimensional character arrays
  initialized from exact-fit string literals through the same type-compatibly
  checked initializer path as other supported scalar/object initializers
- top-level declaration-only function prototypes are accepted without requiring
  a body
- declaration-only prototypes now include `inline` forms alongside `extern`
- preserved GNU attribute lists on function declarations are now analyzed
  through a dedicated attribute analyzer
- function-level `__always_inline__` is accepted as a supported semantic
  function attribute
- other recognized GNU function attributes currently produce semantic errors
- unnamed prototype parameters contribute to function types but are not entered
  into the local scope as named symbols
- unnamed pointer prototype parameters are analyzed as ordinary pointer-typed
  parameters and likewise stay out of the named local symbol table
- unnamed typedef-name prototype parameters follow the same unnamed-parameter
  semantic path while resolving their typedef-backed type normally
- simple parameter-side `const` qualifiers now resolve into
  `QualifiedSemanticType` under pointer pointees, while pointer-side
  qualifiers such as `restrict`, `__restrict`, and `__restrict__` resolve into
  `QualifiedSemanticType` wrapped around the pointer itself, so
  `const char * __restrict`, `const char *`, and `char *` remain
  distinguishable semantic pointer types
- declaration-side and parameter-side `volatile` qualifiers now resolve
  through the same `QualifiedSemanticType` model as `const`, while
  pointer-side `volatile` resolves into `QualifiedSemanticType` wrapped around
  the pointer itself, so `volatile int * volatile`, `volatile int *`, and
  `int *` remain distinguishable semantic pointer types even though IR later
  erases the qualifier
- pointer-side nullability annotations such as `_Nullable`, `_Nonnull`, and
  `_Null_unspecified` now resolve into `PointerSemanticType` metadata on the
  pointer itself, so the semantic model can preserve the annotation while
  still accepting the underlying pointer type in ordinary compatibility checks
- declarator-side compatibility annotations such as `_LIBC_COUNT(__n)` and
  `_LIBC_CSTR` are accepted around pointer declarators and function-pointer
  parameters, and bare pointer-side compatibility annotations such as
  `_LIBC_UNSAFE_INDEXABLE` are accepted through the same path, while
  continuing to resolve to the same underlying pointer semantic type
- GNU spelling aliases such as `__const` and `__const__` now resolve through
  the same qualifier/type path as ordinary `const`, including file-scope
  `extern const` variables and pointer-side `__const` qualifiers
- GNU spelling aliases such as `__volatile` and `__volatile__` now resolve
  through the same qualifier/type path as ordinary `volatile`
- extension builtin scalar semantics such as `_Float16` are now owned through
  one shared builtin-type semantic handler registry exported by the dialect
  manager, so `ConversionChecker` no longer treats `_Float16` as an unowned
  stage-local special case
- usual arithmetic conversion planning now explicitly covers the current
  supported floating family `(_Float16, float, double, long double)` alongside
  the modeled integer family
- usual arithmetic conversion selection for mixed signed/unsigned integers now
  also preserves the unsigned corresponding type when a higher-rank signed
  operand shares the same storage width as the unsigned operand, so typedef-
  backed 64-bit integers such as `int64_t` compare against `0UL` through the
  same rule as their builtin `long long int` spelling
- integer promotions for bit-field-valued expressions now keep track of the
  source field width through direct member access and value-preserving wrappers
  such as the comma operator, assignment expressions, and prefix/postfix
  `++/--`, so narrow `unsigned` bit-fields that fit in `int` compare through
  signed `int` semantics instead of being forced down the `unsigned int` path
- unary bitwise-not now follows C integer promotions, so `~value` on a narrow
  integer still yields `int`, while `~9223372036854775807LL` preserves
  `long long int` through semantic typing
- arithmetic casts such as `_Float16 <-> int`, `_Float16 <-> long double`, and
  `long double <-> int` are now accepted through the shared conversion checker
  for the current supported scalar subset
- decimal and hexadecimal floating literals now bind according to their C
  suffix spelling: unsuffixed literals use `double`, `f/F` suffixed literals
  use `float`, and `l/L` suffixed literals use `long double`
- pointer-target casts such as `(int *)value` and `(const char *)ptr` now
  lower through the same explicit-cast semantic path used by other supported
  scalar and pointer casts
- qualification-preserving pointer conversions such as
  `char * -> const char *` and `char * -> char * __restrict` are accepted for
  calls and assignments, while qualification-dropping conversions such as
  `const char * -> char *` are rejected
- incompatible pointer assignments such as `unsigned int *dst = ...; dst = src;`
  now produce semantic warnings instead of hard failures, matching the current
  C-compatibility goal while still preserving hard errors for non-pointer
  assignment mismatches
- member expressions now merge top-level qualifiers from the containing object
  onto the selected field type, so accesses such as `volatile struct S g;`
  `&g.field` preserve the field's own qualifiers and the owning object's
  `volatile` qualification through the semantic model
- incompatible pointer returns such as `int *f(void) { unsigned int *p = ...;`
  `return p; }` now produce semantic warnings instead of hard failures,
  matching the current C-compatibility goal while still preserving hard errors
  for non-pointer return mismatches
- declaration analysis records symbols for functions, parameters, variables,
  constants, typedefs, structs, unions, enums, and enumerators
- semantic type construction now includes `UnionSemanticType` for lowered
  union declarations and inline anonymous union type nodes
- expression analysis records basic type bindings for identifiers, literals,
  unary/prefix/postfix expressions, cast expressions, conditional expressions,
  calls, indexes, and member expressions
- semantic analysis now reports and rejects:
  - undefined identifiers
  - same-scope redefinitions
  - function-call arity mismatches
  - function-call argument type mismatches
  - qualification-dropping pointer conversions such as
    `const char * -> char *`
  - calls to non-function objects
  - assignment type mismatches
  - assignments to non-assignable targets
  - non-void / void `return` mismatches
  - invalid arithmetic, bitwise, shift, logical, relational, and equality
    binary operands
  - non-scalar condition expressions
  - non-scalar ternary conditions
  - ternary branches with incompatible result types
  - non-pointer index bases
  - non-integer array subscripts
  - non-constant array dimensions
  - integer-zero null pointer constants assigned or passed to pointer targets
  - array-to-pointer decay for pointer-compatible call and assignment checks
  - pointer arithmetic for `pointer +/- integer` and `pointer - pointer`
  - invalid operands for unary `&` and `*`
  - invalid operands for unary `+`, unary `-`, `!`, and `~`
  - unsupported explicit casts between incompatible source/target kinds
  - invalid operands for compound assignment operators such as `+=`, `>>=`,
    and `&=`
  - invalid operands for prefix/postfix `++`
  - `break` outside loops or `switch`
  - `continue` outside loops
  - `case` / `default` labels outside `switch`
  - duplicate `case` labels within one `switch`
  - multiple `default` labels within one `switch`
  - non-constant `case` labels
  - non-constant integer `const` initializers
  - non-constant enumerator values
  - invalid `.` / `->` use on unsupported base types
  - access to missing struct or union members through `.` / `->`
  - non-void functions whose current body may exit without returning a value
- semantic pass success now depends on whether any `Error` diagnostics were
  produced during AST traversal
- integer constant-expression queries now fall back to recursive AST evaluation
  for character literals and unary/binary operator trees
- if the current AST is not complete yet, `SemanticPass` stores a semantic
  model but skips strict rule checking so incomplete lowering does not block
  ordinary frontend smoke tests

## Output

- semantic results are stored in
  [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
  as a `SemanticModel`
- `SemanticModel` stores:
  - success state
  - diagnostics
  - AST-node-to-type bindings
  - AST-node-to-symbol bindings
  - integer constant-expression values for foldable AST nodes
  - semantic function-attribute bindings for analyzed `FunctionDecl` nodes
  - per-variable storage/linkage summaries for global and external object
    emission
  - owned semantic types and symbols

## Notes

- This module is intentionally kept separate from AST so the AST classes remain
  structural and semantic results stay replaceable.
- The current analyzer is still intentionally small; it does not yet cover full
  implicit-conversion rules, floating-point constant folding, or broader
  control-flow diagnostics beyond the current duplicate-`case`/missing-return
  checks.
- `ExprAnalyzer` now receives both `.` and `->` member expressions from the
  parser/AST pipeline and applies shared struct-or-union member lookup rules to
  each operator.
- `ExprAnalyzer` now also models `pointer - pointer` with an internal
  `ptrdiff_t`-style semantic builtin instead of collapsing that result to plain
  `int`.
- `IntegerConversionService` now centralizes the currently supported integer
  width/sign conversion planning used later by IR coercion at `return`,
  assignment, local-initializer, and direct call-argument sites.
- `IntegerConversionService` also now owns the supported scalar subset's
  integer promotions and usual arithmetic conversion selection, so
  `ConversionChecker` can distinguish mixed integer/integer, float/integer, and
  extended builtin float results using one shared rule table.
- variadic call checking now validates and type-checks only the fixed
  parameter prefix, leaving extra operands to later default-argument-promotion
  lowering in IR generation.
- The recent refactor keeps `SemanticAnalyzer` thin so future semantic work can
  continue in specialized helpers instead of growing one class indefinitely.
