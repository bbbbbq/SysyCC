#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    CompilerContext context;
    context.configure_dialects(false, false, false);
    const auto &dialect_manager = context.get_dialect_manager();
    const std::vector<std::string> dialect_names =
        dialect_manager.get_dialect_names();

    assert((dialect_names == std::vector<std::string>{"c99"}));
    assert(!dialect_manager.get_lexer_keyword_registry().has_keyword(
        "__attribute__"));
    assert(!dialect_manager.get_lexer_keyword_registry().has_keyword(
        "_Float16"));
    assert(!dialect_manager.get_preprocess_feature_registry().has_feature(
        PreprocessFeature::ClangBuiltinProbes));
    assert(!dialect_manager.get_preprocess_feature_registry().has_feature(
        PreprocessFeature::GnuPredefinedMacros));
    assert(!dialect_manager.get_semantic_feature_registry().has_feature(
        SemanticFeature::ExtendedBuiltinTypes));
    return 0;
}
