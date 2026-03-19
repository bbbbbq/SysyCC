#include "frontend/lexer/lexer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "common/source_span.hpp"
#include "frontend/parser/parser.tab.h"

extern int yylex(YYSTYPE *yylval_param, yyscan_t yyscanner);
extern int yylex_init_extra(void *user_defined, yyscan_t *scanner);
extern int yylex_destroy(yyscan_t yyscanner);
extern void yyset_in(FILE *input_file, yyscan_t yyscanner);
extern char *yyget_text(yyscan_t yyscanner);

namespace sysycc {

namespace {

TokenKind ToTokenKind(int token) {
    switch (token) {
    case IDENTIFIER:
        return TokenKind::Identifier;
    case INT_LITERAL:
        return TokenKind::IntLiteral;
    case FLOAT_LITERAL:
        return TokenKind::FloatLiteral;
    case CHAR_LITERAL:
        return TokenKind::CharLiteral;
    case STRING_LITERAL:
        return TokenKind::StringLiteral;
    case CONST:
        return TokenKind::KwConst;
    case INT:
        return TokenKind::KwInt;
    case VOID:
        return TokenKind::KwVoid;
    case FLOAT:
        return TokenKind::KwFloat;
    case IF:
        return TokenKind::KwIf;
    case ELSE:
        return TokenKind::KwElse;
    case WHILE:
        return TokenKind::KwWhile;
    case FOR:
        return TokenKind::KwFor;
    case DO:
        return TokenKind::KwDo;
    case SWITCH:
        return TokenKind::KwSwitch;
    case CASE:
        return TokenKind::KwCase;
    case DEFAULT:
        return TokenKind::KwDefault;
    case BREAK:
        return TokenKind::KwBreak;
    case CONTINUE:
        return TokenKind::KwContinue;
    case RETURN:
        return TokenKind::KwReturn;
    case STRUCT:
        return TokenKind::KwStruct;
    case ENUM:
        return TokenKind::KwEnum;
    case TYPEDEF:
        return TokenKind::KwTypedef;
    case PLUS:
        return TokenKind::Plus;
    case MINUS:
        return TokenKind::Minus;
    case MUL:
        return TokenKind::Star;
    case DIV:
        return TokenKind::Slash;
    case MOD:
        return TokenKind::Percent;
    case INC:
        return TokenKind::Increment;
    case DEC:
        return TokenKind::Decrement;
    case BITAND:
        return TokenKind::BitAnd;
    case BITOR:
        return TokenKind::BitOr;
    case BITXOR:
        return TokenKind::BitXor;
    case BITNOT:
        return TokenKind::BitNot;
    case SHL:
        return TokenKind::ShiftLeft;
    case SHR:
        return TokenKind::ShiftRight;
    case ARROW:
        return TokenKind::Arrow;
    case ASSIGN:
        return TokenKind::Assign;
    case EQ:
        return TokenKind::Equal;
    case NE:
        return TokenKind::NotEqual;
    case LT:
        return TokenKind::Less;
    case LE:
        return TokenKind::LessEqual;
    case GT:
        return TokenKind::Greater;
    case GE:
        return TokenKind::GreaterEqual;
    case NOT:
        return TokenKind::LogicalNot;
    case AND:
        return TokenKind::LogicalAnd;
    case OR:
        return TokenKind::LogicalOr;
    case SEMICOLON:
        return TokenKind::Semicolon;
    case COMMA:
        return TokenKind::Comma;
    case COLON:
        return TokenKind::Colon;
    case LPAREN:
        return TokenKind::LParen;
    case RPAREN:
        return TokenKind::RParen;
    case LBRACKET:
        return TokenKind::LBracket;
    case RBRACKET:
        return TokenKind::RBracket;
    case LBRACE:
        return TokenKind::LBrace;
    case RBRACE:
        return TokenKind::RBrace;
    case INVALID:
        return TokenKind::Invalid;
    case 0:
        return TokenKind::EndOfFile;
    default:
        return TokenKind::Invalid;
    }
}

std::string FormatInvalidTokenMessage(const char *lexeme,
                                      const SourceSpan &source_span) {
    std::ostringstream oss;
    oss << "lexer encountered invalid token '"
        << (lexeme == nullptr || lexeme[0] == '\0' ? "<unknown>" : lexeme)
        << "' at " << source_span.get_line_begin() << ":"
        << source_span.get_col_begin() << "-" << source_span.get_line_end()
        << ":" << source_span.get_col_end();
    return oss.str();
}

} // namespace

PassKind LexerPass::Kind() const { return PassKind::Lex; }

const char *LexerPass::Name() const { return "LexerPass"; }

PassResult LexerPass::Run(CompilerContext &context) {
    context.clear_tokens();

    const std::string &lexer_input_file =
        context.get_preprocessed_file_path().empty()
            ? context.get_input_file()
            : context.get_preprocessed_file_path();
    std::FILE *input = std::fopen(lexer_input_file.c_str(), "r");
    if (input == nullptr) {
        return PassResult::Failure("failed to open input file for lexer");
    }

    LexerState lexer_state;
    lexer_state.reset();
    lexer_state.set_emit_parse_nodes(false);

    yyscan_t scanner = nullptr;
    if (yylex_init_extra(&lexer_state, &scanner) != 0) {
        std::fclose(input);
        return PassResult::Failure("failed to initialize lexer scanner");
    }

    yyset_in(input, scanner);
    YYSTYPE semantic_value = {};

    while (true) {
        const int token = yylex(&semantic_value, scanner);
        if (token == 0) {
            break;
        }

        const char *lexeme = yyget_text(scanner);

        if (token == INVALID) {
            const SourceSpan source_span(lexer_state.get_token_line_begin(),
                                         lexer_state.get_token_column_begin(),
                                         lexer_state.get_token_line_end(),
                                         lexer_state.get_token_column_end());
            const std::string invalid_message =
                FormatInvalidTokenMessage(lexeme, source_span);
            yylex_destroy(scanner);
            std::fclose(input);
            return PassResult::Failure(invalid_message);
        }

        context.add_token(
            Token(ToTokenKind(token), lexeme == nullptr ? "" : lexeme,
                  SourceSpan(lexer_state.get_token_line_begin(),
                             lexer_state.get_token_column_begin(),
                             lexer_state.get_token_line_end(),
                             lexer_state.get_token_column_end())));
    }

    yylex_destroy(scanner);
    std::fclose(input);

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
            const SourceSpan &source_span = token.get_source_span();
            ofs << token.get_kind_name() << " " << token.get_text() << " "
                << source_span.get_line_begin() << ":"
                << source_span.get_col_begin() << "-"
                << source_span.get_line_end() << ":"
                << source_span.get_col_end() << "\n";
        }

        context.set_token_dump_file_path(output_file.string());
    }

    return PassResult::Success();
}

} // namespace sysycc
