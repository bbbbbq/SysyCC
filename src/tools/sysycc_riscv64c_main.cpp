#include "backend/asm_gen/riscv64/api/riscv64_codegen_api.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_usage(const char *program_name) {
    std::cout
        << "Usage: " << program_name << " [options] <input.ll|input.bc>\n"
        << "Options:\n"
        << "  -S                 Emit assembly output\n"
        << "  -c                 Emit object output\n"
        << "  -o <file>          Output path\n"
        << "  --target <triple>  Target triple (default: riscv64-unknown-linux-gnu)\n"
        << "  -fPIC              Enable position-independent code generation\n"
        << "  -g                 Forward debug-info requests to LLVM codegen\n"
        << "  --help             Show this help message\n"
        << "  --version          Show version information\n";
}

void print_version() { std::cout << "sysycc-riscv64c version 0.1.0\n"; }

bool write_text_output(const std::filesystem::path &output_path,
                       const std::string &text) {
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

bool write_binary_output(const std::filesystem::path &output_path,
                         const std::vector<std::uint8_t> &bytes) {
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

void print_diagnostics(
    const std::vector<sysycc::Riscv64CodegenDiagnostic> &diagnostics) {
    for (const sysycc::Riscv64CodegenDiagnostic &diagnostic : diagnostics) {
        std::cerr << diagnostic.stage_name << ": " << diagnostic.message;
        if (!diagnostic.file_path.empty()) {
            std::cerr << " (" << diagnostic.file_path;
            if (diagnostic.line > 0) {
                std::cerr << ":" << diagnostic.line;
                if (diagnostic.column > 0) {
                    std::cerr << ":" << diagnostic.column;
                }
            }
            std::cerr << ")";
        }
        std::cerr << "\n";
    }
}

std::string default_output_path(const std::filesystem::path &input_path,
                                bool emit_object) {
    return input_path.stem().string() + (emit_object ? ".o" : ".s");
}

} // namespace

int main(int argc, char **argv) {
    bool emit_asm = false;
    bool emit_object = false;
    bool request_pic = false;
    bool request_debug = false;
    std::string output_file;
    std::string input_file;
    std::string target_triple = "riscv64-unknown-linux-gnu";

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            print_version();
            return 0;
        }
        if (arg == "-S") {
            emit_asm = true;
            continue;
        }
        if (arg == "-c") {
            emit_object = true;
            continue;
        }
        if (arg == "-fPIC") {
            request_pic = true;
            continue;
        }
        if (arg == "-g") {
            request_debug = true;
            continue;
        }
        if (arg == "-o" || arg == "--target") {
            if (index + 1 >= argc) {
                std::cerr << argv[0] << ": error: missing value after '" << arg
                          << "'\n";
                return 1;
            }
            const std::string value = argv[++index];
            if (arg == "-o") {
                output_file = value;
            } else {
                target_triple = value;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << argv[0] << ": error: unknown option '" << arg
                      << "'\n";
            return 1;
        }
        if (!input_file.empty()) {
            std::cerr << argv[0]
                      << ": error: only one input file is supported\n";
            return 1;
        }
        input_file = arg;
    }

    if (input_file.empty()) {
        std::cerr << argv[0] << ": error: missing input file\n";
        return 1;
    }
    if (emit_asm == emit_object) {
        std::cerr << argv[0] << ": error: choose exactly one of '-S' or '-c'\n";
        return 1;
    }

    const std::filesystem::path input_path(input_file);
    const std::string extension = input_path.extension().string();
    const bool is_ll = extension == ".ll";
    const bool is_bc = extension == ".bc";
    if (!is_ll && !is_bc) {
        std::cerr << argv[0]
                  << ": error: input must be a .ll or .bc file\n";
        return 1;
    }

    sysycc::Riscv64CodegenFileRequest request;
    request.input_file_path = input_file;
    request.options.target_triple = target_triple;
    request.options.position_independent = request_pic;
    request.options.debug_info = request_debug;

    const std::filesystem::path output_path =
        output_file.empty()
            ? std::filesystem::path(default_output_path(input_path, emit_object))
            : std::filesystem::path(output_file);

    if (emit_asm) {
        const sysycc::Riscv64AsmCompileResult result =
            is_bc ? sysycc::compile_bc_file_to_asm(request)
                  : sysycc::compile_ll_file_to_asm(request);
        print_diagnostics(result.diagnostics);
        if (result.status != sysycc::Riscv64CodegenStatus::Success) {
            return 1;
        }
        if (!write_text_output(output_path, result.asm_text)) {
            std::cerr << argv[0] << ": error: failed to write output file '"
                      << output_path.string() << "'\n";
            return 1;
        }
        return 0;
    }

    const sysycc::Riscv64ObjectCompileResult result =
        is_bc ? sysycc::compile_bc_file_to_object(request)
              : sysycc::compile_ll_file_to_object(request);
    print_diagnostics(result.diagnostics);
    if (result.status != sysycc::Riscv64CodegenStatus::Success) {
        return 1;
    }
    if (!write_binary_output(output_path, result.object_bytes)) {
        std::cerr << argv[0] << ": error: failed to write output file '"
                  << output_path.string() << "'\n";
        return 1;
    }
    return 0;
}
