#pragma once

#include "frontend/dialects/core/dialect.hpp"

namespace sysycc {

class ClangDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override;

    void contribute_lexer_keywords(
        LexerKeywordRegistry &registry) const override;

    void contribute_preprocess_features(
        PreprocessFeatureRegistry &registry) const override;

    void contribute_preprocess_probe_handlers(
        PreprocessProbeHandlerRegistry &registry) const override;

    void contribute_preprocess_directive_handlers(
        PreprocessDirectiveHandlerRegistry &registry) const override;

    void contribute_semantic_features(
        SemanticFeatureRegistry &registry) const override;
};

} // namespace sysycc
