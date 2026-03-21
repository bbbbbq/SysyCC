#include "frontend/dialects/gnu/gnu_dialect.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::string_view GnuDialect::get_name() const noexcept { return "gnu-c"; }

void GnuDialect::contribute_preprocess_features(
    PreprocessFeatureRegistry &registry) const {
    registry.add_feature(PreprocessFeature::GnuPredefinedMacros);
    registry.add_feature(PreprocessFeature::NonStandardDirectivePayloads);
}

void GnuDialect::contribute_preprocess_directive_handlers(
    PreprocessDirectiveHandlerRegistry &registry) const {
    registry.add_handler(PreprocessDirectiveHandlerKind::GnuWarningDirective,
                         std::string(get_name()));
    registry.add_handler(PreprocessDirectiveHandlerKind::GnuPragmaOnceDirective,
                         std::string(get_name()));
}

void GnuDialect::contribute_lexer_keywords(
    LexerKeywordRegistry &registry) const {
    registry.add_keyword("__attribute__", TokenKind::KwAttribute);
    registry.add_keyword("__signed", TokenKind::KwSigned);
}

void GnuDialect::contribute_parser_features(
    ParserFeatureRegistry &registry) const {
    registry.add_feature(ParserFeature::GnuAttributeSpecifiers);
}

void GnuDialect::contribute_ast_features(AstFeatureRegistry &registry) const {
    registry.add_feature(AstFeature::GnuAttributeLists);
}

void GnuDialect::contribute_semantic_features(
    SemanticFeatureRegistry &registry) const {
    registry.add_feature(SemanticFeature::FunctionAttributes);
}

void GnuDialect::contribute_attribute_semantic_handlers(
    AttributeSemanticHandlerRegistry &registry) const {
    registry.add_handler(AttributeSemanticHandlerKind::GnuFunctionAttributes,
                         std::string(get_name()));
}

void GnuDialect::contribute_ir_extension_lowering_handlers(
    IrExtensionLoweringRegistry &registry) const {
    registry.add_handler(IrExtensionLoweringHandlerKind::GnuFunctionAttributes,
                         std::string(get_name()));
}

void GnuDialect::contribute_ir_features(IrFeatureRegistry &registry) const {
    registry.add_feature(IrFeature::FunctionAttributes);
}

} // namespace sysycc
