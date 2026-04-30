#include "frontend/dialects/packs/gnu/gnu_dialect.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::string_view GnuDialect::get_name() const noexcept { return "gnu-c"; }

void GnuDialect::contribute_preprocess_features(
    PreprocessFeatureRegistry &registry) const {
    registry.add_feature(PreprocessFeature::ClangBuiltinProbes);
    registry.add_feature(PreprocessFeature::GnuPredefinedMacros);
    registry.add_feature(PreprocessFeature::HasIncludeFamily);
    registry.add_feature(PreprocessFeature::NonStandardDirectivePayloads);
}

void GnuDialect::contribute_preprocess_probe_handlers(
    PreprocessProbeHandlerRegistry &registry) const {
    // Host Clang system headers use these probes even for GNU language modes.
    registry.add_handler(PreprocessProbeHandlerKind::ClangBuiltinProbes,
                         "clang");
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
    registry.add_keyword("__attribute", TokenKind::KwAttribute);
    registry.add_keyword("__attribute__", TokenKind::KwAttribute);
    registry.add_keyword("__asm", TokenKind::KwAsm);
    registry.add_keyword("__asm__", TokenKind::KwAsm);
    registry.add_keyword("__extension__", TokenKind::KwExtension);
    registry.add_keyword("__inline", TokenKind::KwInline);
    registry.add_keyword("__inline__", TokenKind::KwInline);
    registry.add_keyword("__const", TokenKind::KwConst);
    registry.add_keyword("__const__", TokenKind::KwConst);
    registry.add_keyword("__volatile", TokenKind::KwVolatile);
    registry.add_keyword("__volatile__", TokenKind::KwVolatile);
    registry.add_keyword("__restrict", TokenKind::KwRestrict);
    registry.add_keyword("__restrict__", TokenKind::KwRestrict);
    registry.add_keyword("__signed", TokenKind::KwSigned);
    registry.add_keyword("__signed__", TokenKind::KwSigned);
    registry.add_keyword("_Float32", TokenKind::KwFloat);
    registry.add_keyword("_Float32x", TokenKind::KwDouble);
    registry.add_keyword("_Float64", TokenKind::KwDouble);
    registry.add_keyword("_Float64x", TokenKind::KwDouble);
    registry.add_keyword("_Float128", TokenKind::KwDouble);
}

void GnuDialect::contribute_parser_features(
    ParserFeatureRegistry &registry) const {
    registry.add_feature(ParserFeature::GnuAttributeSpecifiers);
    registry.add_feature(ParserFeature::GnuAsmLabels);
}

void GnuDialect::contribute_ast_features(AstFeatureRegistry &registry) const {
    registry.add_feature(AstFeature::GnuAttributeLists);
    registry.add_feature(AstFeature::GnuAsmLabels);
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
