#pragma once

#include "common/source_line_map.hpp"
#include "common/source_span.hpp"

namespace sysycc {

// Provides one downstream view over a physical file plus optional logical
// preprocess line mapping.
class SourceMappingView {
  private:
    const SourceFile *physical_file_ = nullptr;
    const SourceLineMap *line_map_ = nullptr;

  public:
    SourceMappingView() = default;

    SourceMappingView(
        const SourceFile *physical_file,
        const SourceLineMap
            *line_map) noexcept // NOLINT(bugprone-easily-swappable-parameters)
        : physical_file_(physical_file), line_map_(line_map) {}

    const SourceFile *get_physical_file() const noexcept {
        return physical_file_;
    }

    const SourceLineMap *get_line_map() const noexcept { return line_map_; }

    bool has_logical_mapping() const noexcept { return line_map_ != nullptr; }

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

} // namespace sysycc
