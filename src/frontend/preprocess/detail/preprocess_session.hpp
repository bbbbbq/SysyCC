#pragma once

#include <string>

#include "frontend/preprocess/detail/constant_expression_evaluator.hpp"
#include "frontend/preprocess/detail/directive/directive_executor.hpp"
#include "frontend/preprocess/detail/directive_parser.hpp"
#include "frontend/preprocess/detail/file_loader.hpp"
#include "frontend/preprocess/detail/include_resolver.hpp"
#include "frontend/preprocess/detail/macro_expander.hpp"
#include "frontend/preprocess/detail/preprocess_context.hpp"

namespace sysycc::preprocess::detail {

// Coordinates one complete preprocessing run using focused helper components.
class PreprocessSession {
  private:
    PreprocessContext preprocess_context_;
    DirectiveParser directive_parser_;
    ConstantExpressionEvaluator constant_expression_evaluator_;
    MacroExpander macro_expander_;
    IncludeResolver include_resolver_;
    FileLoader file_loader_;
    DirectiveExecutor directive_executor_;

    PassResult strip_comments_from_line(const std::string &line,
                                        std::string &stripped_line);
    PassResult handle_non_directive_line(const std::string &line,
                                         int line_number);
    PassResult process_line(const std::string &line, int line_number,
                            const std::string &current_file_path);
    PassResult write_preprocessed_file(std::string &output_file_path) const;
    PassResult preprocess_file(const std::string &file_path);

  public:
    explicit PreprocessSession(CompilerContext &context);
    PassResult Run();
};

} // namespace sysycc::preprocess::detail
