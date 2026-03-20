#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/source_span.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/parser/parser_runtime.hpp"
#include "frontend/semantic/model/semantic_model.hpp"

namespace sysycc {

enum class TokenKind : uint8_t {
    Identifier,
    IntLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    KwConst,
    KwInt,
    KwVoid,
    KwFloat,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwDo,
    KwSwitch,
    KwCase,
    KwDefault,
    KwBreak,
    KwContinue,
    KwReturn,
    KwStruct,
    KwEnum,
    KwTypedef,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Increment,
    Decrement,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,
    Arrow,
    Assign,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LogicalNot,
    LogicalAnd,
    LogicalOr,
    Semicolon,
    Comma,
    Colon,
    Dot,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    EndOfFile,
    Invalid,
};

enum class TokenCategory : uint8_t {
    Identifier,
    Keyword,
    Literal,
    Operator,
    Punctuation,
    Special,
};

// Represents one token produced by lexical analysis.
class Token {
  public:
    Token(TokenKind kind, std::string text, SourceSpan source_span = {})
        : kind_(kind), text_(std::move(text)),
          source_span_(source_span) {}

    TokenKind get_kind() const noexcept { return kind_; }

    const std::string &get_text() const noexcept { return text_; }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    TokenCategory get_category() const noexcept {
        switch (kind_) {
        case TokenKind::Identifier:
            return TokenCategory::Identifier;
        case TokenKind::IntLiteral:
        case TokenKind::FloatLiteral:
        case TokenKind::CharLiteral:
        case TokenKind::StringLiteral:
            return TokenCategory::Literal;
        case TokenKind::KwConst:
        case TokenKind::KwInt:
        case TokenKind::KwVoid:
        case TokenKind::KwFloat:
        case TokenKind::KwIf:
        case TokenKind::KwElse:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwDo:
        case TokenKind::KwSwitch:
        case TokenKind::KwCase:
        case TokenKind::KwDefault:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwReturn:
        case TokenKind::KwStruct:
        case TokenKind::KwEnum:
        case TokenKind::KwTypedef:
            return TokenCategory::Keyword;
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::Increment:
        case TokenKind::Decrement:
        case TokenKind::BitAnd:
        case TokenKind::BitOr:
        case TokenKind::BitXor:
        case TokenKind::BitNot:
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
        case TokenKind::Arrow:
        case TokenKind::Assign:
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
        case TokenKind::LogicalNot:
        case TokenKind::LogicalAnd:
        case TokenKind::LogicalOr:
            return TokenCategory::Operator;
        case TokenKind::Semicolon:
        case TokenKind::Comma:
        case TokenKind::Colon:
        case TokenKind::Dot:
        case TokenKind::LParen:
        case TokenKind::RParen:
        case TokenKind::LBracket:
        case TokenKind::RBracket:
        case TokenKind::LBrace:
        case TokenKind::RBrace:
            return TokenCategory::Punctuation;
        case TokenKind::EndOfFile:
        case TokenKind::Invalid:
            return TokenCategory::Special;
        }

        return TokenCategory::Special;
    }

    const char *get_kind_name() const noexcept {
        switch (kind_) {
        case TokenKind::Identifier:
            return "Identifier";
        case TokenKind::IntLiteral:
            return "IntLiteral";
        case TokenKind::FloatLiteral:
            return "FloatLiteral";
        case TokenKind::CharLiteral:
            return "CharLiteral";
        case TokenKind::StringLiteral:
            return "StringLiteral";
        case TokenKind::KwConst:
            return "KwConst";
        case TokenKind::KwInt:
            return "KwInt";
        case TokenKind::KwVoid:
            return "KwVoid";
        case TokenKind::KwFloat:
            return "KwFloat";
        case TokenKind::KwIf:
            return "KwIf";
        case TokenKind::KwElse:
            return "KwElse";
        case TokenKind::KwWhile:
            return "KwWhile";
        case TokenKind::KwFor:
            return "KwFor";
        case TokenKind::KwDo:
            return "KwDo";
        case TokenKind::KwSwitch:
            return "KwSwitch";
        case TokenKind::KwCase:
            return "KwCase";
        case TokenKind::KwDefault:
            return "KwDefault";
        case TokenKind::KwBreak:
            return "KwBreak";
        case TokenKind::KwContinue:
            return "KwContinue";
        case TokenKind::KwReturn:
            return "KwReturn";
        case TokenKind::KwStruct:
            return "KwStruct";
        case TokenKind::KwEnum:
            return "KwEnum";
        case TokenKind::KwTypedef:
            return "KwTypedef";
        case TokenKind::Plus:
            return "Plus";
        case TokenKind::Minus:
            return "Minus";
        case TokenKind::Star:
            return "Star";
        case TokenKind::Slash:
            return "Slash";
        case TokenKind::Percent:
            return "Percent";
        case TokenKind::Increment:
            return "Increment";
        case TokenKind::Decrement:
            return "Decrement";
        case TokenKind::BitAnd:
            return "BitAnd";
        case TokenKind::BitOr:
            return "BitOr";
        case TokenKind::BitXor:
            return "BitXor";
        case TokenKind::BitNot:
            return "BitNot";
        case TokenKind::ShiftLeft:
            return "ShiftLeft";
        case TokenKind::ShiftRight:
            return "ShiftRight";
        case TokenKind::Arrow:
            return "Arrow";
        case TokenKind::Assign:
            return "Assign";
        case TokenKind::Equal:
            return "Equal";
        case TokenKind::NotEqual:
            return "NotEqual";
        case TokenKind::Less:
            return "Less";
        case TokenKind::LessEqual:
            return "LessEqual";
        case TokenKind::Greater:
            return "Greater";
        case TokenKind::GreaterEqual:
            return "GreaterEqual";
        case TokenKind::LogicalNot:
            return "LogicalNot";
        case TokenKind::LogicalAnd:
            return "LogicalAnd";
        case TokenKind::LogicalOr:
            return "LogicalOr";
        case TokenKind::Semicolon:
            return "Semicolon";
        case TokenKind::Comma:
            return "Comma";
        case TokenKind::Colon:
            return "Colon";
        case TokenKind::Dot:
            return "Dot";
        case TokenKind::LParen:
            return "LParen";
        case TokenKind::RParen:
            return "RParen";
        case TokenKind::LBracket:
            return "LBracket";
        case TokenKind::RBracket:
            return "RBracket";
        case TokenKind::LBrace:
            return "LBrace";
        case TokenKind::RBrace:
            return "RBrace";
        case TokenKind::EndOfFile:
            return "EndOfFile";
        case TokenKind::Invalid:
            return "Invalid";
        }

        return "Unknown";
    }

  private:
    TokenKind kind_;
    std::string text_;
    SourceSpan source_span_;
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
    bool ast_complete_ = false;
    std::string token_dump_file_path_;
    std::string parse_dump_file_path_;
    std::string ast_dump_file_path_;
    std::unique_ptr<ParseTreeNode> parse_tree_root_;
    std::unique_ptr<AstNode> ast_root_;
    std::unique_ptr<SemanticModel> semantic_model_;

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

    bool get_ast_complete() const noexcept { return ast_complete_; }

    void set_ast_complete(bool ast_complete) noexcept {
        ast_complete_ = ast_complete;
    }

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

    const std::string &get_ast_dump_file_path() const noexcept {
        return ast_dump_file_path_;
    }

    void set_ast_dump_file_path(std::string ast_dump_file_path) {
        ast_dump_file_path_ = std::move(ast_dump_file_path);
    }

    const AstNode *get_ast_root() const noexcept { return ast_root_.get(); }

    void set_ast_root(std::unique_ptr<AstNode> ast_root) {
        ast_root_ = std::move(ast_root);
    }

    void clear_ast_root() {
        ast_root_.reset();
        ast_complete_ = false;
    }

    const SemanticModel *get_semantic_model() const noexcept {
        return semantic_model_.get();
    }

    SemanticModel *get_semantic_model() noexcept { return semantic_model_.get(); }

    void set_semantic_model(std::unique_ptr<SemanticModel> semantic_model) {
        semantic_model_ = std::move(semantic_model);
    }

    void clear_semantic_model() { semantic_model_.reset(); }
};
} // namespace sysycc
