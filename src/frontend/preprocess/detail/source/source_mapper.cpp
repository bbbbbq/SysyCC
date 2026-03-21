#include "frontend/preprocess/detail/source/source_mapper.hpp"

namespace sysycc::preprocess::detail {

namespace {

constexpr int kNextPhysicalLineOffset = 1;

} // namespace

void SourceMapper::clear() { file_stack_.clear(); }

void SourceMapper::push_file(const std::string &file_path,
                            SourcePosition include_position) {
    FileFrame frame;
    frame.physical_file_path_ = file_path;
    frame.physical_file_ = source_manager_.get_source_file(file_path);
    frame.logical_file_ = frame.physical_file_;
    frame.include_position_ = include_position;
    file_stack_.push_back(frame);
}

void SourceMapper::pop_file() {
    if (!file_stack_.empty()) {
        file_stack_.pop_back();
    }
}

bool SourceMapper::has_current_file() const noexcept {
    return !file_stack_.empty();
}

bool SourceMapper::has_file_in_stack(const std::string &file_path) const noexcept {
    for (const FileFrame &frame : file_stack_) {
        if (frame.physical_file_path_ == file_path) {
            return true;
        }
    }
    return false;
}

const std::string &SourceMapper::get_current_physical_file_path() const noexcept {
    static const std::string kEmpty;
    if (file_stack_.empty()) {
        return kEmpty;
    }

    return file_stack_.back().physical_file_path_;
}

const SourceFile *SourceMapper::get_current_physical_file() const noexcept {
    if (file_stack_.empty()) {
        return nullptr;
    }

    return file_stack_.back().physical_file_;
}

const SourceFile *SourceMapper::get_current_logical_file() const noexcept {
    if (file_stack_.empty()) {
        return nullptr;
    }

    return file_stack_.back().logical_file_;
}

void SourceMapper::apply_line_directive(int physical_line, int logical_line,
                                        const std::string *logical_file_path) {
    if (file_stack_.empty()) {
        return;
    }

    FileFrame &frame = file_stack_.back();
    frame.line_directive_physical_anchor_ =
        physical_line + kNextPhysicalLineOffset;
    frame.line_directive_logical_start_ = logical_line;
    if (logical_file_path != nullptr && !logical_file_path->empty()) {
        frame.logical_file_ = source_manager_.get_source_file(*logical_file_path);
    }
}

SourcePosition SourceMapper::get_physical_position(int physical_line,
                                                   int column) const noexcept {
    if (file_stack_.empty()) {
        return {};
    }

    const FileFrame &frame = file_stack_.back();
    return SourcePosition(frame.physical_file_, physical_line, column);
}

SourceSpan SourceMapper::get_physical_span(int line_begin, int col_begin,
                                           int line_end,
                                           int col_end) const noexcept {
    return SourceSpan(get_physical_position(line_begin, col_begin),
                      get_physical_position(line_end, col_end));
}

SourcePosition SourceMapper::get_logical_position(int physical_line,
                                                  int column) const noexcept {
    if (file_stack_.empty()) {
        return {};
    }

    const FileFrame &frame = file_stack_.back();
    int logical_line = physical_line;
    if (physical_line >= frame.line_directive_physical_anchor_) {
        logical_line = frame.line_directive_logical_start_ +
                       (physical_line - frame.line_directive_physical_anchor_);
    }

    return SourcePosition(frame.logical_file_, logical_line, column);
}

SourceSpan SourceMapper::get_logical_span(int line_begin, int col_begin,
                                          int line_end,
                                          int col_end) const noexcept {
    return SourceSpan(get_logical_position(line_begin, col_begin),
                      get_logical_position(line_end, col_end));
}

std::vector<SourcePosition> SourceMapper::get_include_trace() const {
    std::vector<SourcePosition> include_trace;
    if (file_stack_.size() < 2) {
        return include_trace;
    }

    include_trace.reserve(file_stack_.size() - 1);
    for (auto it = file_stack_.rbegin(); it != file_stack_.rend(); ++it) {
        if (!it->include_position_.empty()) {
            include_trace.push_back(it->include_position_);
        }
    }

    return include_trace;
}

} // namespace sysycc::preprocess::detail
