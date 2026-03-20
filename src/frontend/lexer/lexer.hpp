#pragma once

#include "common/source_span.hpp"
#include "compiler/pass/pass.hpp"

typedef void *yyscan_t;

namespace sysycc {

// Stores per-scanner lexer state for reentrant scanning.
class LexerState {
  private:
    const SourceFile *source_file_ = nullptr;
    int line_ = 1;
    int column_ = 1;
    int token_line_begin_ = 1;
    int token_column_begin_ = 1;
    int token_line_end_ = 1;
    int token_column_end_ = 1;
    bool emit_parse_nodes_ = false;

  public:
    void reset() noexcept {
        source_file_ = nullptr;
        line_ = 1;
        column_ = 1;
        token_line_begin_ = 1;
        token_column_begin_ = 1;
        token_line_end_ = 1;
        token_column_end_ = 1;
        emit_parse_nodes_ = false;
    }

    void set_source_file(const SourceFile *source_file) noexcept {
        source_file_ = source_file;
    }

    const SourceFile *get_source_file() const noexcept { return source_file_; }

    void update_position(const char *text, int length) noexcept {
        token_line_begin_ = line_;
        token_column_begin_ = column_;
        token_line_end_ = line_;
        token_column_end_ = column_;

        for (int index = 0; index < length; ++index) {
            token_line_end_ = line_;
            token_column_end_ = column_;
            if (text[index] == '\n') {
                ++line_;
                column_ = 1;
            } else {
                ++column_;
            }
        }
    }

    int get_token_line_begin() const noexcept { return token_line_begin_; }

    int get_token_column_begin() const noexcept { return token_column_begin_; }

    int get_token_line_end() const noexcept { return token_line_end_; }

    int get_token_column_end() const noexcept { return token_column_end_; }

    bool get_emit_parse_nodes() const noexcept { return emit_parse_nodes_; }

    void set_emit_parse_nodes(bool emit_parse_nodes) noexcept {
        emit_parse_nodes_ = emit_parse_nodes;
    }
};

// Runs lexical analysis and stores the token stream into compiler context.
class LexerPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
