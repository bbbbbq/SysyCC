#pragma once

#include <string>
#include <vector>

#include "common/source_line_map.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/preprocess/detail/conditional_stack.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"
#include "frontend/preprocess/detail/preprocess_runtime.hpp"
#include "frontend/preprocess/detail/source/source_mapper.hpp"

namespace sysycc::preprocess::detail {

// Collects shared mutable preprocessing state for one preprocessing run.
class PreprocessContext {
  private:
    CompilerContext &compiler_context_;
    PreprocessRuntime runtime_;
    MacroTable macro_table_;
    ConditionalStack conditional_stack_;
    SourceMapper source_mapper_;

  public:
    explicit PreprocessContext(CompilerContext &compiler_context);

    void clear();
    void initialize_predefined_macros();

    CompilerContext &get_compiler_context() noexcept;
    const CompilerContext &get_compiler_context() const noexcept;

    PreprocessRuntime &get_runtime() noexcept;
    const PreprocessRuntime &get_runtime() const noexcept;

    MacroTable &get_macro_table() noexcept;
    const MacroTable &get_macro_table() const noexcept;

    ConditionalStack &get_conditional_stack() noexcept;
    const ConditionalStack &get_conditional_stack() const noexcept;

    SourceMapper &get_source_mapper() noexcept;
    const SourceMapper &get_source_mapper() const noexcept;

    const std::vector<std::string> &get_include_directories() const noexcept;
    const std::vector<std::string> &
    get_system_include_directories() const noexcept;
    const std::vector<CommandLineMacroOption> &
    get_command_line_macro_options() const noexcept;
    const std::vector<std::string> &get_forced_include_files() const noexcept;

    const std::string &get_input_file() const noexcept;
    void set_preprocessed_file_path(std::string file_path);
    void set_preprocessed_line_map(SourceLineMap preprocessed_line_map);
};

} // namespace sysycc::preprocess::detail
