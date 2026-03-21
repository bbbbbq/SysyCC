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
- parse float / char / string literals, pointer declarators, `for`,
  `do ... while`, `switch/case/default`, bitwise operators, shifts, `++/--`,
  and both `.` / `->` member access

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

## Notes

- The generated parser header is [parser.tab.h](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.tab.h).
- The generated parser implementation is [parser_generated.cpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_generated.cpp).
