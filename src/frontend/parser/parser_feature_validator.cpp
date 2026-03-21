#include "frontend/parser/parser_feature_validator.hpp"

#include <string>

#include "frontend/ast/detail/parse_tree_matcher.hpp"

namespace sysycc {

namespace {

bool contains_label_prefix(const ParseTreeNode *node, const char *label_prefix) {
    if (node == nullptr) {
        return false;
    }
    if (detail::ParseTreeMatcher::label_starts_with(node, label_prefix)) {
        return true;
    }
    for (const auto &child : node->children) {
        if (contains_label_prefix(child.get(), label_prefix)) {
            return true;
        }
    }
    return false;
}

bool validate_node(const ParseTreeNode *node,
                   const ParserFeatureRegistry &feature_registry,
                   ParserErrorInfo &error_info) {
    if (node == nullptr) {
        return true;
    }

    if (detail::ParseTreeMatcher::label_equals(node, "func_decl") &&
        !feature_registry.has_feature(
            ParserFeature::FunctionPrototypeDeclarations)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: function prototype declarations",
            "prototype declaration", node->source_span);
        return false;
    }

    if (detail::ParseTreeMatcher::label_equals(node, "union_decl") &&
        !feature_registry.has_feature(ParserFeature::UnionDeclarations)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: union declarations",
            "union", node->source_span);
        return false;
    }

    if (detail::ParseTreeMatcher::label_equals(node, "attribute_specifier") &&
        !feature_registry.has_feature(ParserFeature::GnuAttributeSpecifiers)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: GNU attribute specifiers",
            "__attribute__", node->source_span);
        return false;
    }

    if (detail::ParseTreeMatcher::label_equals(node, "var_decl") &&
        contains_label_prefix(node, "EXTERN") &&
        !feature_registry.has_feature(
            ParserFeature::ExternVariableDeclarations)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: extern variable declarations",
            "extern", node->source_span);
        return false;
    }

    if (detail::ParseTreeMatcher::label_equals(node, "parameter_decl") &&
        contains_label_prefix(node, "CONST") &&
        !feature_registry.has_feature(
            ParserFeature::QualifiedPrototypeParameters)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: qualified prototype parameters",
            "const", node->source_span);
        return false;
    }

    if (detail::ParseTreeMatcher::label_starts_with(node, "FLOAT16") &&
        !feature_registry.has_feature(
            ParserFeature::ExtendedBuiltinTypeSpecifiers)) {
        error_info = ParserErrorInfo(
            "unsupported syntax without enabled parser feature: extended builtin type specifiers",
            "_Float16", node->source_span);
        return false;
    }

    for (const auto &child : node->children) {
        if (!validate_node(child.get(), feature_registry, error_info)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ParserFeatureValidator::validate(
    const ParseTreeNode *parse_tree_root,
    const ParserFeatureRegistry &feature_registry,
    ParserErrorInfo &error_info) const {
    error_info = ParserErrorInfo();
    return validate_node(parse_tree_root, feature_registry, error_info);
}

} // namespace sysycc
