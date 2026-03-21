#include "frontend/preprocess/detail/preprocess_context.hpp"

#include <utility>

namespace sysycc::preprocess::detail {

PreprocessContext::PreprocessContext(CompilerContext &compiler_context)
    : compiler_context_(compiler_context),
      source_mapper_(compiler_context.get_source_manager()) {}

void PreprocessContext::clear() {
    runtime_.clear();
    macro_table_.clear();
    conditional_stack_.clear();
    source_mapper_.clear();
}

CompilerContext &PreprocessContext::get_compiler_context() noexcept {
    return compiler_context_;
}

const CompilerContext &PreprocessContext::get_compiler_context() const noexcept {
    return compiler_context_;
}

PreprocessRuntime &PreprocessContext::get_runtime() noexcept { return runtime_; }

const PreprocessRuntime &PreprocessContext::get_runtime() const noexcept {
    return runtime_;
}

MacroTable &PreprocessContext::get_macro_table() noexcept { return macro_table_; }

const MacroTable &PreprocessContext::get_macro_table() const noexcept {
    return macro_table_;
}

ConditionalStack &PreprocessContext::get_conditional_stack() noexcept {
    return conditional_stack_;
}

const ConditionalStack &PreprocessContext::get_conditional_stack() const noexcept {
    return conditional_stack_;
}

SourceMapper &PreprocessContext::get_source_mapper() noexcept {
    return source_mapper_;
}

const SourceMapper &PreprocessContext::get_source_mapper() const noexcept {
    return source_mapper_;
}

const std::vector<std::string> &
PreprocessContext::get_include_directories() const noexcept {
    return compiler_context_.get_include_directories();
}

const std::vector<std::string> &
PreprocessContext::get_system_include_directories() const noexcept {
    return compiler_context_.get_system_include_directories();
}

const std::string &PreprocessContext::get_input_file() const noexcept {
    return compiler_context_.get_input_file();
}

void PreprocessContext::set_preprocessed_file_path(std::string file_path) {
    compiler_context_.set_preprocessed_file_path(std::move(file_path));
}

void PreprocessContext::set_preprocessed_line_map(
    SourceLineMap preprocessed_line_map) {
    compiler_context_.set_preprocessed_line_map(
        std::move(preprocessed_line_map));
}

} // namespace sysycc::preprocess::detail
