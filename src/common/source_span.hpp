#pragma once

#include <string>

namespace sysycc {

// Stores one source file path tracked by source locations.
class SourceFile {
  private:
    std::string path_;

  public:
    SourceFile() = default;
    explicit SourceFile(std::string path) : path_(std::move(path)) {}

    const std::string &get_path() const noexcept { return path_; }

    bool empty() const noexcept { return path_.empty(); }
};

// Stores one source position as file, line, and column.
class SourcePosition {
  private:
    const SourceFile *file_ = nullptr;
    int line_ = 0;
    int column_ = 0;

  public:
    SourcePosition() = default;
    SourcePosition(const SourceFile *file, int line,
                   int column)  // NOLINT(bugprone-easily-swappable-parameters)
        : file_(file), line_(line), column_(column) {}

    const SourceFile *get_file() const noexcept { return file_; }

    int get_line() const noexcept { return line_; }
    int get_column() const noexcept { return column_; }

    bool empty() const noexcept {
        return file_ == nullptr && line_ == 0 && column_ == 0;
    }
};

// Stores the begin and end position of a source fragment.
class SourceSpan {
  private:
    SourcePosition begin_;
    SourcePosition end_;

  public:
    SourceSpan() = default;

    SourceSpan(SourcePosition begin, SourcePosition end)
        : begin_(begin), end_(end) {}

    const SourcePosition &get_begin() const noexcept { return begin_; }

    const SourcePosition &get_end() const noexcept { return end_; }

    const SourceFile *get_file() const noexcept {
        return begin_.get_file() != nullptr ? begin_.get_file()
                                            : end_.get_file();
    }

    int get_line_begin() const noexcept { return begin_.get_line(); }

    int get_col_begin() const noexcept { return begin_.get_column(); }

    int get_line_end() const noexcept { return end_.get_line(); }

    int get_col_end() const noexcept { return end_.get_column(); }

    void set_begin(SourcePosition begin) noexcept { begin_ = begin; }

    void set_end(SourcePosition end) noexcept { end_ = end; }

    void set_line_begin(int line_begin) noexcept {
        begin_ = SourcePosition(begin_.get_file(), line_begin,
                                begin_.get_column());
    }

    void set_col_begin(int col_begin) noexcept {
        begin_ =
            SourcePosition(begin_.get_file(), begin_.get_line(), col_begin);
    }

    void set_line_end(int line_end) noexcept {
        end_ = SourcePosition(end_.get_file(), line_end, end_.get_column());
    }

    void set_col_end(int col_end) noexcept {
        end_ = SourcePosition(end_.get_file(), end_.get_line(), col_end);
    }

    bool empty() const noexcept {
        return begin_.empty() && end_.empty();
    }
};

} // namespace sysycc
