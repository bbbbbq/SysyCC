# Class Relationships

## Scope

This document describes the current class relationships in the SysyCC project.
It focuses on the active main path used by the executable.

## Main Relationship Graph

```mermaid
classDiagram
    class Cli {
        +Run(argc, argv)
        +set_compiler_option(option)
    }

    class Complier {
        -ComplierOption option_
        -CompilerContext context_
        -PassManager pass_manager_
        +Run()
    }

    class ComplierOption {
        -input_file_
        -output_file_
        -include_directories_
        -dump_tokens_
        -dump_parse_
        -dump_ast_
        -dump_ir_
    }

    class CompilerContext {
        -input_file_
        -preprocessed_file_path_
        -include_directories_
        -tokens_
        -parse_tree_root_
        -token_dump_file_path_
        -parse_dump_file_path_
    }

    class PassManager {
        -passes_
        +AddPass(pass)
        +Run(context)
    }

    class Pass {
        <<abstract>>
        +Kind()
        +Name()
        +Run(context)
    }

    class LexerPass {
    }

    class ParserPass {
    }

    class LexerState {
        -line_
        -column_
        -token_line_begin_
        -token_column_begin_
        -token_line_end_
        -token_column_end_
        -emit_parse_nodes_
        +reset()
        +update_position()
        +get_token_line_begin()
        +get_token_column_begin()
        +get_token_line_end()
        +get_token_column_end()
        +get_emit_parse_nodes()
        +set_emit_parse_nodes()
    }

    class PreprocessPass {
    }

    class PreprocessSession {
    }

    class PreprocessorRuntime {
    }

    class MacroDefinition {
    }

    class MacroTable {
    }

    class MacroExpander {
    }

    class ConditionalStack {
    }

    class DirectiveParser {
    }

    class IncludeResolver {
    }

    class FileLoader {
    }

    class Token {
        -TokenKind kind_
        -string text_
        -SourceSpan source_span_
        +get_kind()
        +get_text()
        +get_source_span()
        +get_category()
        +get_kind_name()
    }

    class SourceSpan {
        -line_begin_
        -col_begin_
        -line_end_
        -col_end_
    }

    class ParseTreeNode {
        +string label
        +children
    }

    Cli ..> ComplierOption : fills
    Complier *-- ComplierOption
    Complier *-- CompilerContext
    Complier *-- PassManager
    PassManager *-- Pass
    Pass <|-- PreprocessPass
    Pass <|-- LexerPass
    Pass <|-- ParserPass
    LexerPass ..> CompilerContext : writes tokens
    LexerPass *-- LexerState
    ParserPass *-- LexerState
    ParserPass ..> CompilerContext : writes parse tree
    PreprocessPass ..> CompilerContext : writes preprocessed file path
    PreprocessPass ..> PreprocessSession
    PreprocessSession *-- PreprocessorRuntime
    PreprocessorRuntime *-- MacroDefinition
    PreprocessSession *-- MacroTable
    PreprocessSession *-- MacroExpander
    PreprocessSession *-- ConditionalStack
    PreprocessSession *-- DirectiveParser
    PreprocessSession *-- IncludeResolver
    PreprocessSession *-- FileLoader
    CompilerContext *-- Token
    CompilerContext *-- ParseTreeNode
    Token *-- SourceSpan
```

## Main Execution Path

The active runtime flow is:

```text
main
  -> Cli
  -> ComplierOption
  -> Complier
  -> PassManager
      -> PreprocessPass
      -> LexerPass
      -> ParserPass
```

## Class Roles

### `ClI::Cli`

Defined in:

- [cli.hpp](/Users/caojunze424/code/SysyCC/src/cli/cli.hpp)

Role:

- parse command line arguments
- store temporary CLI state
- translate CLI state into [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)

### `sysycc::ComplierOption`

Defined in:

- [complier_option.hpp](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)

Role:

- store the configuration of one compile run
- carry file paths, include search directories, and dump switches

### `sysycc::Complier`

Defined in:

- [complier.hpp](/Users/caojunze424/code/SysyCC/src/compiler/complier.hpp)
- [complier.cpp](/Users/caojunze424/code/SysyCC/src/compiler/complier.cpp)

Role:

- own the compilation pipeline
- initialize passes
- invoke the pass manager

Owned objects:

- `ComplierOption`
- `CompilerContext`
- `PassManager`

### `sysycc::CompilerContext`

Defined in:

- [compiler_context.hpp](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)

Role:

- act as the shared data bus for passes
- store preprocessed intermediate file path
- store include search directories for preprocessing
- store token stream with exact lexical token kinds plus derived categories
- store parse tree root
- store intermediate output paths

### `sysycc::Pass`

Defined in:

- [pass.hpp](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.hpp)

Role:

- abstract interface for one compiler stage

Current concrete subclasses:

- `PreprocessPass`
- `LexerPass`
- `ParserPass`

### `sysycc::PreprocessPass`

Defined in:

- [preprocess.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.hpp)
- [preprocess.cpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/preprocess.cpp)

Role:

- expose the only public class of the preprocess module
- run the preprocessing stage before lexical analysis through `detail::PreprocessSession`
- write the preprocessed intermediate file path back into `CompilerContext`

### `sysycc::PassManager`

Defined in:

- [pass.hpp](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.hpp)
- [pass.cpp](/Users/caojunze424/code/SysyCC/src/compiler/pass/pass.cpp)

Role:

- own pass objects
- prevent duplicate `PassKind`
- run passes in order

Current pipeline order:

- `PreprocessPass`
- `LexerPass`
- `ParserPass`

### `sysycc::LexerPass` and `sysycc::ParserPass`

Defined in:

- [lexer.hpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp)
- [parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser.hpp)

Role:

- connect generated `flex`/`bison` code directly with the pass system
- move lexer and parser output into [CompilerContext](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)
- keep lexer-only runs free of parser-runtime terminal-node allocation
- enable scanner-side terminal-node creation only for parser-driven runs
- create independent scanner sessions with their own lexer runtime state

### `sysycc::LexerState`

Defined in:

- [lexer.hpp](/Users/caojunze424/code/SysyCC/src/frontend/lexer/lexer.hpp)

Role:

- store one scanner session's line/column tracking
- store the current token source span
- control whether scanner actions should emit parse-tree terminal nodes

### `sysycc::preprocess::detail::PreprocessSession`

Defined in:

- [preprocess_session.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_session.hpp)
- [preprocess_session.cpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_session.cpp)

Role:

- coordinate one full preprocessing run
- dispatch lines between directive parsing, macro handling, include handling, and conditional handling
- write the final `.preprocessed.sy` artifact

### `sysycc::preprocess::detail::PreprocessorRuntime`, `MacroTable`, `MacroExpander`, `ConditionalStack`, `DirectiveParser`, `IncludeResolver`, `FileLoader`, and `MacroDefinition`

Defined in:

- [preprocess_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/preprocess_runtime.hpp)
- [macro_table.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/macro_table.hpp)
- [macro_expander.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/macro_expander.hpp)
- [conditional_stack.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/conditional_stack.hpp)
- [directive_parser.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/directive_parser.hpp)
- [include_resolver.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/include_resolver.hpp)
- [file_loader.hpp](/Users/caojunze424/code/SysyCC/src/frontend/preprocess/detail/file_loader.hpp)

Role:

- `PreprocessRuntime`: store preprocessing output lines and file traversal state
- `MacroTable`: manage object-like macro definitions
- `MacroExpander`: expand ordinary source lines with macro substitutions
- `ConditionalStack`: manage nested `#if/#ifdef/#ifndef/#elif/#else/#endif` state
- `DirectiveParser`: parse raw directive text into structured directives
- `IncludeResolver`: resolve local `#include "..."` directives through current-directory and `-I` search paths
- `FileLoader`: load source files into line sequences
- `MacroDefinition`: describe one object-like macro definition

### `sysycc::Token`

Defined in:

- [compiler_context.hpp](/Users/caojunze424/code/SysyCC/src/compiler/compiler_context/compiler_context.hpp)

Role:

- represent one token in the token stream
- store token kind, source text, and [SourceSpan](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

### `sysycc::SourceSpan`

Defined in:

- [source_span.hpp](/Users/caojunze424/code/SysyCC/src/common/source_span.hpp)

Role:

- represent source code begin/end positions
- serve as a reusable location object across modules

### `sysycc::ParseTreeNode`

Defined in:

- [parser_runtime.hpp](/Users/caojunze424/code/SysyCC/src/frontend/parser/parser_runtime.hpp)

Role:

- represent one node in the current parse tree
- store label and child node list

## Notes

- The active pass system lives under
  [src/compiler/pass](/Users/caojunze424/code/SysyCC/src/compiler/pass).
- The active frontend structure lives under
  [src/frontend/lexer](/Users/caojunze424/code/SysyCC/src/frontend/lexer),
  [src/frontend/parser](/Users/caojunze424/code/SysyCC/src/frontend/parser), and
  [src/frontend/preprocess](/Users/caojunze424/code/SysyCC/src/frontend/preprocess).
- The files under [src/pass](/Users/caojunze424/code/SysyCC/src/pass) are not
  the primary class relationship path anymore.
- The current architecture is front-end focused and has not yet introduced AST,
  semantic analysis classes, or IR classes.
