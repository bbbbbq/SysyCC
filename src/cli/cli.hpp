#pragma once
#include <iostream>
#include <string>

#include "compiler/complier_option.hpp"

namespace ClI {
class Cli {
  private:
    std::string input_file_;
    std::string output_file_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;
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
                  << "  -o <output_file>   Specify output file\n"
                  << "  --dump-tokens      Dump tokens\n"
                  << "  --dump-parse       Dump parse result\n"
                  << "  --dump-ast         Dump AST\n"
                  << "  --dump-ir          Dump IR\n"
                  << "  -h, --help         Show this help message and exit\n"
                  << "  -v, --version      Show version information and exit\n";
    }
    void PrintVersion() {
        std::cout << "sysycc version " << version_ << std::endl;
    }

    void set_compiler_option(sysycc::ComplierOption &option) const {
        option.set_input_file(input_file_);
        option.set_output_file(output_file_);
        option.set_dump_tokens(dump_tokens_);
        option.set_dump_parse(dump_parse_);
        option.set_dump_ast(dump_ast_);
        option.set_dump_ir(dump_ir_);
    }
};
} // namespace ClI
