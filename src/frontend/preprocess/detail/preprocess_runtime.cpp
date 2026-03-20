#include "frontend/preprocess/detail/preprocess_runtime.hpp"

namespace sysycc::preprocess::detail {

PreprocessRuntime::PreprocessRuntime() = default;

void PreprocessRuntime::clear() {
    output_lines_.clear();
    file_stack_.clear();
    pragma_once_files_.clear();
    processed_files_.clear();
    in_block_comment_ = false;
}

void PreprocessRuntime::append_output_line(std::string line) {
    output_lines_.push_back(std::move(line));
}

void PreprocessRuntime::push_file(std::string file_path) {
    file_stack_.push_back(std::move(file_path));
}

void PreprocessRuntime::pop_file() {
    if (!file_stack_.empty()) {
        file_stack_.pop_back();
    }
}

const std::string &PreprocessRuntime::get_current_file() const noexcept {
    static const std::string kEmpty;
    if (file_stack_.empty()) {
        return kEmpty;
    }

    return file_stack_.back();
}

bool PreprocessRuntime::has_file_in_stack(const std::string &file_path) const noexcept {
    // The active file stack doubles as a lightweight include-cycle detector.
    for (const std::string &current_file_path : file_stack_) {
        if (current_file_path == file_path) {
            return true;
        }
    }

    return false;
}

void PreprocessRuntime::mark_pragma_once_file(const std::string &file_path) {
    pragma_once_files_.insert(file_path);
}

void PreprocessRuntime::mark_file_processed(const std::string &file_path) {
    processed_files_.insert(file_path);
}

bool PreprocessRuntime::should_skip_file(
    const std::string &file_path) const noexcept {
    return pragma_once_files_.find(file_path) != pragma_once_files_.end() &&
           processed_files_.find(file_path) != processed_files_.end();
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
