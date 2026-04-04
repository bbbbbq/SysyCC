#include "cli.hpp"

namespace ClI {
namespace {

bool parse_stop_after_stage(const std::string &stage_name,
                            sysycc::StopAfterStage &stage) {
    if (stage_name == "preprocess") {
        stage = sysycc::StopAfterStage::Preprocess;
        return true;
    }
    if (stage_name == "lex") {
        stage = sysycc::StopAfterStage::Lex;
        return true;
    }
    if (stage_name == "parse") {
        stage = sysycc::StopAfterStage::Parse;
        return true;
    }
    if (stage_name == "ast") {
        stage = sysycc::StopAfterStage::Ast;
        return true;
    }
    if (stage_name == "semantic") {
        stage = sysycc::StopAfterStage::Semantic;
        return true;
    }
    if (stage_name == "core-ir") {
        stage = sysycc::StopAfterStage::CoreIr;
        return true;
    }
    if (stage_name == "llvm-ir") {
        stage = sysycc::StopAfterStage::IR;
        return true;
    }
    if (stage_name == "ir") {
        stage = sysycc::StopAfterStage::IR;
        return true;
    }
    if (stage_name == "asm") {
        stage = sysycc::StopAfterStage::Asm;
        return true;
    }
    return false;
}

bool parse_backend_kind(const std::string &backend_name,
                        sysycc::BackendKind &backend_kind) {
    if (backend_name == "llvm-ir") {
        backend_kind = sysycc::BackendKind::LlvmIr;
        return true;
    }
    if (backend_name == "aarch64-native") {
        backend_kind = sysycc::BackendKind::AArch64Native;
        return true;
    }
    return false;
}

} // namespace

void Cli::Run(int argc, char *argv[]) {
    input_file_.clear();
    output_file_.clear();
    include_directories_.clear();
    system_include_directories_ =
        sysycc::detail::get_default_system_include_directories();
    dump_tokens_ = false;
    dump_parse_ = false;
    dump_ast_ = false;
    dump_ir_ = false;
    dump_core_ir_ = false;
    emit_asm_ = false;
    stop_after_stage_ = sysycc::StopAfterStage::None;
    enable_gnu_dialect_ = true;
    enable_clang_dialect_ = true;
    enable_builtin_type_extension_pack_ = true;
    backend_kind_ = sysycc::BackendKind::LlvmIr;
    target_triple_.clear();
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

        if (arg == "--dump-core-ir") {
            dump_core_ir_ = true;
            continue;
        }

        if (arg == "-S") {
            emit_asm_ = true;
            continue;
        }

        if (arg.rfind("--backend=", 0) == 0) {
            const std::string backend_name =
                arg.substr(std::string("--backend=").size());
            if (!parse_backend_kind(backend_name, backend_kind_)) {
                has_error_ = true;
                std::cerr << "error: invalid backend kind: " << backend_name
                          << '\n';
                PrintHelp();
                return;
            }
            continue;
        }

        if (arg == "--backend") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing backend kind after --backend"
                          << '\n';
                PrintHelp();
                return;
            }

            const std::string backend_name = argv[++i];
            if (!parse_backend_kind(backend_name, backend_kind_)) {
                has_error_ = true;
                std::cerr << "error: invalid backend kind: " << backend_name
                          << '\n';
                PrintHelp();
                return;
            }
            continue;
        }

        if (arg.rfind("--target=", 0) == 0) {
            target_triple_ = arg.substr(std::string("--target=").size());
            continue;
        }

        if (arg == "--target") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing target triple after --target"
                          << '\n';
                PrintHelp();
                return;
            }

            target_triple_ = argv[++i];
            continue;
        }

        if (arg.rfind("--stop-after=", 0) == 0) {
            const std::string stage_name =
                arg.substr(std::string("--stop-after=").size());
            if (!parse_stop_after_stage(stage_name, stop_after_stage_)) {
                has_error_ = true;
                std::cerr << "error: invalid stop-after stage: " << stage_name
                          << '\n';
                PrintHelp();
                return;
            }
            continue;
        }

        if (arg == "--stop-after") {
            if (i + 1 >= argc) {
                has_error_ = true;
                std::cerr << "error: missing stage name after --stop-after"
                          << '\n';
                PrintHelp();
                return;
            }

            const std::string stage_name = argv[++i];
            if (!parse_stop_after_stage(stage_name, stop_after_stage_)) {
                has_error_ = true;
                std::cerr << "error: invalid stop-after stage: " << stage_name
                          << '\n';
                PrintHelp();
                return;
            }
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
