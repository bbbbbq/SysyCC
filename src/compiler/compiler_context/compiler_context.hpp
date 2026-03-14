#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "frontend/driver/parser_runtime.hpp"

namespace sysycc {

enum class TokenKind {
    Identifier,
    Keyword,
    Literal,
    Operator,
    Punctuation,
};

class Token {
  public:
    TokenKind kind;
    std::string text;
    int line = -1;
    int column = -1;
    Token(TokenKind kind, std::string text, int line = -1, int column = -1)
        : kind(kind), text(std::move(text)), line(line), column(column) {}
};

class CompilerContext {
  private:
    std::string input_file_;
    std::vector<Token> tokens_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool parse_success_ = false;
    std::string parse_message_;
    std::string token_dump_file_path_;
    std::string parse_dump_file_path_;
    std::unique_ptr<ParseTreeNode> parse_tree_root_;

  public:
    CompilerContext() = default;

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
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

    void set_dump_parse(bool dump_parse) noexcept {
        dump_parse_ = dump_parse;
    }

    bool get_parse_success() const noexcept { return parse_success_; }

    void set_parse_success(bool parse_success) noexcept {
        parse_success_ = parse_success;
    }

    const std::string &get_parse_message() const noexcept {
        return parse_message_;
    }

    void set_parse_message(std::string parse_message) {
        parse_message_ = std::move(parse_message);
    }

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
