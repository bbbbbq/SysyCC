#include "frontend/driver/parser_driver.hpp"

#include <filesystem>
#include <fstream>
#include <cstdio>

#include "frontend/driver/parser_runtime.hpp"

extern FILE *yyin;
extern int yyparse(void);
extern void yyrestart(FILE *);

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

} // namespace

PassResult ParserDriver::Run(CompilerContext &context) const {
    std::FILE *input = std::fopen(context.get_input_file().c_str(), "r");
    if (input == nullptr) {
        context.set_parse_success(false);
        context.set_parse_message("failed to open input file for parser");
        return PassResult::Failure(context.get_parse_message());
    }

    parser_runtime_reset();
    yyrestart(input);
    yyin = input;

    const int parse_result = yyparse();
    std::fclose(input);

    context.set_parse_tree_root(take_parse_tree_root());

    context.set_parse_success(parse_result == 0);
    context.set_parse_message(parse_result == 0 ? "parse succeeded"
                                                : "parse failed");

    if (context.get_dump_parse()) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".parse.txt");

        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure(
                "failed to open parse dump file in intermediate_results");
        }

        ofs << "input_file: " << context.get_input_file() << "\n";
        ofs << "parse_success: "
            << (context.get_parse_success() ? "true" : "false") << "\n";
        ofs << "parse_message: " << context.get_parse_message() << "\n";
        ofs << "parse_tree:\n";
        WriteParseTree(ofs, context.get_parse_tree_root(), 1);

        context.set_parse_dump_file_path(output_file.string());
    }

    return parse_result == 0 ? PassResult::Success()
                             : PassResult::Failure(context.get_parse_message());
}

PassKind ParserPass::Kind() const { return PassKind::Parse; }

const char *ParserPass::Name() const { return "ParserPass"; }

PassResult ParserPass::Run(CompilerContext &context) {
    return parser_driver_.Run(context);
}

} // namespace sysycc
