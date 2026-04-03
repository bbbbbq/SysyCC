#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/lower/legacy/llvm/llvm_ir_backend.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    CompilerContext compiler_context;
    SemanticContext semantic_context(compiler_context,
                                     std::make_unique<SemanticModel>());
    TypeResolver type_resolver;

    auto nullable_pointer_ast = std::make_unique<PointerTypeNode>(
        std::make_unique<BuiltinTypeNode>("char"), SourceSpan{}, false, false,
        false,
        PointerNullabilityKind::Nullable);
    const SemanticType *nullable_pointer_type = type_resolver.resolve_type(
        nullable_pointer_ast.get(), semantic_context, nullptr);
    assert(nullable_pointer_type != nullptr);
    const auto *pointer_type =
        static_cast<const PointerSemanticType *>(nullable_pointer_type);
    assert(pointer_type->get_nullability_kind() ==
           PointerNullabilityKind::Nullable);

    LlvmIrBackend backend;
    backend.begin_module();
    BuiltinSemanticType void_type("void");
    backend.declare_function("pointer_nullability_probe", &void_type,
                             {nullable_pointer_type}, false, false);
    backend.end_module();

    const std::string ir_text = backend.get_output_text();
    assert(ir_text.find("declare void @pointer_nullability_probe(ptr)") !=
           std::string::npos);
    assert(ir_text.find("_Nullable") == std::string::npos);
    return 0;
}
