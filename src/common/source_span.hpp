#pragma once

namespace sysycc {

// Stores one source position as line and column.
class SourcePosition {
  private:
    int line_ = 0;
    int column_ = 0;

  public:
    SourcePosition() = default;
    SourcePosition(int line, int column)  // NOLINT(bugprone-easily-swappable-parameters)
        : line_(line), column_(column) {}

    int get_line() const noexcept { return line_; }
    int get_column() const noexcept { return column_; }
};

// Stores the begin and end position of a source fragment.
class SourceSpan {
  private:
    int line_begin_ = 0;
    int col_begin_ = 0;
    int line_end_ = 0;
    int col_end_ = 0;

  public:
    SourceSpan() = default;

    SourceSpan(SourcePosition begin, SourcePosition end)
        : line_begin_(begin.get_line()), col_begin_(begin.get_column()),
          line_end_(end.get_line()), col_end_(end.get_column()) {}

    int get_line_begin() const noexcept { return line_begin_; }

    int get_col_begin() const noexcept { return col_begin_; }

    int get_line_end() const noexcept { return line_end_; }

    int get_col_end() const noexcept { return col_end_; }

    void set_line_begin(int line_begin) noexcept { line_begin_ = line_begin; }

    void set_col_begin(int col_begin) noexcept { col_begin_ = col_begin; }

    void set_line_end(int line_end) noexcept { line_end_ = line_end; }

    void set_col_end(int col_end) noexcept { col_end_ = col_end; }

    bool empty() const noexcept {
        return line_begin_ == 0 && col_begin_ == 0 && line_end_ == 0 &&
               col_end_ == 0;
    }
};

} // namespace sysycc
