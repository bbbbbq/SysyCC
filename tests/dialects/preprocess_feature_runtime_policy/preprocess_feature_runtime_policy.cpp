#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

#include "frontend/dialects/core/dialect_manager.hpp"
#include "frontend/dialects/packs/clang/clang_dialect.hpp"
#include "frontend/dialects/packs/gnu/gnu_dialect.hpp"
#include "frontend/preprocess/detail/conditional/builtin_probe_evaluator.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"
#include "frontend/preprocess/detail/predefined_macro_initializer.hpp"

using namespace sysycc;
using namespace sysycc::preprocess::detail;

int main() {
    MacroTable macro_table;
    PreprocessFeatureRegistry no_features;
    initialize_predefined_macros(macro_table, no_features, "");
    assert(macro_table.has_macro("__STDC__"));
    assert(macro_table.has_macro("__STRICT_ANSI__"));

    DialectManager empty_dialects;
    BuiltinProbeEvaluator probe_evaluator;
    std::size_t index = 0;
    long long value = -1;
    bool handled = true;
    PassResult result = probe_evaluator.try_evaluate(
        "__has_include(<missing.h>)", index, macro_table, "", {}, {}, {},
        empty_dialects, value, handled);
    assert(result.ok);
    assert(!handled);

    index = 0;
    value = -1;
    handled = true;
    result = probe_evaluator.try_evaluate("__has_feature(modules)", index,
                                          macro_table, "", {}, {}, {},
                                          empty_dialects, value, handled);
    assert(result.ok);
    assert(!handled);

    DialectManager clang_dialects;
    clang_dialects.register_dialect(std::make_unique<ClangDialect>());
    index = 0;
    value = -1;
    handled = false;
    result = probe_evaluator.try_evaluate("__has_include(<missing.h>)", index,
                                          macro_table, "", {}, {}, {},
                                          clang_dialects, value, handled);
    assert(result.ok);
    assert(handled);
    assert(value == 0);

    index = 0;
    value = -1;
    handled = false;
    result = probe_evaluator.try_evaluate("__has_feature(modules)", index,
                                          macro_table, "", {}, {}, {},
                                          clang_dialects, value, handled);
    assert(result.ok);
    assert(handled);
    assert(value == 0);

    DialectManager gnu_dialects;
    gnu_dialects.register_dialect(std::make_unique<GnuDialect>());
    MacroTable gnu_macro_table;
    initialize_predefined_macros(
        gnu_macro_table, gnu_dialects.get_preprocess_feature_registry(), "");
    assert(gnu_macro_table.has_macro("__STDC__"));
    assert(!gnu_macro_table.has_macro("__STRICT_ANSI__"));

    return 0;
}
