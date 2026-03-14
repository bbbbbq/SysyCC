#ifndef SYSYCC_COMPLIER_OPTION_HPP
#define SYSYCC_COMPLIER_OPTION_HPP

#include <string>
#include <utility>

namespace sysycc {

class ComplierOption {
  private:
    std::string input_file_;
    std::string output_file_;
    bool dump_tokens_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;

  public:
    ComplierOption() = default;

    explicit ComplierOption(std::string input_file)
        : input_file_(std::move(input_file)) {}

    ComplierOption(std::string input_file, std::string output_file)
        : input_file_(std::move(input_file)),
          output_file_(std::move(output_file)) {}

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
    }

    const std::string &get_output_file() const noexcept { return output_file_; }

    void set_output_file(std::string output_file) {
        output_file_ = std::move(output_file);
    }

    bool dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool dump_ir() const noexcept { return dump_ir_; }

    void set_dump_ir(bool dump_ir) noexcept { dump_ir_ = dump_ir; }
};

} // namespace sysycc

#endif
