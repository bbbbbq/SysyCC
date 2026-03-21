#include "frontend/attribute/attribute_analyzer.hpp"

#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/dialects/registries/attribute_semantic_handler_registry.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
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
    if (function_decl == nullptr) {
        return {};
    }

    if (function_decl->get_attributes().empty()) {
        return {};
    }

    const auto &handler_registry = semantic_context.get_compiler_context()
                                       .get_dialect_manager()
                                       .get_attribute_semantic_handler_registry();
    if (handler_registry.has_handler(
            AttributeSemanticHandlerKind::GnuFunctionAttributes)) {
        return gnu_function_attribute_handler_.analyze_function_attributes(
            function_decl, semantic_context);
    }

    for (const ParsedAttribute &attribute :
         function_decl->get_attributes().get_attributes()) {
        add_error(semantic_context,
                  "no attribute semantic handler registered for: " +
                      attribute.get_name(),
                  attribute.get_source_span());
    }

    return {};
}

} // namespace sysycc
