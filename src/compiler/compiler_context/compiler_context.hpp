#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/source_span.hpp"
#include "frontend/parser/parser_runtime.hpp"

namespace sysycc {

enum class TokenKind {
    Identifier,
    Keyword,
    Literal,
    Operator,
    Punctuation,
};

// Represents one token produced by lexical analysis.
class Token {
  public:
    TokenKind kind;
    std::string text;
    SourceSpan source_span;

    Token(TokenKind kind, std::string text, SourceSpan source_span = {})
        : kind(kind), text(std::move(text)),
          source_span(std::move(source_span)) {}
};

// Acts as the shared data bus across compiler passes.
class CompilerContext {
  private:
    std::string input_file_;
    std::string preprocessed_file_path_;
    std::vector<std::string> include_directories_;
    std::vector<Token> tokens_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    std::string token_dump_file_path_;
    std::string parse_dump_file_path_;
    std::unique_ptr<ParseTreeNode> parse_tree_root_;

  public:
    CompilerContext() = default;

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
    }

    const std::string &get_preprocessed_file_path() const noexcept {
        return preprocessed_file_path_;
    }

    void set_preprocessed_file_path(std::string preprocessed_file_path) {
        preprocessed_file_path_ = std::move(preprocessed_file_path);
    }

    const std::vector<std::string> &get_include_directories() const noexcept {
        return include_directories_;
    }

    void set_include_directories(std::vector<std::string> include_directories) {
        include_directories_ = std::move(include_directories);
    }

    const std::vector<Token> &tokens() const { return tokens_; }

    std::vector<Token> &get_tokens() noexcept { return tokens_; }

    void clear_tokens() { tokens_.clear(); }

    void add_token(Token token) { tokens_.push_back(std::move(token)); }

    bool get_dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool get_dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool get_dump_parse() const noexcept { return dump_parse_; }

    void set_dump_parse(bool dump_parse) noexcept { dump_parse_ = dump_parse; }

    const std::string &get_token_dump_file_path() const noexcept {
        return token_dump_file_path_;
    }

    void set_token_dump_file_path(std::string token_dump_file_path) {
        token_dump_file_path_ = std::move(token_dump_file_path);
    }

    const std::string &get_parse_dump_file_path() const noexcept {
        return parse_dump_file_path_;
    }

    void set_parse_dump_file_path(std::string parse_dump_file_path) {
        parse_dump_file_path_ = std::move(parse_dump_file_path);
    }

    const ParseTreeNode *get_parse_tree_root() const noexcept {
        return parse_tree_root_.get();
    }

    void set_parse_tree_root(std::unique_ptr<ParseTreeNode> parse_tree_root) {
        parse_tree_root_ = std::move(parse_tree_root);
    }
};
} // namespace sysycc
