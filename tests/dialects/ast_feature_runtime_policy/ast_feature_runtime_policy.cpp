#include <cassert>
#include <memory>

#include "frontend/ast/detail/ast_feature_validator.hpp"

using namespace sysycc;
using namespace sysycc::detail;

int main() {
    AstFeatureValidator validator;
    AstFeatureErrorInfo error_info;
    AstFeatureRegistry feature_registry;

    auto translation_unit = std::make_unique<TranslationUnit>();
    translation_unit->add_top_level_decl(std::make_unique<FunctionDecl>(
        "attr_func", std::make_unique<BuiltinTypeNode>("int"),
        std::vector<std::unique_ptr<Decl>>(),
        ParsedAttributeList(
            AttributeAttachmentSite::DeclSpecifier,
            std::vector<ParsedAttribute>{ParsedAttribute(
                AttributeSyntaxKind::GNU, "__always_inline__",
                std::vector<ParsedAttributeArgument>(), SourceSpan())},
            SourceSpan()),
        nullptr));
    assert(!validator.validate(translation_unit.get(), feature_registry,
                               error_info));
    feature_registry.add_feature(AstFeature::GnuAttributeLists);
    assert(validator.validate(translation_unit.get(), feature_registry,
                              error_info));

    auto qualified_type = std::make_unique<QualifiedTypeNode>(
        true, std::make_unique<BuiltinTypeNode>("char"));
    feature_registry = AstFeatureRegistry();
    assert(!validator.validate(qualified_type.get(), feature_registry, error_info));
    feature_registry.add_feature(AstFeature::QualifiedTypeNodes);
    assert(validator.validate(qualified_type.get(), feature_registry, error_info));

    auto union_type = std::make_unique<UnionTypeNode>("U");
    feature_registry = AstFeatureRegistry();
    assert(!validator.validate(union_type.get(), feature_registry, error_info));
    feature_registry.add_feature(AstFeature::UnionTypeNodes);
    assert(validator.validate(union_type.get(), feature_registry, error_info));

    auto float16_type = std::make_unique<BuiltinTypeNode>("_Float16");
    feature_registry = AstFeatureRegistry();
    assert(!validator.validate(float16_type.get(), feature_registry, error_info));
    feature_registry.add_feature(AstFeature::ExtendedBuiltinTypes);
    assert(validator.validate(float16_type.get(), feature_registry, error_info));

    return 0;
}
