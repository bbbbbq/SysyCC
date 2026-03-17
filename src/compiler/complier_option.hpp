#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sysycc {

// Stores the configuration for one compiler invocation.
class ComplierOption {
  private:
    std::string input_file_;
    std::string output_file_;
    std::vector<std::string> include_directories_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
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

    const std::vector<std::string> &get_include_directories() const noexcept {
        return include_directories_;
    }

    void set_include_directories(std::vector<std::string> include_directories) {
        include_directories_ = std::move(include_directories);
    }

    void add_include_directory(std::string include_directory) {
        include_directories_.push_back(std::move(include_directory));
    }

    bool dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool dump_parse() const noexcept { return dump_parse_; }

    void set_dump_parse(bool dump_parse) noexcept { dump_parse_ = dump_parse; }

    bool dump_ir() const noexcept { return dump_ir_; }

    void set_dump_ir(bool dump_ir) noexcept { dump_ir_ = dump_ir; }
};

} // namespace sysycc
