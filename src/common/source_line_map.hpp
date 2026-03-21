#pragma once

#include <cstddef>
#include <vector>

#include "common/source_span.hpp"

namespace sysycc {

// Stores one logical source position per emitted logical line.
class SourceLineMap {
  private:
    std::vector<SourcePosition> line_positions_;

  public:
    void clear() noexcept { line_positions_.clear(); }

    bool empty() const noexcept { return line_positions_.empty(); }

    std::size_t get_line_count() const noexcept { return line_positions_.size(); }

    void add_line_position(SourcePosition line_position) {
        line_positions_.push_back(line_position);
    }

    const SourcePosition *get_line_position(int line) const noexcept;

    const std::vector<SourcePosition> &get_line_positions() const noexcept {
        return line_positions_;
    }
};

} // namespace sysycc
