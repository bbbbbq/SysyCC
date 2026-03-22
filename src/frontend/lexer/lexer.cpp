#include "frontend/lexer/lexer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "common/diagnostic/diagnostic_engine.hpp"
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
    case ANNOTATION_IDENT:
        return TokenKind::AnnotationIdentifier;
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
    case VOLATILE:
        return TokenKind::KwVolatile;
    case EXTERN:
        return TokenKind::KwExtern;
    case STATIC:
        return TokenKind::KwStatic;
    case ATTRIBUTE:
        return TokenKind::KwAttribute;
    case ASM:
        return TokenKind::KwAsm;
    case INLINE:
        return TokenKind::KwInline;
    case RESTRICT:
        return TokenKind::KwRestrict;
    case NULLABILITY:
        return TokenKind::KwNullability;
    case LONG:
        return TokenKind::KwLong;
    case SIGNED:
        return TokenKind::KwSigned;
    case SHORT:
        return TokenKind::KwShort;
    case UNSIGNED:
        return TokenKind::KwUnsigned;
    case INT:
        return TokenKind::KwInt;
    case CHAR:
        return TokenKind::KwChar;
    case VOID:
        return TokenKind::KwVoid;
    case FLOAT:
        return TokenKind::KwFloat;
    case DOUBLE:
        return TokenKind::KwDouble;
    case FLOAT16:
        return TokenKind::KwFloat16;
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
    case GOTO:
        return TokenKind::KwGoto;
    case RETURN:
        return TokenKind::KwReturn;
    case STRUCT:
        return TokenKind::KwStruct;
    case UNION:
        return TokenKind::KwUnion;
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
    case ADD_ASSIGN:
        return TokenKind::AddAssign;
    case SUB_ASSIGN:
        return TokenKind::SubAssign;
    case MUL_ASSIGN:
        return TokenKind::MulAssign;
    case DIV_ASSIGN:
        return TokenKind::DivAssign;
    case MOD_ASSIGN:
        return TokenKind::ModAssign;
    case SHL_ASSIGN:
        return TokenKind::ShlAssign;
    case SHR_ASSIGN:
        return TokenKind::ShrAssign;
    case AND_ASSIGN:
        return TokenKind::AndAssign;
    case XOR_ASSIGN:
        return TokenKind::XorAssign;
    case OR_ASSIGN:
        return TokenKind::OrAssign;
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
    case ELLIPSIS:
        return TokenKind::Ellipsis;
    case QUESTION:
        return TokenKind::Question;
    case SEMICOLON:
        return TokenKind::Semicolon;
    case COMMA:
        return TokenKind::Comma;
    case COLON:
        return TokenKind::Colon;
    case DOT:
        return TokenKind::Dot;
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
        << "' at ";
    if (source_span.get_file() != nullptr &&
        !source_span.get_file()->empty()) {
        oss << source_span.get_file()->get_path() << ":";
    }
    oss << source_span.get_line_begin() << ":"
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
        const std::string message = "failed to open input file for lexer";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Lexer,
                                                  message);
        return PassResult::Failure(message);
    }

    LexerState lexer_state;
    lexer_state.reset();
    lexer_state.set_source_mapping_view(
        context.get_source_location_service().build_source_mapping_view(
            lexer_input_file));
    lexer_state.set_keyword_registry(
        &context.get_dialect_manager().get_lexer_keyword_registry());
    lexer_state.set_emit_parse_nodes(false);

    yyscan_t scanner = nullptr;
    if (yylex_init_extra(&lexer_state, &scanner) != 0) {
        std::fclose(input);
        const std::string message = "failed to initialize lexer scanner";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Lexer,
                                                  message);
        return PassResult::Failure(message);
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
            const SourceSpan source_span(
                lexer_state.get_token_begin_logical_position(),
                lexer_state.get_token_end_logical_position());
            const std::string invalid_message =
                FormatInvalidTokenMessage(lexeme, source_span);
            context.get_diagnostic_engine().add_error(DiagnosticStage::Lexer,
                                                      invalid_message,
                                                      source_span);
            yylex_destroy(scanner);
            std::fclose(input);
            return PassResult::Failure(invalid_message);
        }

        context.add_token(
            Token(ToTokenKind(token), lexeme == nullptr ? "" : lexeme,
                  SourceSpan(lexer_state.get_token_begin_logical_position(),
                             lexer_state.get_token_end_logical_position())));
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
            const std::string message =
                "failed to open token dump file in intermediate_results";
            context.get_diagnostic_engine().add_error(DiagnosticStage::Lexer,
                                                      message);
            return PassResult::Failure(message);
        }

        for (const Token &token : context.tokens()) {
            const SourceSpan &source_span = token.get_source_span();
            ofs << token.get_kind_name() << " " << token.get_text() << " ";
            if (source_span.get_file() != nullptr &&
                !source_span.get_file()->empty()) {
                ofs << source_span.get_file()->get_path() << ":";
            }
            ofs << source_span.get_line_begin() << ":"
                << source_span.get_col_begin() << "-"
                << source_span.get_line_end() << ":"
                << source_span.get_col_end() << "\n";
        }

        context.set_token_dump_file_path(output_file.string());
    }

    return PassResult::Success();
}

} // namespace sysycc
