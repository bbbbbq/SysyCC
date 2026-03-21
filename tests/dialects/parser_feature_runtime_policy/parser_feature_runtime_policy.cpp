#include <cassert>
#include <memory>

#include "frontend/dialects/registries/parser_feature_registry.hpp"
#include "frontend/parser/parser_feature_validator.hpp"

using namespace sysycc;

namespace {

std::unique_ptr<ParseTreeNode> make_terminal(const char *label,
                                             SourceSpan source_span = {}) {
    return std::make_unique<ParseTreeNode>(label, source_span);
}

std::unique_ptr<ParseTreeNode> build_function_prototype_tree() {
    auto node = std::make_unique<ParseTreeNode>("func_decl", SourceSpan());
    return node;
}

std::unique_ptr<ParseTreeNode> build_attribute_tree() {
    auto node = std::make_unique<ParseTreeNode>("attribute_specifier", SourceSpan());
    node->children.push_back(make_terminal("ATTRIBUTE __attribute__"));
    return node;
}

std::unique_ptr<ParseTreeNode> build_float16_tree() {
    auto node = std::make_unique<ParseTreeNode>("type_specifier", SourceSpan());
    node->children.push_back(make_terminal("FLOAT16 _Float16"));
    return node;
}

std::unique_ptr<ParseTreeNode> build_union_tree() {
    auto node = std::make_unique<ParseTreeNode>("union_decl", SourceSpan());
    node->children.push_back(make_terminal("UNION union"));
    return node;
}

std::unique_ptr<ParseTreeNode> build_extern_var_tree() {
    auto node = std::make_unique<ParseTreeNode>("var_decl", SourceSpan());
    auto storage = std::make_unique<ParseTreeNode>("storage_specifier_opt", SourceSpan());
    storage->children.push_back(make_terminal("EXTERN extern"));
    node->children.push_back(std::move(storage));
    return node;
}

std::unique_ptr<ParseTreeNode> build_const_parameter_tree() {
    auto node = std::make_unique<ParseTreeNode>("parameter_decl", SourceSpan());
    node->children.push_back(make_terminal("CONST const"));
    return node;
}

} // namespace

int main() {
    ParserFeatureValidator validator;
    ParserErrorInfo error_info;
    ParserFeatureRegistry feature_registry;

    auto prototype_tree = build_function_prototype_tree();
    assert(!validator.validate(prototype_tree.get(), feature_registry, error_info));
    feature_registry.add_feature(ParserFeature::FunctionPrototypeDeclarations);
    assert(validator.validate(prototype_tree.get(), feature_registry, error_info));

    feature_registry = ParserFeatureRegistry();
    auto attribute_tree = build_attribute_tree();
    assert(!validator.validate(attribute_tree.get(), feature_registry, error_info));
    feature_registry.add_feature(ParserFeature::GnuAttributeSpecifiers);
    assert(validator.validate(attribute_tree.get(), feature_registry, error_info));

    feature_registry = ParserFeatureRegistry();
    auto float16_tree = build_float16_tree();
    assert(!validator.validate(float16_tree.get(), feature_registry, error_info));
    feature_registry.add_feature(ParserFeature::ExtendedBuiltinTypeSpecifiers);
    assert(validator.validate(float16_tree.get(), feature_registry, error_info));

    feature_registry = ParserFeatureRegistry();
    auto union_tree = build_union_tree();
    assert(!validator.validate(union_tree.get(), feature_registry, error_info));
    feature_registry.add_feature(ParserFeature::UnionDeclarations);
    assert(validator.validate(union_tree.get(), feature_registry, error_info));

    feature_registry = ParserFeatureRegistry();
    auto extern_var_tree = build_extern_var_tree();
    assert(!validator.validate(extern_var_tree.get(), feature_registry, error_info));
    feature_registry.add_feature(ParserFeature::ExternVariableDeclarations);
    assert(validator.validate(extern_var_tree.get(), feature_registry, error_info));

    feature_registry = ParserFeatureRegistry();
    auto const_parameter_tree = build_const_parameter_tree();
    assert(!validator.validate(const_parameter_tree.get(), feature_registry,
                               error_info));
    feature_registry.add_feature(ParserFeature::QualifiedPrototypeParameters);
    assert(validator.validate(const_parameter_tree.get(), feature_registry,
                              error_info));

    return 0;
}
