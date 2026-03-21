#pragma once

#include <string>
#include <vector>

#include "common/source_manager.hpp"
#include "common/source_span.hpp"

namespace sysycc::preprocess::detail {

// Tracks physical and logical source locations during preprocessing.
class SourceMapper {
  private:
    struct FileFrame {
        std::string physical_file_path_;
        const SourceFile *physical_file_ = nullptr;
        const SourceFile *logical_file_ = nullptr;
        int line_directive_physical_anchor_ = 1;
        int line_directive_logical_start_ = 1;
    };

    std::vector<FileFrame> file_stack_;
    SourceManager &source_manager_;

  public:
    explicit SourceMapper(SourceManager &source_manager)
        : source_manager_(source_manager) {}

    void clear();

    void push_file(const std::string &file_path);
    void pop_file();

    bool has_current_file() const noexcept;
    bool has_file_in_stack(const std::string &file_path) const noexcept;

    const std::string &get_current_physical_file_path() const noexcept;
    const SourceFile *get_current_physical_file() const noexcept;
    const SourceFile *get_current_logical_file() const noexcept;

    void apply_line_directive(int physical_line, int logical_line,
                              const std::string *logical_file_path);

    SourcePosition get_physical_position(int physical_line,
                                         int column) const noexcept;
    SourceSpan get_physical_span(int line_begin, int col_begin, int line_end,
                                 int col_end) const noexcept;

    SourcePosition get_logical_position(int physical_line,
                                        int column) const noexcept;
    SourceSpan get_logical_span(int line_begin, int col_begin, int line_end,
                                int col_end) const noexcept;

    SourcePosition map_position(int physical_line, int column) const noexcept {
        return get_logical_position(physical_line, column);
    }

    SourceSpan map_span(int line_begin, int col_begin, int line_end,
                        int col_end) const noexcept {
        return get_logical_span(line_begin, col_begin, line_end, col_end);
    }
};

} // namespace sysycc::preprocess::detail
