#include "common/source_mapping_view.hpp"

namespace sysycc {

SourcePosition SourceMappingView::get_physical_position(
    int physical_line, int column) const noexcept {
    return SourcePosition(physical_file_, physical_line, column);
}

SourceSpan SourceMappingView::get_physical_span(int line_begin, int col_begin,
                                                int line_end,
                                                int col_end) const noexcept {
    return SourceSpan(get_physical_position(line_begin, col_begin),
                      get_physical_position(line_end, col_end));
}

SourcePosition SourceMappingView::get_logical_position(
    int physical_line, int column) const noexcept {
    if (line_map_ == nullptr) {
        return get_physical_position(physical_line, column);
    }

    const SourcePosition *line_position = line_map_->get_line_position(
        physical_line);
    if (line_position == nullptr) {
        return get_physical_position(physical_line, column);
    }

    const SourceFile *file =
        line_position->get_file() != nullptr ? line_position->get_file()
                                             : physical_file_;
    const int line = line_position->get_line() > 0 ? line_position->get_line()
                                                   : physical_line;
    return SourcePosition(file, line, column);
}

SourceSpan SourceMappingView::get_logical_span(int line_begin, int col_begin,
                                               int line_end,
                                               int col_end) const noexcept {
    return SourceSpan(get_logical_position(line_begin, col_begin),
                      get_logical_position(line_end, col_end));
}

} // namespace sysycc
