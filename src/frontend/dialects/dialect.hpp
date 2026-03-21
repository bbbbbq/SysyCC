#pragma once

#include <string_view>

#include "frontend/dialects/ast_feature_registry.hpp"
#include "frontend/dialects/attribute_semantic_handler_registry.hpp"
#include "frontend/dialects/builtin_type_semantic_handler_registry.hpp"
#include "frontend/dialects/ir_extension_lowering_registry.hpp"
#include "frontend/dialects/ir_feature_registry.hpp"
#include "frontend/dialects/lexer_keyword_registry.hpp"
#include "frontend/dialects/parser_feature_registry.hpp"
#include "frontend/dialects/preprocess_directive_handler_registry.hpp"
#include "frontend/dialects/preprocess_feature_registry.hpp"
#include "frontend/dialects/preprocess_probe_handler_registry.hpp"
#include "frontend/dialects/semantic_feature_registry.hpp"

namespace sysycc {

class FrontendDialect {
  public:
    virtual ~FrontendDialect() = default;

    virtual std::string_view get_name() const noexcept = 0;

    virtual void contribute_preprocess_features(
        PreprocessFeatureRegistry &registry) const {}

    virtual void contribute_preprocess_probe_handlers(
        PreprocessProbeHandlerRegistry &registry) const {}

    virtual void contribute_preprocess_directive_handlers(
        PreprocessDirectiveHandlerRegistry &registry) const {}

    virtual void contribute_lexer_keywords(
        LexerKeywordRegistry &registry) const {}

    virtual void contribute_parser_features(
        ParserFeatureRegistry &registry) const {}

    virtual void contribute_ast_features(AstFeatureRegistry &registry) const {}

    virtual void contribute_semantic_features(
        SemanticFeatureRegistry &registry) const {}

    virtual void contribute_attribute_semantic_handlers(
        AttributeSemanticHandlerRegistry &registry) const {}

    virtual void contribute_builtin_type_semantic_handlers(
        BuiltinTypeSemanticHandlerRegistry &registry) const {}

    virtual void contribute_ir_extension_lowering_handlers(
        IrExtensionLoweringRegistry &registry) const {}

    virtual void contribute_ir_features(IrFeatureRegistry &registry) const {}
};

} // namespace sysycc
