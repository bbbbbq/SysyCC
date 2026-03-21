#include <cassert>

#include "frontend/dialects/builtin_type_semantic_handler_registry.hpp"
#include "frontend/dialects/semantic_feature_registry.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    BuiltinTypeSemanticHandlerRegistry builtin_type_handlers;
    builtin_type_handlers.add_handler(
        BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes,
        "extended-builtin-types");

    SemanticFeatureRegistry no_features;
    ConversionChecker checker_without_features(&no_features,
                                              &builtin_type_handlers);

    BuiltinSemanticType float16_type("_Float16");
    BuiltinSemanticType char_type("char");
    QualifiedSemanticType const_char_type(true, &char_type);
    PointerSemanticType const_char_ptr_type(&const_char_type);
    PointerSemanticType char_ptr_type(&char_type);

    assert(!checker_without_features.is_arithmetic_type(&float16_type));
    assert(!checker_without_features.is_assignable_type(&const_char_ptr_type,
                                                        &char_ptr_type));

    SemanticFeatureRegistry enabled_features;
    enabled_features.add_feature(SemanticFeature::ExtendedBuiltinTypes);
    enabled_features.add_feature(SemanticFeature::QualifiedPointerConversions);
    ConversionChecker checker_with_features(&enabled_features,
                                           &builtin_type_handlers);

    assert(checker_with_features.is_arithmetic_type(&float16_type));
    assert(checker_with_features.is_assignable_type(&const_char_ptr_type,
                                                    &char_ptr_type));
    return 0;
}
