#include "frontend/attribute/attribute_analyzer.hpp"

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

} // namespace

std::vector<SemanticFunctionAttribute>
AttributeAnalyzer::analyze_function_attributes(
    const FunctionDecl *function_decl,
    detail::SemanticContext &semantic_context) const {
    std::vector<SemanticFunctionAttribute> attributes;
    if (function_decl == nullptr) {
        return attributes;
    }

    bool has_always_inline = false;
    for (const ParsedAttribute &attribute :
         function_decl->get_attributes().get_attributes()) {
        if (attribute.get_name() == "__always_inline__") {
            if (!attribute.get_arguments().empty()) {
                add_error(semantic_context,
                          "attribute __always_inline__ does not take arguments",
                          attribute.get_source_span());
                continue;
            }
            if (!has_always_inline) {
                attributes.push_back(SemanticFunctionAttribute::AlwaysInline);
                has_always_inline = true;
            }
            continue;
        }

        add_error(semantic_context,
                  "unsupported attribute: " + attribute.get_name(),
                  attribute.get_source_span());
    }

    return attributes;
}

} // namespace sysycc
