#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/dialects/builtin_types/builtin_type_extension_pack.hpp"
#include "frontend/dialects/c99/c99_dialect.hpp"
#include "frontend/dialects/clang/clang_dialect.hpp"
#include "frontend/dialects/dialect_manager.hpp"
#include "frontend/dialects/gnu/gnu_dialect.hpp"

using namespace sysycc;

int main() {
    DialectManager dialect_manager;
    dialect_manager.register_dialect(std::make_unique<C99Dialect>());
    dialect_manager.register_dialect(std::make_unique<GnuDialect>());
    dialect_manager.register_dialect(std::make_unique<ClangDialect>());
    dialect_manager.register_dialect(
        std::make_unique<BuiltinTypeExtensionPack>());

    const std::vector<std::string> dialect_names =
        dialect_manager.get_dialect_names();
    assert((dialect_names ==
            std::vector<std::string>{"c99", "gnu-c", "clang",
                                     "extended-builtin-types"}));
    assert(dialect_manager.get_registration_errors().empty());

    const auto &preprocess_registry =
        dialect_manager.get_preprocess_feature_registry();
    assert(preprocess_registry.has_feature(
        PreprocessFeature::ClangBuiltinProbes));
    assert(preprocess_registry.has_feature(PreprocessFeature::HasIncludeFamily));
    assert(preprocess_registry.has_feature(
        PreprocessFeature::GnuPredefinedMacros));
    assert(preprocess_registry.has_feature(
        PreprocessFeature::NonStandardDirectivePayloads));

    const auto &keyword_registry = dialect_manager.get_lexer_keyword_registry();
    assert(keyword_registry.has_keyword("const"));
    assert(keyword_registry.get_keyword_kind("const") == TokenKind::KwConst);
    assert(keyword_registry.has_keyword("__attribute__"));
    assert(keyword_registry.get_keyword_kind("__attribute__") ==
           TokenKind::KwAttribute);
    assert(keyword_registry.has_keyword("__signed"));
    assert(keyword_registry.get_keyword_kind("__signed") ==
           TokenKind::KwSigned);
    assert(keyword_registry.has_keyword("_Float16"));
    assert(keyword_registry.get_keyword_kind("_Float16") ==
           TokenKind::KwFloat16);
    assert(keyword_registry.get_conflicts().empty());

    const auto &parser_registry = dialect_manager.get_parser_feature_registry();
    assert(parser_registry.has_feature(
        ParserFeature::FunctionPrototypeDeclarations));
    assert(parser_registry.has_feature(ParserFeature::GnuAttributeSpecifiers));
    assert(parser_registry.has_feature(
        ParserFeature::ExtendedBuiltinTypeSpecifiers));

    const auto &ast_registry = dialect_manager.get_ast_feature_registry();
    assert(ast_registry.has_feature(AstFeature::GnuAttributeLists));
    assert(ast_registry.has_feature(AstFeature::ExtendedBuiltinTypes));

    const auto &semantic_registry =
        dialect_manager.get_semantic_feature_registry();
    assert(semantic_registry.has_feature(SemanticFeature::FunctionAttributes));
    assert(semantic_registry.has_feature(
        SemanticFeature::ExtendedBuiltinTypes));

    const auto &ir_registry = dialect_manager.get_ir_feature_registry();
    assert(ir_registry.has_feature(IrFeature::FunctionAttributes));

    const auto &probe_handler_registry =
        dialect_manager.get_preprocess_probe_handler_registry();
    assert(probe_handler_registry.has_handler(
        PreprocessProbeHandlerKind::ClangBuiltinProbes));
    assert(probe_handler_registry.get_registration_errors().empty());

    const auto &directive_handler_registry =
        dialect_manager.get_preprocess_directive_handler_registry();
    assert(directive_handler_registry.has_handler(
        PreprocessDirectiveHandlerKind::GnuWarningDirective));
    assert(directive_handler_registry.has_handler(
        PreprocessDirectiveHandlerKind::ClangWarningDirective));
    assert(directive_handler_registry.has_handler(
        PreprocessDirectiveHandlerKind::GnuPragmaOnceDirective));
    assert(directive_handler_registry.has_handler(
        PreprocessDirectiveHandlerKind::ClangPragmaOnceDirective));
    assert(directive_handler_registry.get_registration_errors().empty());

    const auto &attribute_handler_registry =
        dialect_manager.get_attribute_semantic_handler_registry();
    assert(attribute_handler_registry.has_handler(
        AttributeSemanticHandlerKind::GnuFunctionAttributes));
    assert(attribute_handler_registry.get_registration_errors().empty());

    const auto &builtin_type_handler_registry =
        dialect_manager.get_builtin_type_semantic_handler_registry();
    assert(builtin_type_handler_registry.has_handler(
        BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes));
    assert(builtin_type_handler_registry.get_registration_errors().empty());

    const auto &ir_extension_lowering_registry =
        dialect_manager.get_ir_extension_lowering_registry();
    assert(ir_extension_lowering_registry.has_handler(
        IrExtensionLoweringHandlerKind::GnuFunctionAttributes));
    assert(ir_extension_lowering_registry.get_registration_errors().empty());

    return 0;
}
