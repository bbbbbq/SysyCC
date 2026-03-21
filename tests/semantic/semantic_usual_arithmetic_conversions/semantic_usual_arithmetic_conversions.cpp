#include <cassert>
#include <string>

#include "frontend/dialects/builtin_type_semantic_handler_registry.hpp"
#include "frontend/dialects/semantic_feature_registry.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

using namespace sysycc;
using namespace sysycc::detail;

namespace {

const BuiltinSemanticType *expect_builtin_type(const SemanticType *type,
                                               const std::string &name) {
    assert(type != nullptr);
    assert(type->get_kind() == SemanticTypeKind::Builtin);
    const auto *builtin_type = static_cast<const BuiltinSemanticType *>(type);
    assert(builtin_type->get_name() == name);
    return builtin_type;
}

} // namespace

int main() {
    SemanticModel semantic_model;
    IntegerConversionService conversion_service;

    BuiltinSemanticType char_type("char");
    BuiltinSemanticType signed_char_type("signed char");
    BuiltinSemanticType unsigned_char_type("unsigned char");
    BuiltinSemanticType short_type("short");
    BuiltinSemanticType unsigned_short_type("unsigned short");
    BuiltinSemanticType int_type("int");
    BuiltinSemanticType unsigned_int_type("unsigned int");
    BuiltinSemanticType ptrdiff_type("ptrdiff_t");
    BuiltinSemanticType long_type("long int");
    BuiltinSemanticType long_long_type("long long int");
    BuiltinSemanticType unsigned_long_long_type("unsigned long long");
    BuiltinSemanticType float_type("float");
    BuiltinSemanticType double_type("double");
    BuiltinSemanticType long_double_type("long double");
    BuiltinSemanticType float16_type("_Float16");

    expect_builtin_type(
        conversion_service.get_integer_promotion_type(&char_type, semantic_model),
        "int");
    expect_builtin_type(conversion_service.get_integer_promotion_type(
                            &signed_char_type, semantic_model),
                        "int");
    expect_builtin_type(conversion_service.get_integer_promotion_type(
                            &unsigned_char_type, semantic_model),
                        "int");
    expect_builtin_type(
        conversion_service.get_integer_promotion_type(&short_type, semantic_model),
        "int");
    expect_builtin_type(conversion_service.get_integer_promotion_type(
                            &ptrdiff_type, semantic_model),
                        "ptrdiff_t");

    expect_builtin_type(conversion_service.get_common_integer_type(
                            &unsigned_short_type, &short_type, semantic_model),
                        "int");
    expect_builtin_type(conversion_service.get_common_integer_type(
                            &unsigned_int_type, &long_type, semantic_model),
                        "long int");
    expect_builtin_type(conversion_service.get_common_integer_type(
                            &long_long_type, &unsigned_int_type, semantic_model),
                        "long long int");
    expect_builtin_type(conversion_service.get_common_integer_type(
                            &long_long_type, &unsigned_long_long_type,
                            semantic_model),
                        "unsigned long long");
    expect_builtin_type(conversion_service.get_common_integer_type(
                            &char_type, &unsigned_int_type, semantic_model),
                        "unsigned int");
    expect_builtin_type(conversion_service.get_common_integer_type(
                            &ptrdiff_type, &unsigned_int_type, semantic_model),
                        "ptrdiff_t");

    expect_builtin_type(conversion_service.get_usual_arithmetic_conversion_type(
                            &float16_type, &float_type, semantic_model),
                        "float");
    expect_builtin_type(conversion_service.get_usual_arithmetic_conversion_type(
                            &float16_type, &double_type, semantic_model),
                        "double");
    expect_builtin_type(conversion_service.get_usual_arithmetic_conversion_type(
                            &double_type, &long_double_type, semantic_model),
                        "long double");
    expect_builtin_type(conversion_service.get_usual_arithmetic_conversion_type(
                            &unsigned_int_type, &long_type, semantic_model),
                        "long int");
    expect_builtin_type(conversion_service.get_usual_arithmetic_conversion_type(
                            &ptrdiff_type, &unsigned_int_type, semantic_model),
                        "ptrdiff_t");

    assert(conversion_service.get_integer_conversion_plan(&unsigned_short_type,
                                                         &int_type)
               .get_kind() == IntegerConversionKind::ZeroExtend);
    assert(conversion_service.get_integer_conversion_plan(&int_type,
                                                         &unsigned_long_long_type)
               .get_kind() == IntegerConversionKind::SignExtend);
    assert(conversion_service.get_integer_conversion_plan(
               &unsigned_long_long_type, &int_type)
               .get_kind() == IntegerConversionKind::Truncate);

    BuiltinTypeSemanticHandlerRegistry builtin_handlers;
    builtin_handlers.add_handler(
        BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes,
        "extended-builtin-types");

    SemanticFeatureRegistry semantic_features;
    semantic_features.add_feature(SemanticFeature::ExtendedBuiltinTypes);

    ConversionChecker checker(&semantic_features, &builtin_handlers);
    expect_builtin_type(checker.get_usual_arithmetic_conversion_type(
                            &float16_type, &float_type, semantic_model),
                        "float");
    expect_builtin_type(checker.get_usual_arithmetic_conversion_type(
                            &float16_type, &double_type, semantic_model),
                        "double");
    expect_builtin_type(checker.get_usual_arithmetic_conversion_type(
                            &unsigned_int_type, &long_type, semantic_model),
                        "long int");

    return 0;
}
