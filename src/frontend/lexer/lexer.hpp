#pragma once

#include <string_view>

#include "common/source_mapping_view.hpp"
#include "common/source_span.hpp"
#include "compiler/pass/pass.hpp"
#include "frontend/dialects/lexer_keyword_registry.hpp"

typedef void *yyscan_t;

namespace sysycc {

// Stores per-scanner lexer state for reentrant scanning.
class LexerState {
  private:
    SourceMappingView source_mapping_view_;
    const LexerKeywordRegistry *keyword_registry_ = nullptr;
    bool classify_typedef_names_ = false;
    int line_ = 1;
    int column_ = 1;
    int token_line_begin_ = 1;
    int token_column_begin_ = 1;
    int token_line_end_ = 1;
    int token_column_end_ = 1;
    bool emit_parse_nodes_ = false;

  public:
    void reset() noexcept {
        source_mapping_view_ = SourceMappingView();
        keyword_registry_ = nullptr;
        classify_typedef_names_ = false;
        line_ = 1;
        column_ = 1;
        token_line_begin_ = 1;
        token_column_begin_ = 1;
        token_line_end_ = 1;
        token_column_end_ = 1;
        emit_parse_nodes_ = false;
    }

    void set_source_mapping_view(SourceMappingView source_mapping_view) noexcept {
        source_mapping_view_ = source_mapping_view;
    }

    const SourceMappingView &get_source_mapping_view() const noexcept {
        return source_mapping_view_;
    }

    void set_keyword_registry(
        const LexerKeywordRegistry *keyword_registry) noexcept {
        keyword_registry_ = keyword_registry;
    }

    const LexerKeywordRegistry *get_keyword_registry() const noexcept {
        return keyword_registry_;
    }

    void set_classify_typedef_names(bool classify_typedef_names) noexcept {
        classify_typedef_names_ = classify_typedef_names;
    }

    bool get_classify_typedef_names() const noexcept {
        return classify_typedef_names_;
    }

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

    SourcePosition get_token_begin_physical_position() const noexcept {
        return source_mapping_view_.get_physical_position(token_line_begin_,
                                                          token_column_begin_);
    }

    SourcePosition get_token_end_physical_position() const noexcept {
        return source_mapping_view_.get_physical_position(token_line_end_,
                                                          token_column_end_);
    }

    SourcePosition get_token_begin_logical_position() const noexcept {
        return source_mapping_view_.get_logical_position(token_line_begin_,
                                                         token_column_begin_);
    }

    SourcePosition get_token_end_logical_position() const noexcept {
        return source_mapping_view_.get_logical_position(token_line_end_,
                                                         token_column_end_);
    }

    SourcePosition get_token_begin_position() const noexcept {
        return get_token_begin_logical_position();
    }

    SourcePosition get_token_end_position() const noexcept {
        return get_token_end_logical_position();
    }

    bool get_emit_parse_nodes() const noexcept { return emit_parse_nodes_; }

    void set_emit_parse_nodes(bool emit_parse_nodes) noexcept {
        emit_parse_nodes_ = emit_parse_nodes;
    }
};

inline TokenKind classify_identifier_kind(const LexerState &state,
                                          std::string_view text) noexcept {
    const auto *keyword_registry = state.get_keyword_registry();
    if (keyword_registry == nullptr || !keyword_registry->has_keyword(text)) {
        return TokenKind::Identifier;
    }
    return keyword_registry->get_keyword_kind(text);
}

// Runs lexical analysis and stores the token stream into compiler context.
class LexerPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
