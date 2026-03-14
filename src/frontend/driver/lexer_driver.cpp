#include "frontend/driver/lexer_driver.hpp"

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>

#include "frontend/grammer/parser.tab.h"

extern FILE *yyin;
extern char *yytext;
extern int yylex(void);
extern void yyrestart(FILE *);
extern void reset_lexer_state(void);
extern int lexer_current_line(void);
extern int lexer_current_column(void);

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

PassResult LexerDriver::Run(CompilerContext &context) const {
    std::FILE *input = std::fopen(context.get_input_file().c_str(), "r");
    if (input == nullptr) {
        return PassResult::Failure("failed to open input file for lexer");
    }

    context.clear_tokens();
    reset_lexer_state();
    yyrestart(input);
    yyin = input;

    while (true) {
        const int token = yylex();
        if (token == 0) {
            break;
        }

        context.add_token(
            Token(ToTokenKind(token), yytext == nullptr ? "" : yytext,
                  lexer_current_line(), lexer_current_column()));
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
                << token.line << ":" << token.column << "\n";
        }

        context.set_token_dump_file_path(output_file.string());
    }

    return PassResult::Success();
}

PassKind LexerPass::Kind() const { return PassKind::Lex; }

const char *LexerPass::Name() const { return "LexerPass"; }

PassResult LexerPass::Run(CompilerContext &context) {
    return lexer_driver_.Run(context);
}

} // namespace sysycc
