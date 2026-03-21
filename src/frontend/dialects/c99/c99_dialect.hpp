#pragma once

#include "frontend/dialects/dialect.hpp"

namespace sysycc {

class C99Dialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override;

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const
        override;

    void contribute_parser_features(ParserFeatureRegistry &registry) const
        override;

    void contribute_ast_features(AstFeatureRegistry &registry) const override;

    void contribute_semantic_features(
        SemanticFeatureRegistry &registry) const override;
};

} // namespace sysycc
