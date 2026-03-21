#include "frontend/dialects/core/dialect_manager.hpp"

#include <cstddef>
#include <utility>

namespace sysycc {

void DialectManager::append_keyword_registration_errors(
    std::string_view dialect_name, std::size_t previous_conflict_count) {
    const auto &conflicts = lexer_keyword_registry_.get_conflicts();
    for (std::size_t index = previous_conflict_count; index < conflicts.size();
         ++index) {
        registration_errors_.push_back("dialect '" + std::string(dialect_name) +
                                       "' keyword registration conflict: " +
                                       conflicts[index]);
    }
}

void DialectManager::append_preprocess_probe_handler_registration_errors(
    std::string_view dialect_name, std::size_t previous_error_count) {
    const auto &errors =
        preprocess_probe_handler_registry_.get_registration_errors();
    for (std::size_t index = previous_error_count; index < errors.size();
         ++index) {
        registration_errors_.push_back("dialect '" + std::string(dialect_name) +
                                       "' preprocess probe handler conflict: " +
                                       errors[index]);
    }
}

void DialectManager::append_preprocess_directive_handler_registration_errors(
    std::string_view dialect_name, std::size_t previous_error_count) {
    const auto &errors =
        preprocess_directive_handler_registry_.get_registration_errors();
    for (std::size_t index = previous_error_count; index < errors.size();
         ++index) {
        registration_errors_.push_back(
            "dialect '" + std::string(dialect_name) +
            "' preprocess directive handler conflict: " + errors[index]);
    }
}

void DialectManager::append_attribute_handler_registration_errors(
    std::string_view dialect_name, std::size_t previous_error_count) {
    const auto &errors =
        attribute_semantic_handler_registry_.get_registration_errors();
    for (std::size_t index = previous_error_count; index < errors.size();
         ++index) {
        registration_errors_.push_back("dialect '" + std::string(dialect_name) +
                                       "' attribute semantic handler conflict: " +
                                       errors[index]);
    }
}

void DialectManager::append_builtin_type_handler_registration_errors(
    std::string_view dialect_name, std::size_t previous_error_count) {
    const auto &errors =
        builtin_type_semantic_handler_registry_.get_registration_errors();
    for (std::size_t index = previous_error_count; index < errors.size();
         ++index) {
        registration_errors_.push_back(
            "dialect '" + std::string(dialect_name) +
            "' builtin-type semantic handler conflict: " + errors[index]);
    }
}

void DialectManager::append_ir_extension_handler_registration_errors(
    std::string_view dialect_name, std::size_t previous_error_count) {
    const auto &errors =
        ir_extension_lowering_registry_.get_registration_errors();
    for (std::size_t index = previous_error_count; index < errors.size();
         ++index) {
        registration_errors_.push_back("dialect '" + std::string(dialect_name) +
                                       "' ir extension handler conflict: " +
                                       errors[index]);
    }
}

void DialectManager::register_dialect(std::unique_ptr<FrontendDialect> dialect) {
    if (dialect == nullptr) {
        return;
    }
    const std::string dialect_name(dialect->get_name());
    dialect->contribute_preprocess_features(preprocess_feature_registry_);
    const std::size_t previous_probe_handler_error_count =
        preprocess_probe_handler_registry_.get_registration_errors().size();
    dialect->contribute_preprocess_probe_handlers(
        preprocess_probe_handler_registry_);
    append_preprocess_probe_handler_registration_errors(
        dialect_name, previous_probe_handler_error_count);
    const std::size_t previous_directive_handler_error_count =
        preprocess_directive_handler_registry_.get_registration_errors().size();
    dialect->contribute_preprocess_directive_handlers(
        preprocess_directive_handler_registry_);
    append_preprocess_directive_handler_registration_errors(
        dialect_name, previous_directive_handler_error_count);
    const std::size_t previous_conflict_count =
        lexer_keyword_registry_.get_conflicts().size();
    dialect->contribute_lexer_keywords(lexer_keyword_registry_);
    append_keyword_registration_errors(dialect_name, previous_conflict_count);
    dialect->contribute_parser_features(parser_feature_registry_);
    dialect->contribute_ast_features(ast_feature_registry_);
    dialect->contribute_semantic_features(semantic_feature_registry_);
    const std::size_t previous_attribute_handler_error_count =
        attribute_semantic_handler_registry_.get_registration_errors().size();
    dialect->contribute_attribute_semantic_handlers(
        attribute_semantic_handler_registry_);
    append_attribute_handler_registration_errors(
        dialect_name, previous_attribute_handler_error_count);
    const std::size_t previous_builtin_type_handler_error_count =
        builtin_type_semantic_handler_registry_.get_registration_errors().size();
    dialect->contribute_builtin_type_semantic_handlers(
        builtin_type_semantic_handler_registry_);
    append_builtin_type_handler_registration_errors(
        dialect_name, previous_builtin_type_handler_error_count);
    const std::size_t previous_ir_extension_handler_error_count =
        ir_extension_lowering_registry_.get_registration_errors().size();
    dialect->contribute_ir_extension_lowering_handlers(
        ir_extension_lowering_registry_);
    append_ir_extension_handler_registration_errors(
        dialect_name, previous_ir_extension_handler_error_count);
    dialect->contribute_ir_features(ir_feature_registry_);
    dialects_.push_back(std::move(dialect));
}

const std::vector<std::unique_ptr<FrontendDialect>> &
DialectManager::get_dialects() const noexcept {
    return dialects_;
}

std::vector<std::string> DialectManager::get_dialect_names() const {
    std::vector<std::string> names;
    names.reserve(dialects_.size());
    for (const auto &dialect : dialects_) {
        if (dialect != nullptr) {
            names.emplace_back(dialect->get_name());
        }
    }
    return names;
}

const PreprocessFeatureRegistry &
DialectManager::get_preprocess_feature_registry() const noexcept {
    return preprocess_feature_registry_;
}

const PreprocessProbeHandlerRegistry &
DialectManager::get_preprocess_probe_handler_registry() const noexcept {
    return preprocess_probe_handler_registry_;
}

const PreprocessDirectiveHandlerRegistry &
DialectManager::get_preprocess_directive_handler_registry() const noexcept {
    return preprocess_directive_handler_registry_;
}

const LexerKeywordRegistry &DialectManager::get_lexer_keyword_registry() const
    noexcept {
    return lexer_keyword_registry_;
}

const ParserFeatureRegistry &DialectManager::get_parser_feature_registry() const
    noexcept {
    return parser_feature_registry_;
}

const AstFeatureRegistry &DialectManager::get_ast_feature_registry() const
    noexcept {
    return ast_feature_registry_;
}

const SemanticFeatureRegistry &
DialectManager::get_semantic_feature_registry() const noexcept {
    return semantic_feature_registry_;
}

const AttributeSemanticHandlerRegistry &
DialectManager::get_attribute_semantic_handler_registry() const noexcept {
    return attribute_semantic_handler_registry_;
}

const BuiltinTypeSemanticHandlerRegistry &
DialectManager::get_builtin_type_semantic_handler_registry() const noexcept {
    return builtin_type_semantic_handler_registry_;
}

const IrExtensionLoweringRegistry &
DialectManager::get_ir_extension_lowering_registry() const noexcept {
    return ir_extension_lowering_registry_;
}

const IrFeatureRegistry &DialectManager::get_ir_feature_registry() const
    noexcept {
    return ir_feature_registry_;
}

const std::vector<std::string> &DialectManager::get_registration_errors() const
    noexcept {
    return registration_errors_;
}

} // namespace sysycc
