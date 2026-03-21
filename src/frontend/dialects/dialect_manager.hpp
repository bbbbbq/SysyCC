#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontend/dialects/ast_feature_registry.hpp"
#include "frontend/dialects/attribute_semantic_handler_registry.hpp"
#include "frontend/dialects/builtin_type_semantic_handler_registry.hpp"
#include "frontend/dialects/dialect.hpp"
#include "frontend/dialects/ir_extension_lowering_registry.hpp"
#include "frontend/dialects/ir_feature_registry.hpp"
#include "frontend/dialects/lexer_keyword_registry.hpp"
#include "frontend/dialects/parser_feature_registry.hpp"
#include "frontend/dialects/preprocess_directive_handler_registry.hpp"
#include "frontend/dialects/preprocess_feature_registry.hpp"
#include "frontend/dialects/preprocess_probe_handler_registry.hpp"
#include "frontend/dialects/semantic_feature_registry.hpp"

namespace sysycc {

class DialectManager {
  private:
    std::vector<std::unique_ptr<FrontendDialect>> dialects_;
    PreprocessFeatureRegistry preprocess_feature_registry_;
    PreprocessProbeHandlerRegistry preprocess_probe_handler_registry_;
    PreprocessDirectiveHandlerRegistry preprocess_directive_handler_registry_;
    LexerKeywordRegistry lexer_keyword_registry_;
    ParserFeatureRegistry parser_feature_registry_;
    AstFeatureRegistry ast_feature_registry_;
    SemanticFeatureRegistry semantic_feature_registry_;
    AttributeSemanticHandlerRegistry attribute_semantic_handler_registry_;
    BuiltinTypeSemanticHandlerRegistry builtin_type_semantic_handler_registry_;
    IrExtensionLoweringRegistry ir_extension_lowering_registry_;
    IrFeatureRegistry ir_feature_registry_;
    std::vector<std::string> registration_errors_;

    void append_keyword_registration_errors(std::string_view dialect_name,
                                           std::size_t previous_conflict_count);
    void append_preprocess_probe_handler_registration_errors(
        std::string_view dialect_name, std::size_t previous_error_count);
    void append_preprocess_directive_handler_registration_errors(
        std::string_view dialect_name, std::size_t previous_error_count);
    void append_attribute_handler_registration_errors(
        std::string_view dialect_name, std::size_t previous_error_count);
    void append_builtin_type_handler_registration_errors(
        std::string_view dialect_name, std::size_t previous_error_count);
    void append_ir_extension_handler_registration_errors(
        std::string_view dialect_name, std::size_t previous_error_count);

  public:
    void register_dialect(std::unique_ptr<FrontendDialect> dialect);

    const std::vector<std::unique_ptr<FrontendDialect>> &get_dialects() const
        noexcept;

    std::vector<std::string> get_dialect_names() const;

    const PreprocessFeatureRegistry &get_preprocess_feature_registry() const
        noexcept;

    const PreprocessProbeHandlerRegistry &
    get_preprocess_probe_handler_registry() const noexcept;

    const PreprocessDirectiveHandlerRegistry &
    get_preprocess_directive_handler_registry() const noexcept;

    const LexerKeywordRegistry &get_lexer_keyword_registry() const noexcept;

    const ParserFeatureRegistry &get_parser_feature_registry() const noexcept;

    const AstFeatureRegistry &get_ast_feature_registry() const noexcept;

    const SemanticFeatureRegistry &
    get_semantic_feature_registry() const noexcept;

    const AttributeSemanticHandlerRegistry &
    get_attribute_semantic_handler_registry() const noexcept;

    const BuiltinTypeSemanticHandlerRegistry &
    get_builtin_type_semantic_handler_registry() const noexcept;

    const IrExtensionLoweringRegistry &
    get_ir_extension_lowering_registry() const noexcept;

    const IrFeatureRegistry &get_ir_feature_registry() const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
