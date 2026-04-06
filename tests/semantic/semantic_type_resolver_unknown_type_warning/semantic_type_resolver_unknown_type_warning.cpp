#include <cassert>
#include <memory>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    CompilerContext compiler_context;
    SemanticContext semantic_context(compiler_context,
                                     std::make_unique<SemanticModel>());
    TypeResolver type_resolver;

    UnknownTypeNode unknown_type("mystery-type", SourceSpan{});
    const SemanticType *resolved_type =
        type_resolver.resolve_type(&unknown_type, semantic_context, nullptr);

    assert(resolved_type == nullptr);
    const auto &diagnostics = semantic_context.get_semantic_model().get_diagnostics();
    assert(diagnostics.size() == 1);
    assert(diagnostics.front().get_severity() == DiagnosticSeverity::Warning);
    assert(diagnostics.front().get_message() ==
           "semantic skeleton encountered unknown type");
    return 0;
}
