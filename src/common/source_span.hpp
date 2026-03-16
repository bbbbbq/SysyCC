#pragma once

namespace sysycc {

class SourceSpan {
  private:
    int line_begin_ = 0;
    int col_begin_ = 0;
    int line_end_ = 0;
    int col_end_ = 0;

  public:
    SourceSpan() = default;

    SourceSpan(int line_begin, int col_begin, int line_end, int col_end)
        : line_begin_(line_begin),
          col_begin_(col_begin),
          line_end_(line_end),
          col_end_(col_end) {}

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
