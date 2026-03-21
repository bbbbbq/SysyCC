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
├── parser.y
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
  ordinary expression-level ternary `?:`, C-style casts `(type)expr`, both `.`
  / `->` member access, and top-level `extern` / `inline` function prototype
  declarations
- parse `extern` variable declarations such as `extern int signgam;`
- parse declaration-side builtin forms such as `_Float16`, `__signed char`,
  `short`, `unsigned char`, and `unsigned short`
- parse `union` declarations and inline anonymous `union { ... } name;`
  declarations inside blocks
- parse `unsigned`, `unsigned int`, and `unsigned long long` declaration
  specifiers
- parse `long` / `long int` declaration specifiers as builtin integer forms
- parse `long long` / `long long int` declaration specifiers as builtin integer
  forms
- recognize GNU-style `__attribute__((...))` specifier sequences attached to
  function declarations and definitions
- accept declaration-only function prototypes such as `extern int foo(void);`
  and `inline int foo(void);`, plus unnamed prototype parameters such as
  `extern int bar(float);`
- accept unnamed pointer prototype parameters such as
  `extern float modff(float, float *);`
- accept simple `const`-qualified prototype parameters such as
  `extern float nanf(const char *);`, preserving the qualifier into AST
  lowering as a pointee-side qualified type
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
- capture parser syntax failures as structured parser-runtime error state so
  CLI diagnostics can report the current token text and logical source span

## Key Files

- [parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.hpp)
- [parser.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.cpp)
- [parser_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.hpp)
- [parser_runtime.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.cpp)
- [parser.y](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.y)
- [parser_generated.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_generated.cpp)
- [parser.tab.h](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.tab.h)

## Output Artifacts

- parse dump text files in `build/intermediate_results`
- parse tree runtime nodes stored in [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- parser syntax diagnostics emitted through the shared
  [DiagnosticEngine](/Users/caojunze424/code/SysyCC/src/common/diagnostic/diagnostic_engine.hpp)

## Notes

- The generated parser header is [parser.tab.h](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.tab.h).
- The generated parser implementation is [parser_generated.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_generated.cpp).
