#pragma once

#include <string>
#include <vector>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Resolves local include paths for preprocessing.
class IncludeResolver {
  public:
    PassResult
    resolve_local_include(const std::string &directive_line,
                          const std::string &including_file_path,
                          const std::vector<std::string> &include_directories,
                          const std::string &include_token,
                          std::string &resolved_file_path) const;
};

} // namespace sysycc::preprocess::detail
