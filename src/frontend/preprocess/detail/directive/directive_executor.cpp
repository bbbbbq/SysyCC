#include "frontend/preprocess/detail/directive/directive_executor.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace sysycc::preprocess::detail {

DirectiveExecutor::DirectiveExecutor(
    PreprocessContext &preprocess_context,
    ConstantExpressionEvaluator &constant_expression_evaluator,
    MacroExpander &macro_expander, IncludeResolver &include_resolver)
    : preprocess_context_(preprocess_context),
      constant_expression_evaluator_(constant_expression_evaluator),
      macro_expander_(macro_expander),
      include_resolver_(include_resolver) {}

PassResult DirectiveExecutor::evaluate_if_condition(const Directive &directive,
                                                    bool &condition) const {
    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure("invalid " + directive.get_keyword() +
                                   " directive: missing condition");
    }

    long long value = 0;
    PassResult evaluate_result = constant_expression_evaluator_.evaluate(
        arguments[0], preprocess_context_.get_macro_table(),
        preprocess_context_.get_source_mapper().get_current_physical_file_path(),
        preprocess_context_.get_include_directories(),
        preprocess_context_.get_system_include_directories(), value);
    if (!evaluate_result.ok) {
        return evaluate_result;
    }

    condition = value != 0;
    return PassResult::Success();
}

PassResult
DirectiveExecutor::handle_error_directive(const Directive &directive) const {
    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure("#error directive triggered");
    }

    return PassResult::Failure("#error: " + arguments[0]);
}

PassResult DirectiveExecutor::handle_warning_directive(
    const Directive &directive, int line_number) const {
    const std::vector<std::string> &arguments = directive.get_arguments();
    std::string message = "#warning directive triggered";
    if (!arguments.empty()) {
        message = "#warning: " + arguments[0];
    }

    preprocess_context_.get_compiler_context().get_diagnostic_engine().add_warning(
        DiagnosticStage::Preprocess, message,
        preprocess_context_.get_source_mapper().get_logical_span(line_number, 1,
                                                                 line_number, 1));
    return PassResult::Success();
}

PassResult DirectiveExecutor::handle_pragma_directive(const Directive &directive) {
    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Success();
    }

    if (arguments[0] == "once") {
        preprocess_context_.get_runtime().mark_pragma_once_file(
            preprocess_context_.get_source_mapper().get_current_physical_file_path());
    }

    return PassResult::Success();
}

PassResult DirectiveExecutor::handle_line_directive(
    const Directive &directive, int line_number) const {
    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure("invalid #line directive: missing line number");
    }

    std::istringstream payload(arguments[0]);
    std::string line_number_text;
    payload >> line_number_text;
    if (line_number_text.empty()) {
        return PassResult::Failure("invalid #line directive: missing line number");
    }

    for (char ch : line_number_text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return PassResult::Failure(
                "invalid #line directive: invalid line number");
        }
    }

    std::string logical_file_path;
    std::string quoted_file_path;
    payload >> quoted_file_path;
    if (!quoted_file_path.empty()) {
        if (quoted_file_path.size() < 2 || quoted_file_path.front() != '"' ||
            quoted_file_path.back() != '"') {
            return PassResult::Failure(
                "invalid #line directive: invalid file name");
        }
        logical_file_path =
            quoted_file_path.substr(1, quoted_file_path.size() - 2);
    }

    preprocess_context_.get_source_mapper().apply_line_directive(
        line_number,
        std::stoi(line_number_text),
        logical_file_path.empty() ? nullptr : &logical_file_path);

    return PassResult::Success();
}

PassResult DirectiveExecutor::handle_conditional_directive(
    const Directive &directive) {
    const std::vector<std::string> &arguments = directive.get_arguments();
    bool condition = false;

    switch (directive.get_kind()) {
    case DirectiveKind::Ifdef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition = preprocess_context_.get_macro_table().has_macro(arguments[0]);
        return preprocess_context_.get_conditional_stack().push_if(condition);
    case DirectiveKind::Ifndef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition =
            !preprocess_context_.get_macro_table().has_macro(arguments[0]);
        return preprocess_context_.get_conditional_stack().push_if(condition);
    case DirectiveKind::If: {
        PassResult condition_result =
            evaluate_if_condition(directive, condition);
        if (!condition_result.ok) {
            return condition_result;
        }
        return preprocess_context_.get_conditional_stack().push_if(condition);
    }
    case DirectiveKind::Elifdef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition = preprocess_context_.get_macro_table().has_macro(arguments[0]);
        return preprocess_context_.get_conditional_stack().handle_elif(condition);
    case DirectiveKind::Elifndef:
        if (arguments.empty()) {
            return PassResult::Failure("invalid " + directive.get_keyword() +
                                       " directive: missing condition");
        }
        condition =
            !preprocess_context_.get_macro_table().has_macro(arguments[0]);
        return preprocess_context_.get_conditional_stack().handle_elif(condition);
    case DirectiveKind::Elif: {
        PassResult condition_result =
            evaluate_if_condition(directive, condition);
        if (!condition_result.ok) {
            return condition_result;
        }
        return preprocess_context_.get_conditional_stack().handle_elif(condition);
    }
    case DirectiveKind::Else:
        return preprocess_context_.get_conditional_stack().handle_else();
    case DirectiveKind::Endif:
        return preprocess_context_.get_conditional_stack().handle_endif();
    default:
        return PassResult::Failure("unexpected conditional directive kind");
    }
}

PassResult DirectiveExecutor::handle_include_directive(
    const std::string &line, int line_number, const Directive &directive,
    const std::string &current_file_path,
    const std::function<PassResult(const std::string &, SourcePosition)>
        &preprocess_file_callback) const {
    if (directive.get_kind() != DirectiveKind::Include &&
        directive.get_kind() != DirectiveKind::IncludeNext) {
        return PassResult::Failure("unexpected include directive kind");
    }

    const std::vector<std::string> &arguments = directive.get_arguments();
    if (arguments.empty()) {
        return PassResult::Failure(
            "invalid #include directive: missing include path");
    }

    const std::string expanded_include_token =
        macro_expander_.expand_line(arguments[0],
                                    preprocess_context_.get_macro_table());

    std::string resolved_file_path;
    PassResult resolve_result = include_resolver_.resolve_include(
        line, current_file_path, preprocess_context_.get_include_directories(),
        preprocess_context_.get_system_include_directories(),
        directive.get_kind() == DirectiveKind::IncludeNext,
        expanded_include_token, resolved_file_path);
    if (!resolve_result.ok) {
        return resolve_result;
    }

    return preprocess_file_callback(
        resolved_file_path,
        preprocess_context_.get_source_mapper().get_logical_position(
            line_number, 1));
}

PassResult DirectiveExecutor::handle_macro_directive(
    const std::string &line, int line_number, const Directive &directive) {
    const std::vector<std::string> &arguments = directive.get_arguments();

    if (directive.get_kind() == DirectiveKind::Define) {
        if (arguments.empty()) {
            return PassResult::Failure(
                "invalid #define directive: missing macro name");
        }

        std::string replacement;
        if (arguments.size() > 1) {
            replacement = arguments[1];
        }

        return preprocess_context_.get_macro_table().define_macro(MacroDefinition(
            arguments[0], replacement, directive.get_is_function_like_macro(),
            directive.get_is_variadic_macro(),
            directive.get_macro_parameters(),
            preprocess_context_.get_source_mapper().get_logical_span(
                line_number, 1, line_number, static_cast<int>(line.size()))));
    }

    if (directive.get_kind() == DirectiveKind::Undef) {
        if (arguments.empty()) {
            return PassResult::Failure(
                "invalid #undef directive: missing macro name");
        }

        preprocess_context_.get_macro_table().undefine_macro(arguments[0]);
        return PassResult::Success();
    }

    return PassResult::Failure("unexpected macro directive kind");
}

PassResult DirectiveExecutor::execute(
    const std::string &line, int line_number, const Directive &directive,
    const std::string &current_file_path,
    const std::function<PassResult(const std::string &, SourcePosition)>
        &preprocess_file_callback) {
    switch (directive.get_kind()) {
    case DirectiveKind::Ifdef:
    case DirectiveKind::Ifndef:
    case DirectiveKind::If:
    case DirectiveKind::Elif:
    case DirectiveKind::Elifdef:
    case DirectiveKind::Elifndef:
    case DirectiveKind::Else:
    case DirectiveKind::Endif:
        return handle_conditional_directive(directive);
    default:
        break;
    }

    if (!preprocess_context_.get_conditional_stack().is_in_active_region()) {
        return PassResult::Success();
    }

    switch (directive.get_kind()) {
    case DirectiveKind::Include:
    case DirectiveKind::IncludeNext:
        return handle_include_directive(line, line_number, directive,
                                        current_file_path,
                                        preprocess_file_callback);
    case DirectiveKind::Error:
        return handle_error_directive(directive);
    case DirectiveKind::Warning:
        return handle_warning_directive(directive, line_number);
    case DirectiveKind::Pragma:
        return handle_pragma_directive(directive);
    case DirectiveKind::Line:
        return handle_line_directive(directive, line_number);
    case DirectiveKind::Define:
    case DirectiveKind::Undef:
        return handle_macro_directive(line, line_number, directive);
    default:
        return PassResult::Failure("unsupported preprocess directive: " +
                                   directive.get_keyword());
    }
}

} // namespace sysycc::preprocess::detail
