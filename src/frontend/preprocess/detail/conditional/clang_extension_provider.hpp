#pragma once

#include <cstddef>
#include <string>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Handles clang- and Apple-style non-standard preprocessor builtin probes.
class ClangExtensionProvider {
  public:
    PassResult try_evaluate(const std::string &expression, std::size_t &index,
                            long long &value, bool &handled) const;
};

} // namespace sysycc::preprocess::detail
