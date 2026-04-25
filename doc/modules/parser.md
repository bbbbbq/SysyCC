# Parser Module

## Scope

The parser module contains the syntax-analysis pass, the bison grammar
template, generated parser files, and the runtime used to build parse trees.

## Directory Layout

```text
src/frontend/parser/
├── parser.hpp
├── parser.cpp
├── parser_runtime.hpp
├── parser_runtime.cpp
└── parser.y
```

Generated parser outputs live under the active build tree:

```text
<build-dir>/generated/frontend/parser/
├── parser_generated.cpp
└── parser.tab.h
```

## Responsibilities

- open the preprocessed source file or original input file
- invoke `yyparse()`
- collect parse trees
- enable scanner-side terminal-node construction only while parser mode is active
- let parser-mode scanner sessions inherit preprocess logical file/line
  remapping so parse-tree terminal spans reflect accepted `#line` directives
- consume the same shared downstream source-mapping view as the lexer pass
- dump parse results into `build/intermediate_results/*.parse.txt`
- accept the current SysY22 core grammar plus a growing subset of C-style
  extensions
- parse float / double / char / string literals, pointer declarators, `for`,
  `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`,
  compound-assignment operators such as `+=`, `>>=`, and `&=`, ordinary
  expression-level ternary `?:`, C-style casts `(type)expr`, both `.` / `->`
  member access, and top-level `extern` / `inline` function prototype
  declarations
- parse decimal and hexadecimal floating literals with standard C suffix
  spellings such as `0.5f`, `1.0F`, `0.5L`, unsuffixed `0.5`,
  `0x1.0p-2f`, and `0x1.0p+0L`
- accept pointer-target cast forms such as `(int *)value` and
  `(const char *)buffer` through a dedicated `cast_target_type` grammar path
- parse `extern` variable declarations such as `extern int signgam;`
- parse declaration-side `_Alignas(...)` compatibility spellings on object and
  field declarations, including `stdalign.h`-driven `alignas(...)` expansions
- parse declaration-side builtin forms such as `_Float16`, `__signed char`,
  `short`, `unsigned char`, and `unsigned short`
- parse `union` declarations and inline anonymous `union { ... } name;`
  declarations inside blocks
- parse `unsigned`, `unsigned int`, `unsigned long`, and `unsigned long long`
  declaration specifiers
- parse `unsigned long` / `unsigned long int` declaration specifiers as
  builtin integer forms
- parse `long` / `long int` declaration specifiers as builtin integer forms
- parse `long long` / `long long int` declaration specifiers as builtin integer
  forms
- recognize GNU-style `__attribute__((...))` specifier sequences attached to
  function declarations and definitions
- accept leading GNU attribute specifier sequences ahead of function
  declarations such as `__attribute__((__always_inline__)) int foo(void);`
- recognize GNU-style `__asm("_symbol")` declaration suffixes attached to
  function prototypes, including adjacent string-literal concatenation used by
  Darwin alias macros
- recognize GNU-style `__asm("_symbol")` declaration suffixes attached to
  variable declarations from system headers such as `extern long timezone
  __asm("_timezone");`
- accept declaration-only function prototypes such as `extern int foo(void);`
  and `inline int foo(void);`, plus unnamed prototype parameters such as
  `extern int bar(float);`
- accept C99 variadic function prototypes and definitions such as
  `int printf(const char *fmt, ...);` and `int keep_first(int x, ...) { ... }`
- accept unnamed pointer prototype parameters such as
  `extern float modff(float, float *);`
- accept unnamed array prototype parameters such as
  `double erand48(unsigned short[3]);` by decaying them through the ordinary
  parameter-type path
- accept unnamed typedef-name prototype parameters such as
  `int strncasecmp(const char *, const char *, size_t);`
- accept pointer-return function prototypes such as
  `void *memchr(const void *, int, size_t);`
- accept grouped function declarators that return function pointers such as
  `void (*signal(int, void (*)(int)))(int);`
- accept suffix GNU attributes on function prototypes such as
  `int foo(void) __attribute__((__always_inline__));`
- accept suffix GNU attributes on struct and union declarations such as
  `struct S { char bytes[4]; } __attribute__((aligned(4)));`
- accept simple `const`-qualified prototype parameters such as
  `extern float nanf(const char *);`, preserving the qualifier into AST
  lowering as a pointee-side qualified type
- accept `volatile`-qualified declaration spellings on both the pointee side
  and pointer side, including forms such as `volatile int * volatile p`,
  `static volatile struct S0 g;`, `const volatile int x;`, and GNU aliases
  `__volatile` / `__volatile__`
- accept pointer-side qualifier spellings such as
  `const char * restrict name`, `const char * __restrict name`, and
  `const char * __restrict__ name`
- accept pointer-side nullability spellings and Darwin-style pointer annotation
  spellings inside declarators, including function-pointer parameters such as
  `int (* _Nullable read_fn)(void *, char *_LIBC_COUNT(__n), int __n)` and
  pointer-return declarations such as `char *_LIBC_CSTR ctermid_r(char *);`
  and bare pointer-side annotation qualifiers such as
  `const char *_LIBC_UNSAFE_INDEXABLE`
- accept GNU `const` spellings such as `__const` and `__const__` through the
  same `CONST` token path used by ordinary `const`
- accept top-level `extern const` variable declarations such as
  `extern __const int sys_nerr;` and
  `extern __const char *__const sys_errlist[];`
- accept top-level `static` object declarations such as `static int g = 1;`
- accept top-level `static` function definitions such as
  `static void helper(void) { ... }`
- accept `long double` as a builtin declaration type, including prototype forms
  such as `extern long double f(long double);`
- accept `long int` as a builtin declaration type, including prototype forms
  such as `extern float scalblnf(float, long int);`
- accept `long long int` as a builtin declaration type, including prototype
  forms such as `extern long long int llrint(double);`
- accept `_Float16` as a builtin declaration type, including prototype forms
  such as `extern _Float16 nextafterh(_Float16, _Float16);`
- accept builtin declaration forms such as `signed char`, `short`,
  `unsigned char`, and `unsigned short` inside declarations and typedefs
- accept grouped function-pointer declarators such as
  `void (*routine)(void *)` inside fields, typedefs, and related declaration
  positions used by system headers
- accept grouped ordinary function declarators such as
  `static int (safe_add)(int x, int y)` so macro-expanded wrapper spellings
  like `FUNC_NAME(name)` from CSmith-style runtimes remain valid after
  preprocessing
- accept named `struct` and `union` forward declarations such as
  `struct Node;` and `union Value;`
- accept `const`-qualified object declarations that use non-builtin aggregate
  types such as `const struct Pair snapshot = value;` through the ordinary
  variable-declaration path instead of treating them as SysY-style `ConstDecl`
- seed parser-runtime bootstrap typedef names such as `size_t`, `ptrdiff_t`,
  `va_list`, `__builtin_va_list`, `wchar_t`, and common fixed-width or
  pointer-width aliases such as `uint32_t`, `int32_t`, `uint64_t`, `intptr_t`,
  and `uintptr_t` as `TYPE_NAME` tokens before user parsing so system-header
  typedef chains can be recognized without a prior in-translation-unit
  bootstrap header
- seed compiler builtin type-macro spellings such as `__PTRDIFF_TYPE__`,
  `__SIZE_TYPE__`, `__INTMAX_TYPE__`, `__UINTMAX_TYPE__`, `__WCHAR_TYPE__`,
  and `__WINT_TYPE__`, plus compatibility builtin names such as `__uint128_t`,
  as `TYPE_NAME` tokens for transitive system-header declarations
- capture parser syntax failures as structured parser-runtime error state so
  CLI diagnostics can report the current token text and logical source span

## Key Files

- [parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.hpp)
- [parser.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.cpp)
- [parser_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.hpp)
- [parser_runtime.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.cpp)
- [parser.y](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.y)

## Output Artifacts

- parse dump text files in `build/intermediate_results`
- parse tree runtime nodes stored in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- parser syntax diagnostics emitted through the shared
  [DiagnosticEngine](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)

## Notes

- The generated parser header now lives under the active build tree, for
  example `build/generated/frontend/parser/parser.tab.h`.
- The generated parser implementation now lives under the active build tree,
  for example `build/generated/frontend/parser/parser_generated.cpp`.
