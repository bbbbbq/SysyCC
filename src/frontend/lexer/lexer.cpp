#include "frontend/lexer/lexer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "common/source_span.hpp"
#include "frontend/parser/parser.tab.h"

extern FILE *yyin;
extern char *yytext;
extern int yylex(void);
extern void yyrestart(FILE *);
extern void reset_lexer_state(void);
extern int lexer_current_line_begin(void);
extern int lexer_current_column_begin(void);
extern int lexer_current_line_end(void);
extern int lexer_current_column_end(void);

namespace sysycc {

namespace {

TokenKind ToTokenKind(int token) {
    switch (token) {
    case IDENTIFIER:
        return TokenKind::Identifier;
    case INT_LITERAL:
        return TokenKind::Literal;
    case CONST:
    case INT:
    case VOID:
    case IF:
    case ELSE:
    case WHILE:
    case BREAK:
    case CONTINUE:
    case RETURN:
        return TokenKind::Keyword;
    case PLUS:
    case MINUS:
    case MUL:
    case DIV:
    case MOD:
    case ASSIGN:
    case EQ:
    case NE:
    case LT:
    case LE:
    case GT:
    case GE:
    case NOT:
    case AND:
    case OR:
        return TokenKind::Operator;
    default:
        return TokenKind::Punctuation;
    }
}

const char *TokenKindToString(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::Keyword:
        return "Keyword";
    case TokenKind::Literal:
        return "Literal";
    case TokenKind::Operator:
        return "Operator";
    case TokenKind::Punctuation:
        return "Punctuation";
    }

    return "Unknown";
}

} // namespace

PassKind LexerPass::Kind() const { return PassKind::Lex; }

const char *LexerPass::Name() const { return "LexerPass"; }

PassResult LexerPass::Run(CompilerContext &context) {
    context.clear_tokens();
    reset_lexer_state();

    const std::string &lexer_input_file =
        context.get_preprocessed_file_path().empty()
            ? context.get_input_file()
            : context.get_preprocessed_file_path();
    std::FILE *input = std::fopen(lexer_input_file.c_str(), "r");
    if (input == nullptr) {
        return PassResult::Failure("failed to open input file for lexer");
    }
    yyrestart(input);
    yyin = input;

    while (true) {
        const int token = yylex();
        if (token == 0) {
            break;
        }

        context.add_token(
            Token(ToTokenKind(token), yytext == nullptr ? "" : yytext,
                  SourceSpan(lexer_current_line_begin(),
                             lexer_current_column_begin(),
                             lexer_current_line_end(),
                             lexer_current_column_end())));
        if (token == INVALID) {
            std::fclose(input);
            return PassResult::Failure("lexer encountered invalid token");
        }
    }

    std::fclose(input);

    if (context.tokens().empty()) {
        return PassResult::Failure("lexer produced no tokens");
    }

    if (context.get_dump_tokens()) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".tokens.txt");

        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure(
                "failed to open token dump file in intermediate_results");
        }

        for (const Token &token : context.tokens()) {
            ofs << TokenKindToString(token.kind) << " " << token.text << " "
                << token.source_span.get_line_begin() << ":"
                << token.source_span.get_col_begin() << "-"
                << token.source_span.get_line_end() << ":"
                << token.source_span.get_col_end() << "\n";
        }

        context.set_token_dump_file_path(output_file.string());
    }

    return PassResult::Success();
}

} // namespace sysycc
