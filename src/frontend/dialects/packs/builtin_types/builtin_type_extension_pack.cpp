#include "frontend/dialects/packs/builtin_types/builtin_type_extension_pack.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::string_view BuiltinTypeExtensionPack::get_name() const noexcept {
    return "extended-builtin-types";
}

void BuiltinTypeExtensionPack::contribute_lexer_keywords(
    LexerKeywordRegistry &registry) const {
    registry.add_keyword("_Float16", TokenKind::KwFloat16);
}

void BuiltinTypeExtensionPack::contribute_parser_features(
    ParserFeatureRegistry &registry) const {
    registry.add_feature(ParserFeature::ExtendedBuiltinTypeSpecifiers);
}

void BuiltinTypeExtensionPack::contribute_ast_features(
    AstFeatureRegistry &registry) const {
    registry.add_feature(AstFeature::ExtendedBuiltinTypes);
}

void BuiltinTypeExtensionPack::contribute_semantic_features(
    SemanticFeatureRegistry &registry) const {
    registry.add_feature(SemanticFeature::ExtendedBuiltinTypes);
}

void BuiltinTypeExtensionPack::contribute_builtin_type_semantic_handlers(
    BuiltinTypeSemanticHandlerRegistry &registry) const {
    registry.add_handler(
        BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes,
        std::string(get_name()));
}

} // namespace sysycc
