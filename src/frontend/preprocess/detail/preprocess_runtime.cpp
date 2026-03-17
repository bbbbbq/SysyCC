#include "frontend/preprocess/detail/preprocess_runtime.hpp"

namespace sysycc::preprocess::detail {

PreprocessRuntime::PreprocessRuntime() = default;

void PreprocessRuntime::clear() {
    output_lines_.clear();
    file_stack_.clear();
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
