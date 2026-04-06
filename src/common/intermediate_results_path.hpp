#pragma once

#include <cstdlib>
#include <filesystem>

namespace sysycc {

inline std::filesystem::path get_intermediate_results_dir() {
    if (const char *configured_dir = std::getenv("SYSYCC_INTERMEDIATE_RESULTS_DIR");
        configured_dir != nullptr && configured_dir[0] != '\0') {
        return std::filesystem::path(configured_dir);
    }
    return std::filesystem::path("build/intermediate_results");
}

} // namespace sysycc
