#pragma once

#include <string>
#include <vector>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Loads source files as line sequences for the preprocess session.
class FileLoader {
  public:
    PassResult read_lines(const std::string &file_path,
                          std::vector<std::string> &lines) const;
};

} // namespace sysycc::preprocess::detail
