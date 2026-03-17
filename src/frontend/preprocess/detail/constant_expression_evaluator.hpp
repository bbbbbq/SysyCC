#pragma once

#include <string>

#include "compiler/pass/pass.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

// Evaluates simple preprocess constant expressions used by #if and #elif.
class ConstantExpressionEvaluator {
  public:
    PassResult evaluate(const std::string &expression,
                        const MacroTable &macro_table, long long &value) const;
};

} // namespace sysycc::preprocess::detail
