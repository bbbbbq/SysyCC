#pragma once

#include <string>
#include <vector>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Resolves quoted and system include paths for preprocessing.
class IncludeResolver {
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    PassResult
    resolve_include(
        const std::string &directive_line,
        const std::string &including_file_path,
        const std::vector<std::string> &include_directories,
        const std::vector<std::string> &system_include_directories,
        const std::string &include_token,
        std::string &resolved_file_path) const;
};

} // namespace sysycc::preprocess::detail
