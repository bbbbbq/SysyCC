#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "frontend/dialects/core/dialect_manager.hpp"
#include "compiler/pass/pass.hpp"
#include "frontend/preprocess/detail/conditional/nonstandard_extension_manager.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

// Evaluates builtin preprocessor probes used inside #if and #elif
// expressions.
class BuiltinProbeEvaluator {
  private:
    class IncludeResolver *include_resolver_ = nullptr;
    NonStandardExtensionManager non_standard_extension_manager_;

  public:
    BuiltinProbeEvaluator();
    ~BuiltinProbeEvaluator();

    BuiltinProbeEvaluator(const BuiltinProbeEvaluator &) = delete;
    BuiltinProbeEvaluator &
    operator=(const BuiltinProbeEvaluator &) = delete;

    PassResult try_evaluate(const std::string &expression, std::size_t &index,
                            const MacroTable &macro_table,
                            const std::string &current_file_path,
                            const std::vector<std::string> &include_directories,
                            const std::vector<std::string>
                                &system_include_directories,
                            const DialectManager &dialect_manager,
                            long long &value, bool &handled) const;
};

} // namespace sysycc::preprocess::detail
