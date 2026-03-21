#include "frontend/parser/parser.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "common/diagnostic/diagnostic_engine.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser_runtime.hpp"

extern int yyparse(yyscan_t scanner);
extern int yylex_init_extra(void *user_defined, yyscan_t *scanner);
extern int yylex_destroy(yyscan_t yyscanner);
extern void yyset_in(FILE *input_file, yyscan_t yyscanner);

namespace sysycc {

namespace {

void WriteParseTree(std::ostream &os, const ParseTreeNode *node, int depth) {
    if (node == nullptr) {
        return;
    }

    for (int i = 0; i < depth; ++i) {
        os << "  ";
    }
    os << node->label << "\n";

    for (const std::unique_ptr<ParseTreeNode> &child : node->children) {
        WriteParseTree(os, child.get(), depth + 1);
    }
}

std::string FormatParserErrorMessage(const ParserErrorInfo &error_info) {
    std::string message = "parser error: ";
    message += error_info.get_message().empty() ? "syntax error"
                                                : error_info.get_message();

    const std::string &token_text = error_info.get_token_text();
    if (token_text.empty()) {
        message += " near <end of file>";
    } else {
        message += " near '";
        message += token_text;
        message += "'";
    }

    const SourceSpan &source_span = error_info.get_source_span();
    if (!source_span.empty()) {
        message += " at ";
        if (source_span.get_file() != nullptr &&
            !source_span.get_file()->empty()) {
            message += source_span.get_file()->get_path();
            message += ":";
        }
        message += std::to_string(source_span.get_line_begin());
        message += ":";
        message += std::to_string(source_span.get_col_begin());
        message += "-";
        message += std::to_string(source_span.get_line_end());
        message += ":";
        message += std::to_string(source_span.get_col_end());
    }

    return message;
}

} // namespace

PassKind ParserPass::Kind() const { return PassKind::Parse; }

const char *ParserPass::Name() const { return "ParserPass"; }

PassResult ParserPass::Run(CompilerContext &context) {
    parser_runtime_reset();

    const std::string &parser_input_file =
        context.get_preprocessed_file_path().empty()
            ? context.get_input_file()
            : context.get_preprocessed_file_path();
    std::FILE *input = std::fopen(parser_input_file.c_str(), "r");
    if (input == nullptr) {
        const std::string message = "failed to open input file for parser";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Parser,
                                                  message);
        return PassResult::Failure(message);
    }

    LexerState lexer_state;
    lexer_state.reset();
    lexer_state.set_source_mapping_view(
        context.get_source_location_service().build_source_mapping_view(
            parser_input_file));
    lexer_state.set_emit_parse_nodes(true);

    yyscan_t scanner = nullptr;
    if (yylex_init_extra(&lexer_state, &scanner) != 0) {
        std::fclose(input);
        const std::string message = "failed to initialize parser scanner";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Parser,
                                                  message);
        return PassResult::Failure(message);
    }

    yyset_in(input, scanner);

    const int parse_result = yyparse(scanner);
    yylex_destroy(scanner);
    std::fclose(input);

    context.set_parse_tree_root(take_parse_tree_root());
    const bool parse_success = parse_result == 0;
    const ParserErrorInfo &parser_error_info = get_parser_error_info();
    const std::string parse_message =
        parse_success
            ? "parse succeeded"
            : (parser_error_info.empty()
                   ? "parse failed"
                   : FormatParserErrorMessage(parser_error_info));

    if (context.get_dump_parse()) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".parse.txt");

        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            const std::string message =
                "failed to open parse dump file in intermediate_results";
            context.get_diagnostic_engine().add_error(DiagnosticStage::Parser,
                                                      message);
            return PassResult::Failure(message);
        }

        ofs << "input_file: " << context.get_input_file() << "\n";
        ofs << "parse_success: " << (parse_success ? "true" : "false") << "\n";
        ofs << "parse_message: " << parse_message << "\n";
        ofs << "parse_tree:\n";
        WriteParseTree(ofs, context.get_parse_tree_root(), 1);

        context.set_parse_dump_file_path(output_file.string());
    }

    if (!parse_success) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Parser, parse_message,
            parser_error_info.get_source_span());
        return PassResult::Failure(parse_message);
    }
    return PassResult{true, parse_message};
}

} // namespace sysycc
