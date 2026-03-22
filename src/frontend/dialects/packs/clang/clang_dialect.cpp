#include "frontend/dialects/packs/clang/clang_dialect.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::string_view ClangDialect::get_name() const noexcept { return "clang"; }

void ClangDialect::contribute_lexer_keywords(
    LexerKeywordRegistry &registry) const {
    registry.add_keyword("_Nullable", TokenKind::KwNullability);
    registry.add_keyword("_Nonnull", TokenKind::KwNullability);
    registry.add_keyword("_Null_unspecified", TokenKind::KwNullability);
}

void ClangDialect::contribute_preprocess_features(
    PreprocessFeatureRegistry &registry) const {
    registry.add_feature(PreprocessFeature::ClangBuiltinProbes);
    registry.add_feature(PreprocessFeature::HasIncludeFamily);
}

void ClangDialect::contribute_preprocess_probe_handlers(
    PreprocessProbeHandlerRegistry &registry) const {
    registry.add_handler(PreprocessProbeHandlerKind::ClangBuiltinProbes,
                         std::string(get_name()));
}

void ClangDialect::contribute_preprocess_directive_handlers(
    PreprocessDirectiveHandlerRegistry &registry) const {
    registry.add_handler(PreprocessDirectiveHandlerKind::ClangWarningDirective,
                         std::string(get_name()));
    registry.add_handler(
        PreprocessDirectiveHandlerKind::ClangPragmaOnceDirective,
        std::string(get_name()));
}

void ClangDialect::contribute_semantic_features(
    SemanticFeatureRegistry &registry) const {
    registry.add_feature(SemanticFeature::FunctionAttributes);
}

} // namespace sysycc
