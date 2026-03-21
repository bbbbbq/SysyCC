#pragma once

#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/source_line_map.hpp"
#include "common/source_span.hpp"

namespace sysycc::preprocess::detail {

// Stores output lines, comment state, and file-skipping metadata during
// preprocessing.
class PreprocessRuntime {
  private:
    std::vector<std::string> output_lines_;
    SourceLineMap output_line_map_;
    std::unordered_set<std::string> pragma_once_files_;
    std::unordered_set<std::string> processed_files_;
    bool in_block_comment_ = false;

  public:
    PreprocessRuntime();

    void clear();
    void append_output_line(std::string line, SourcePosition source_position);
    const std::vector<std::string> &get_output_lines() const noexcept {
        return output_lines_;
    }
    const SourceLineMap &get_output_line_map() const noexcept {
        return output_line_map_;
    }

    void mark_pragma_once_file(const std::string &file_path);
    void mark_file_processed(const std::string &file_path);
    bool should_skip_file(const std::string &file_path) const noexcept;
    bool get_in_block_comment() const noexcept { return in_block_comment_; }
    void set_in_block_comment(bool in_block_comment) noexcept {
        in_block_comment_ = in_block_comment;
    }
    std::string build_output_text() const;
};

} // namespace sysycc::preprocess::detail
