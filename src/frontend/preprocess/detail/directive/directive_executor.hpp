#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "frontend/preprocess/detail/constant_expression_evaluator.hpp"
#include "frontend/preprocess/detail/directive_parser.hpp"
#include "frontend/preprocess/detail/include_resolver.hpp"
#include "frontend/preprocess/detail/macro_expander.hpp"
#include "frontend/preprocess/detail/preprocess_context.hpp"

namespace sysycc::preprocess::detail {

// Executes parsed preprocess directives against the active session state.
class DirectiveExecutor {
  private:
    PreprocessContext &preprocess_context_;
    ConstantExpressionEvaluator &constant_expression_evaluator_;
    MacroExpander &macro_expander_;
    IncludeResolver &include_resolver_;

    bool has_warning_directive_handler() const;
    bool has_pragma_once_handler() const;
    PassResult evaluate_if_condition(const Directive &directive,
                                     bool &condition) const;
    PassResult handle_error_directive(const Directive &directive) const;
    PassResult handle_warning_directive(const Directive &directive,
                                        int line_number) const;
    PassResult handle_pragma_directive(const Directive &directive);
    PassResult handle_line_directive(const Directive &directive,
                                     int line_number) const;
    PassResult handle_conditional_directive(const Directive &directive);
    PassResult handle_include_directive(
        const std::string &line, int line_number, const Directive &directive,
        const std::string &current_file_path,
        const std::function<PassResult(const std::string &, SourcePosition)>
            &preprocess_file_callback) const;
    bool is_system_header_path(const std::string &file_path) const;
    bool should_allow_macro_redefinition(const MacroDefinition &definition) const;
    PassResult handle_macro_directive(const std::string &line, int line_number,
                                      const Directive &directive);

  public:
    DirectiveExecutor(PreprocessContext &preprocess_context,
                      ConstantExpressionEvaluator &constant_expression_evaluator,
                      MacroExpander &macro_expander,
                      IncludeResolver &include_resolver);

    PassResult execute(const std::string &line, int line_number,
                       const Directive &directive,
                       const std::string &current_file_path,
                       const std::function<PassResult(const std::string &,
                                                      SourcePosition)>
                           &preprocess_file_callback);
};

} // namespace sysycc::preprocess::detail
