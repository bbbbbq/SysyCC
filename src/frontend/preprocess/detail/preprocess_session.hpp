#pragma once

#include <string>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/preprocess/detail/conditional_stack.hpp"
#include "frontend/preprocess/detail/constant_expression_evaluator.hpp"
#include "frontend/preprocess/detail/directive_parser.hpp"
#include "frontend/preprocess/detail/file_loader.hpp"
#include "frontend/preprocess/detail/include_resolver.hpp"
#include "frontend/preprocess/detail/macro_expander.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"
#include "frontend/preprocess/detail/preprocess_runtime.hpp"

namespace sysycc::preprocess::detail {

// Coordinates one complete preprocessing run using focused helper components.
class PreprocessSession {
  private:
    CompilerContext &context_;
    PreprocessRuntime runtime_;
    DirectiveParser directive_parser_;
    ConstantExpressionEvaluator constant_expression_evaluator_;
    MacroTable macro_table_;
    MacroExpander macro_expander_;
    ConditionalStack conditional_stack_;
    IncludeResolver include_resolver_;
    FileLoader file_loader_;

    PassResult evaluate_if_condition(const Directive &directive,
                                     bool &condition) const;
    PassResult strip_comments_from_line(const std::string &line,
                                        std::string &stripped_line);
    PassResult handle_non_directive_line(const std::string &line);
    PassResult handle_conditional_directive(const Directive &directive);
    PassResult handle_include_directive(const std::string &line,
                                        const Directive &directive,
                                        const std::string &current_file_path);
    PassResult handle_macro_directive(const std::string &line, int line_number,
                                      const Directive &directive);
    PassResult process_line(const std::string &line, int line_number,
                            const std::string &current_file_path);
    PassResult write_preprocessed_file(std::string &output_file_path) const;
    PassResult preprocess_file(const std::string &file_path);

  public:
    explicit PreprocessSession(CompilerContext &context);
    PassResult Run();
};

} // namespace sysycc::preprocess::detail
