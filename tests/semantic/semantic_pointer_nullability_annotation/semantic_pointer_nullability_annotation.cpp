#include <cassert>
#include <memory>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    CompilerContext compiler_context;
    SemanticContext semantic_context(compiler_context,
                                     std::make_unique<SemanticModel>());
    TypeResolver type_resolver;
    ConversionChecker conversion_checker;

    auto nullable_char_ptr_ast = std::make_unique<PointerTypeNode>(
        std::make_unique<BuiltinTypeNode>("char"), SourceSpan{}, false, false,
        false, PointerNullabilityKind::Nullable);
    auto nonnull_char_ptr_ast = std::make_unique<PointerTypeNode>(
        std::make_unique<BuiltinTypeNode>("char"), SourceSpan{}, false, false,
        false, PointerNullabilityKind::Nonnull);
    auto unspecified_char_ptr_ast = std::make_unique<PointerTypeNode>(
        std::make_unique<BuiltinTypeNode>("char"), SourceSpan{}, false, false,
        false, PointerNullabilityKind::NullUnspecified);

    const SemanticType *nullable_char_ptr_type = type_resolver.resolve_type(
        nullable_char_ptr_ast.get(), semantic_context, nullptr);
    const SemanticType *nonnull_char_ptr_type = type_resolver.resolve_type(
        nonnull_char_ptr_ast.get(), semantic_context, nullptr);
    const SemanticType *unspecified_char_ptr_type = type_resolver.resolve_type(
        unspecified_char_ptr_ast.get(), semantic_context, nullptr);

    assert(nullable_char_ptr_type != nullptr);
    assert(nonnull_char_ptr_type != nullptr);
    assert(unspecified_char_ptr_type != nullptr);

    const auto *nullable_pointer_type =
        static_cast<const PointerSemanticType *>(nullable_char_ptr_type);
    const auto *nonnull_pointer_type =
        static_cast<const PointerSemanticType *>(nonnull_char_ptr_type);
    const auto *unspecified_pointer_type =
        static_cast<const PointerSemanticType *>(unspecified_char_ptr_type);

    assert(nullable_pointer_type->get_nullability_kind() ==
           PointerNullabilityKind::Nullable);
    assert(nonnull_pointer_type->get_nullability_kind() ==
           PointerNullabilityKind::Nonnull);
    assert(unspecified_pointer_type->get_nullability_kind() ==
           PointerNullabilityKind::NullUnspecified);

    assert(conversion_checker.is_assignable_type(nullable_char_ptr_type,
                                                 nonnull_char_ptr_type));
    assert(conversion_checker.is_assignable_type(nullable_char_ptr_type,
                                                 unspecified_char_ptr_type));
    assert(conversion_checker.is_assignable_type(nonnull_char_ptr_type,
                                                 nullable_char_ptr_type));
    return 0;
}
