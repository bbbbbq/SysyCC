#include "cli.hpp"

namespace ClI {
void Cli::Run(int argc, char *argv[]) {
    input_file_.clear();
    output_file_.clear();
    dump_tokens_ = false;
    dump_ast_ = false;
    dump_ir_ = false;
    is_help_ = false;
    is_version_ = false;

    if (argc <= 1) {
        is_help_ = true;
        PrintHelp();
        return;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            is_help_ = true;
            PrintHelp();
            return;
        }

        if (arg == "-v" || arg == "--version") {
            is_version_ = true;
            PrintVersion();
            return;
        }

        if (arg == "--dump-tokens") {
            dump_tokens_ = true;
            continue;
        }

        if (arg == "--dump-ast") {
            dump_ast_ = true;
            continue;
        }

        if (arg == "--dump-ir") {
            dump_ir_ = true;
            continue;
        }

        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing output file after -o" << std::endl;
                PrintHelp();
                return;
            }

            output_file_ = argv[++i];
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option: " << arg << std::endl;
            PrintHelp();
            return;
        }

        if (input_file_.empty()) {
            input_file_ = arg;
            continue;
        }

        std::cerr << "error: multiple input files are not supported: " << arg
                  << std::endl;
        PrintHelp();
        return;
    }

    if (input_file_.empty()) {
        std::cerr << "error: missing input file" << std::endl;
        PrintHelp();
    }
}
} // namespace ClI
