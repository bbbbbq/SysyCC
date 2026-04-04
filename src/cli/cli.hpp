#pragma once
#include <iostream>
#include <string>
#include <vector>

#include "backend/asm_gen/backend_kind.hpp"
#include "compiler/complier_option.hpp"

namespace ClI {
// Parses command line arguments and translates them into compiler options.
class Cli {
  private:
    std::string input_file_;
    std::string output_file_;
    std::vector<std::string> include_directories_;
    std::vector<std::string> system_include_directories_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;
    bool dump_core_ir_ = false;
    bool emit_asm_ = false;
    sysycc::StopAfterStage stop_after_stage_ = sysycc::StopAfterStage::None;
    bool enable_gnu_dialect_ = true;
    bool enable_clang_dialect_ = true;
    bool enable_builtin_type_extension_pack_ = true;
    sysycc::BackendKind backend_kind_ = sysycc::BackendKind::LlvmIr;
    std::string target_triple_;
    bool is_help_ = false;
    bool is_version_ = false;
    bool has_error_ = false;
    std::string version_ = "0.1.0";

  public:
    void Run(int argc, char *argv[]);
    bool get_is_help() const noexcept { return is_help_; }
    bool get_is_version() const noexcept { return is_version_; }
    bool get_has_error() const noexcept { return has_error_; }
    bool has_input_file() const noexcept { return !input_file_.empty(); }

    static void PrintHelp() {
        std::cout << "Usage: sysycc [options] <input_file>\n"
                  << "Options:\n"
                  << "  -I<dir>            Add include search directory\n"
                  << "  -I <dir>           Add include search directory\n"
                  << "  -isystem <dir>     Add system include search directory\n"
                  << "  -o <output_file>   Specify output file\n"
                  << "  -S                 Emit assembly output\n"
                  << "  --dump-tokens      Dump tokens\n"
                  << "  --dump-parse       Dump parse result\n"
                  << "  --dump-ast         Dump AST\n"
                  << "  --dump-ir          Dump IR\n"
                  << "  --dump-core-ir     Dump optimized Core IR\n"
                  << "  --backend <kind>   Select backend (llvm-ir or aarch64-native)\n"
                  << "  --target <triple>  Select target triple\n"
                  << "  --stop-after=<stage>  Stop after preprocess, lex, parse, ast, semantic, core-ir, ir, or asm\n"
                  << "  --strict-c99       Disable GNU/Clang/builtin-type extension packs\n"
                  << "  --enable-gnu-dialect       Enable GNU dialect pack\n"
                  << "  --disable-gnu-dialect      Disable GNU dialect pack\n"
                  << "  --enable-clang-dialect     Enable Clang dialect pack\n"
                  << "  --disable-clang-dialect    Disable Clang dialect pack\n"
                  << "  --enable-builtin-types     Enable builtin-type extension pack\n"
                  << "  --disable-builtin-types    Disable builtin-type extension pack\n"
                  << "  -h, --help         Show this help message and exit\n"
                  << "  -v, --version      Show version information and exit\n";
    }
    void PrintVersion() {
        std::cout << "sysycc version " << version_ << '\n';
    }

    void set_compiler_option(sysycc::ComplierOption &option) const {
        option.set_input_file(input_file_);
        option.set_output_file(output_file_);
        option.set_include_directories(include_directories_);
        std::vector<std::string> merged_system_include_directories =
            option.get_system_include_directories();
        merged_system_include_directories.insert(
            merged_system_include_directories.begin(),
            system_include_directories_.begin(),
            system_include_directories_.end());
        option.set_system_include_directories(
            std::move(merged_system_include_directories));
        option.set_dump_tokens(dump_tokens_);
        option.set_dump_parse(dump_parse_);
        option.set_dump_ast(dump_ast_);
        option.set_dump_ir(dump_ir_);
        option.set_dump_core_ir(dump_core_ir_);
        option.set_emit_asm(emit_asm_);
        option.set_stop_after_stage(stop_after_stage_);
        option.set_enable_gnu_dialect(enable_gnu_dialect_);
        option.set_enable_clang_dialect(enable_clang_dialect_);
        option.set_enable_builtin_type_extension_pack(
            enable_builtin_type_extension_pack_);
        sysycc::BackendOptions backend_options;
        backend_options.set_backend_kind(backend_kind_);
        backend_options.set_target_triple(target_triple_);
        backend_options.set_output_file(output_file_);
        option.set_backend_options(std::move(backend_options));
    }
};
} // namespace ClI
