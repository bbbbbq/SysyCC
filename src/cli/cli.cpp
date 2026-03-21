#include "cli.hpp"

namespace ClI {
void Cli::Run(int argc, char *argv[]) {
    input_file_.clear();
    output_file_.clear();
    include_directories_.clear();
    system_include_directories_.clear();
    dump_tokens_ = false;
    dump_parse_ = false;
    dump_ast_ = false;
    dump_ir_ = false;
    enable_gnu_dialect_ = true;
    enable_clang_dialect_ = true;
    enable_builtin_type_extension_pack_ = true;
    is_help_ = false;
    is_version_ = false;
    has_error_ = false;

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

        if (arg == "--dump-parse") {
            dump_parse_ = true;
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

        if (arg == "--strict-c99") {
            enable_gnu_dialect_ = false;
            enable_clang_dialect_ = false;
            enable_builtin_type_extension_pack_ = false;
            continue;
        }

        if (arg == "--enable-gnu-dialect") {
            enable_gnu_dialect_ = true;
            continue;
        }

        if (arg == "--disable-gnu-dialect") {
            enable_gnu_dialect_ = false;
            continue;
        }

        if (arg == "--enable-clang-dialect") {
            enable_clang_dialect_ = true;
            continue;
        }

        if (arg == "--disable-clang-dialect") {
            enable_clang_dialect_ = false;
            continue;
        }

        if (arg == "--enable-builtin-types") {
            enable_builtin_type_extension_pack_ = true;
            continue;
        }

        if (arg == "--disable-builtin-types") {
            enable_builtin_type_extension_pack_ = false;
            continue;
        }

        if (arg == "-o") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing output file after -o" << '\n';
                PrintHelp();
                return;
            }

            output_file_ = argv[++i];
            continue;
        }

        if (arg == "-I") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing include directory after -I"
                          << '\n';
                PrintHelp();
                return;
            }

            include_directories_.push_back(argv[++i]);
            continue;
        }

        if (arg == "-isystem") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing include directory after -isystem"
                          << '\n';
                PrintHelp();
                return;
            }

            system_include_directories_.push_back(argv[++i]);
            continue;
        }

        if (arg.size() > 2 && arg.rfind("-I", 0) == 0) {
            include_directories_.push_back(arg.substr(2));
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            has_error_ = true;
            std::cerr << "error: unknown option: " << arg << '\n';
            PrintHelp();
            return;
        }

        if (input_file_.empty()) {
            input_file_ = arg;
            continue;
        }

        has_error_ = true;
        std::cerr << "error: multiple input files are not supported: " << arg
                  << '\n';
        PrintHelp();
        return;
    }

    if (input_file_.empty()) {
        has_error_ = true;
        std::cerr << "error: missing input file" << '\n';
        PrintHelp();
    }
}
} // namespace ClI
