#include "common/source_line_map.hpp"

namespace sysycc {

const SourcePosition *SourceLineMap::get_line_position(int line) const noexcept {
    if (line <= 0 || line > static_cast<int>(line_positions_.size())) {
        return nullptr;
    }

    return &line_positions_[static_cast<std::size_t>(line - 1)];
}

} // namespace sysycc
