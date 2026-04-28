#include "frontend/attribute/gnu_function_attribute_handler.hpp"

#include <string>
#include <utility>
#include <vector>

#include "frontend/ast/ast_node.hpp"
#include "frontend/attribute/attribute.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/support/semantic_context.hpp"

namespace sysycc {

namespace {

void add_error(detail::SemanticContext &semantic_context, std::string message,
               const SourceSpan &source_span) {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
}

bool is_ignored_system_header_attribute(const ParsedAttribute &attribute,
                                        detail::SemanticContext &semantic_context) {
    return semantic_context.is_system_header_span(attribute.get_source_span());
}

bool is_always_inline_attribute(const std::string &name) {
    return name == "always_inline" || name == "__always_inline" ||
           name == "__always_inline__";
}

bool is_noinline_attribute(const std::string &name) {
    return name == "noinline" || name == "__noinline" ||
           name == "__noinline__";
}

bool is_accepted_noop_function_attribute(const std::string &name) {
    return name == "malloc" || name == "__malloc__" ||
           name == "noreturn" || name == "__noreturn__" ||
           name == "deprecated" || name == "__deprecated__";
}

} // namespace

std::vector<SemanticFunctionAttribute>
GnuFunctionAttributeHandler::analyze_function_attributes(
    const FunctionDecl *function_decl,
    detail::SemanticContext &semantic_context) const {
    std::vector<SemanticFunctionAttribute> attributes;
    if (function_decl == nullptr) {
        return attributes;
    }

    bool has_always_inline = false;
    for (const ParsedAttribute &attribute :
         function_decl->get_attributes().get_attributes()) {
        if (is_always_inline_attribute(attribute.get_name())) {
            if (!attribute.get_arguments().empty()) {
                add_error(semantic_context,
                          "attribute " + attribute.get_name() +
                              " does not take arguments",
                          attribute.get_source_span());
                continue;
            }
            if (!has_always_inline) {
                attributes.push_back(SemanticFunctionAttribute::AlwaysInline);
                has_always_inline = true;
            }
            continue;
        }

        if (is_noinline_attribute(attribute.get_name())) {
            if (!attribute.get_arguments().empty()) {
                add_error(semantic_context,
                          "attribute " + attribute.get_name() +
                              " does not take arguments",
                          attribute.get_source_span());
            }
            continue;
        }

        if (is_accepted_noop_function_attribute(attribute.get_name())) {
            continue;
        }

        if (is_ignored_system_header_attribute(attribute, semantic_context)) {
            continue;
        }

        add_error(semantic_context,
                  "unsupported attribute: " + attribute.get_name(),
                  attribute.get_source_span());
    }

    return attributes;
}

} // namespace sysycc
