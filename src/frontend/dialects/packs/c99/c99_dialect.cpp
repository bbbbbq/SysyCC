#include "frontend/dialects/packs/c99/c99_dialect.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

std::string_view C99Dialect::get_name() const noexcept { return "c99"; }

void C99Dialect::contribute_lexer_keywords(
    LexerKeywordRegistry &registry) const {
    registry.add_keyword("const", TokenKind::KwConst);
    registry.add_keyword("extern", TokenKind::KwExtern);
    registry.add_keyword("inline", TokenKind::KwInline);
    registry.add_keyword("long", TokenKind::KwLong);
    registry.add_keyword("signed", TokenKind::KwSigned);
    registry.add_keyword("short", TokenKind::KwShort);
    registry.add_keyword("unsigned", TokenKind::KwUnsigned);
    registry.add_keyword("int", TokenKind::KwInt);
    registry.add_keyword("char", TokenKind::KwChar);
    registry.add_keyword("void", TokenKind::KwVoid);
    registry.add_keyword("float", TokenKind::KwFloat);
    registry.add_keyword("double", TokenKind::KwDouble);
    registry.add_keyword("if", TokenKind::KwIf);
    registry.add_keyword("else", TokenKind::KwElse);
    registry.add_keyword("while", TokenKind::KwWhile);
    registry.add_keyword("for", TokenKind::KwFor);
    registry.add_keyword("do", TokenKind::KwDo);
    registry.add_keyword("switch", TokenKind::KwSwitch);
    registry.add_keyword("case", TokenKind::KwCase);
    registry.add_keyword("default", TokenKind::KwDefault);
    registry.add_keyword("break", TokenKind::KwBreak);
    registry.add_keyword("continue", TokenKind::KwContinue);
    registry.add_keyword("return", TokenKind::KwReturn);
    registry.add_keyword("struct", TokenKind::KwStruct);
    registry.add_keyword("union", TokenKind::KwUnion);
    registry.add_keyword("enum", TokenKind::KwEnum);
    registry.add_keyword("typedef", TokenKind::KwTypedef);
}

void C99Dialect::contribute_parser_features(
    ParserFeatureRegistry &registry) const {
    registry.add_feature(ParserFeature::FunctionPrototypeDeclarations);
    registry.add_feature(ParserFeature::ExternVariableDeclarations);
    registry.add_feature(ParserFeature::QualifiedPrototypeParameters);
    registry.add_feature(ParserFeature::UnionDeclarations);
}

void C99Dialect::contribute_ast_features(AstFeatureRegistry &registry) const {
    registry.add_feature(AstFeature::QualifiedTypeNodes);
    registry.add_feature(AstFeature::UnionTypeNodes);
}

void C99Dialect::contribute_semantic_features(
    SemanticFeatureRegistry &registry) const {
    registry.add_feature(SemanticFeature::QualifiedPointerConversions);
    registry.add_feature(SemanticFeature::UnionSemanticTypes);
}

} // namespace sysycc
