#pragma once

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace sysycc::preprocess::detail {

// Stores output lines and file traversal state during preprocessing.
class PreprocessRuntime {
  private:
    std::vector<std::string> output_lines_;
    std::vector<std::string> file_stack_;
    bool in_block_comment_ = false;

  public:
    PreprocessRuntime();

    void clear();
    void append_output_line(std::string line);
    const std::vector<std::string> &get_output_lines() const noexcept {
        return output_lines_;
    }

    void push_file(std::string file_path);
    void pop_file();
    const std::string &get_current_file() const noexcept;
    bool get_in_block_comment() const noexcept { return in_block_comment_; }
    void set_in_block_comment(bool in_block_comment) noexcept {
        in_block_comment_ = in_block_comment;
    }
    std::string build_output_text() const;
};

} // namespace sysycc::preprocess::detail
