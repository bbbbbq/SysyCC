#pragma once

#include <string>
#include <vector>

#include "frontend/dialects/core/dialect_manager.hpp"
#include "compiler/pass/pass.hpp"
#include "frontend/preprocess/detail/conditional/builtin_probe_evaluator.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

// Evaluates simple preprocess constant expressions used by #if and #elif.
class ConstantExpressionEvaluator {
  private:
    BuiltinProbeEvaluator builtin_probe_evaluator_;

  public:
    PassResult evaluate(const std::string &expression,
                        const MacroTable &macro_table,
                        const std::string &current_file_path,
                        const std::vector<std::string> &include_directories,
                        const std::vector<std::string> &system_include_directories,
                        const DialectManager &dialect_manager,
                        long long &value) const;
};

} // namespace sysycc::preprocess::detail
