#pragma once

#include "frontend/dialects/core/dialect.hpp"

namespace sysycc {

class BuiltinTypeExtensionPack : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override;

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const
        override;

    void contribute_parser_features(ParserFeatureRegistry &registry) const
        override;

    void contribute_ast_features(AstFeatureRegistry &registry) const override;

    void contribute_semantic_features(
        SemanticFeatureRegistry &registry) const override;

    void contribute_builtin_type_semantic_handlers(
        BuiltinTypeSemanticHandlerRegistry &registry) const override;
};

} // namespace sysycc
