#pragma once

#include "frontend/dialects/core/dialect.hpp"

namespace sysycc {

class GnuDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override;

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const
        override;

    void contribute_preprocess_features(
        PreprocessFeatureRegistry &registry) const override;

    void contribute_preprocess_directive_handlers(
        PreprocessDirectiveHandlerRegistry &registry) const override;

    void contribute_parser_features(ParserFeatureRegistry &registry) const
        override;

    void contribute_ast_features(AstFeatureRegistry &registry) const override;

    void contribute_semantic_features(
        SemanticFeatureRegistry &registry) const override;

    void contribute_attribute_semantic_handlers(
        AttributeSemanticHandlerRegistry &registry) const override;

    void contribute_ir_extension_lowering_handlers(
        IrExtensionLoweringRegistry &registry) const override;

    void contribute_ir_features(IrFeatureRegistry &registry) const override;
};

} // namespace sysycc
