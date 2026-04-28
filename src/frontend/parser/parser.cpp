#include "frontend/parser/parser.hpp"

#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "common/diagnostic/diagnostic_engine.hpp"
#include "common/intermediate_results_path.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser_feature_validator.hpp"
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
    std::string message = error_info.get_message().empty() ? "syntax error"
                                                           : error_info.get_message();

    const std::string &token_text = error_info.get_token_text();
    if (token_text.empty()) {
        message += " near <end of file>";
    } else {
        message += " near '";
        message += token_text;
        message += "'";
    }

    return message;
}

bool IsIdentifierStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsIdentifierContinue(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsTypedefPrescanMarkerIdentifier(const std::string &identifier) {
    return identifier == "__attribute__" || identifier == "__attribute" ||
           identifier == "__asm__" || identifier == "__asm" ||
           identifier == "__extension__";
}

void RegisterLastIdentifierInChunk(const std::string &chunk) {
    std::string last_identifier;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    for (std::size_t index = 0; index < chunk.size();) {
        const char ch = chunk[index];
        if (ch == '(') {
            ++paren_depth;
            ++index;
            continue;
        }
        if (ch == ')' && paren_depth > 0) {
            --paren_depth;
            ++index;
            continue;
        }
        if (ch == '{') {
            ++brace_depth;
            ++index;
            continue;
        }
        if (ch == '}' && brace_depth > 0) {
            --brace_depth;
            ++index;
            continue;
        }
        if (ch == '[') {
            ++bracket_depth;
            ++index;
            continue;
        }
        if (ch == ']' && bracket_depth > 0) {
            --bracket_depth;
            ++index;
            continue;
        }
        if (!IsIdentifierStart(chunk[index])) {
            ++index;
            continue;
        }
        const std::size_t start = index;
        ++index;
        while (index < chunk.size() && IsIdentifierContinue(chunk[index])) {
            ++index;
        }
        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            std::string identifier = chunk.substr(start, index - start);
            if (!IsTypedefPrescanMarkerIdentifier(identifier)) {
                last_identifier = std::move(identifier);
            }
        }
    }
    register_typedef_name(last_identifier);
}

void RegisterFunctionPointerTypedefNames(const std::string &decl) {
    for (std::size_t index = 0; index < decl.size(); ++index) {
        if (decl[index] != '(') {
            continue;
        }
        std::size_t cursor = index + 1;
        while (cursor < decl.size() &&
               std::isspace(static_cast<unsigned char>(decl[cursor])) != 0) {
            ++cursor;
        }
        bool saw_pointer = false;
        while (cursor < decl.size() && decl[cursor] == '*') {
            saw_pointer = true;
            ++cursor;
            while (cursor < decl.size() &&
                   std::isspace(static_cast<unsigned char>(decl[cursor])) != 0) {
                ++cursor;
            }
        }
        if (!saw_pointer || cursor >= decl.size() ||
            !IsIdentifierStart(decl[cursor])) {
            continue;
        }
        const std::size_t name_start = cursor;
        ++cursor;
        while (cursor < decl.size() && IsIdentifierContinue(decl[cursor])) {
            ++cursor;
        }
        register_typedef_name(decl.substr(name_start, cursor - name_start));
    }
}

void RegisterTopLevelTypedefDeclarators(const std::string &decl) {
    std::size_t chunk_start = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    for (std::size_t index = 0; index <= decl.size(); ++index) {
        const char ch = index < decl.size() ? decl[index] : ',';
        if (ch == '(') {
            ++paren_depth;
        } else if (ch == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
            --brace_depth;
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']' && bracket_depth > 0) {
            --bracket_depth;
        }
        if ((ch == ',' || index == decl.size()) && paren_depth == 0 &&
            brace_depth == 0 && bracket_depth == 0) {
            const std::string chunk = decl.substr(chunk_start,
                                                  index - chunk_start);
            if (chunk.find("(*") == std::string::npos) {
                RegisterLastIdentifierInChunk(chunk);
            }
            chunk_start = index + 1;
        }
    }
}

void PrimeTypedefNamesFromSource(const std::string &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    for (std::size_t index = 0; index < source.size();) {
        if ((index > 0 && IsIdentifierContinue(source[index - 1])) ||
            source.compare(index, 7, "typedef") != 0 ||
            (index + 7 < source.size() &&
             IsIdentifierContinue(source[index + 7]))) {
            ++index;
            continue;
        }
        std::size_t cursor = index + 7;
        int paren_depth = 0;
        int brace_depth = 0;
        int bracket_depth = 0;
        for (; cursor < source.size(); ++cursor) {
            const char ch = source[cursor];
            if (ch == '(') {
                ++paren_depth;
            } else if (ch == ')' && paren_depth > 0) {
                --paren_depth;
            } else if (ch == '{') {
                ++brace_depth;
            } else if (ch == '}' && brace_depth > 0) {
                --brace_depth;
            } else if (ch == '[') {
                ++bracket_depth;
            } else if (ch == ']' && bracket_depth > 0) {
                --bracket_depth;
            } else if (ch == ';' && paren_depth == 0 && brace_depth == 0 &&
                       bracket_depth == 0) {
                break;
            }
        }
        if (cursor >= source.size()) {
            return;
        }
        const std::string decl =
            source.substr(index + 7, cursor - (index + 7));
        RegisterFunctionPointerTypedefNames(decl);
        RegisterTopLevelTypedefDeclarators(decl);
        index = cursor + 1;
    }
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
    PrimeTypedefNamesFromSource(parser_input_file);
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
    lexer_state.set_keyword_registry(
        &context.get_dialect_manager().get_lexer_keyword_registry());
    lexer_state.set_classify_typedef_names(true);
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
    bool parse_success = parse_result == 0;
    ParserErrorInfo parser_error_info = get_parser_error_info();
    if (parse_success) {
        ParserFeatureValidator feature_validator;
        if (!feature_validator.validate(
                context.get_parse_tree_root(),
                context.get_dialect_manager().get_parser_feature_registry(),
                parser_error_info)) {
            parse_success = false;
        }
    }
    const std::string parse_message =
        parse_success
            ? "parse succeeded"
            : (parser_error_info.empty()
                   ? "parse failed"
                   : FormatParserErrorMessage(parser_error_info));

    if (context.get_dump_parse()) {
        const std::filesystem::path output_dir =
            sysycc::get_intermediate_results_dir();
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
