#include "frontend/preprocess/detail/preprocess_runtime.hpp"

namespace sysycc::preprocess::detail {

PreprocessRuntime::PreprocessRuntime() = default;

void PreprocessRuntime::clear() {
    output_lines_.clear();
    output_line_map_.clear();
    pragma_once_files_.clear();
    processed_files_.clear();
    in_block_comment_ = false;
}

void PreprocessRuntime::append_output_line(std::string line,
                                           SourcePosition source_position) {
    output_lines_.push_back(std::move(line));
    output_line_map_.add_line_position(source_position);
}

void PreprocessRuntime::mark_pragma_once_file(const std::string &file_path) {
    pragma_once_files_.insert(file_path);
}

void PreprocessRuntime::mark_file_processed(const std::string &file_path) {
    processed_files_.insert(file_path);
}

bool PreprocessRuntime::should_skip_file(
    const std::string &file_path) const noexcept {
    return pragma_once_files_.find(file_path) != pragma_once_files_.end();
}

std::string PreprocessRuntime::build_output_text() const {
    std::ostringstream oss;
    for (std::size_t index = 0; index < output_lines_.size(); ++index) {
        if (index != 0) {
            oss << '\n';
        }
        oss << output_lines_[index];
    }
    return oss.str();
}

} // namespace sysycc::preprocess::detail
