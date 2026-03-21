#pragma once

#include <cstddef>
#include <string>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Reserves one place for GNU/GCC-specific preprocessor probe handling.
class GnuExtensionProvider {
  public:
    PassResult try_evaluate(const std::string &expression, std::size_t &index,
                            long long &value, bool &handled) const;
};

} // namespace sysycc::preprocess::detail
