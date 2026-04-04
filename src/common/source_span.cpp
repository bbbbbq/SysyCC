#include "common/source_span.hpp"

#include <fstream>
#include <string>

namespace sysycc {

void SourceFile::ensure_source_text_loaded() const {
    if (source_text_cache_.attempted_load) {
        return;
    }

    source_text_cache_.attempted_load = true;
    if (path_.empty()) {
        return;
    }

    std::ifstream input(path_);
    if (!input.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        source_text_cache_.lines.push_back(line);
    }
}

std::optional<std::string> SourceFile::get_line_text(int line_number) const {
    if (line_number <= 0) {
        return std::nullopt;
    }

    ensure_source_text_loaded();
    const std::size_t line_index = static_cast<std::size_t>(line_number - 1);
    if (line_index >= source_text_cache_.lines.size()) {
        return std::nullopt;
    }

    return source_text_cache_.lines[line_index];
}

} // namespace sysycc
