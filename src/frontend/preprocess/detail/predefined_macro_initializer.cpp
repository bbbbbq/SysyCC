#include "frontend/preprocess/detail/predefined_macro_initializer.hpp"

#include <string>

namespace sysycc::preprocess::detail {

namespace {

void define_object_like_macro(MacroTable &macro_table, const std::string &name,
                              const std::string &replacement) {
    const PassResult result =
        macro_table.define_macro(MacroDefinition(name, replacement));
    (void)result;
}

} // namespace

void initialize_predefined_macros(MacroTable &macro_table) {
    define_object_like_macro(macro_table, "__STDC__", "1");
    define_object_like_macro(macro_table, "__STDC_HOSTED__", "1");
    define_object_like_macro(macro_table, "__STDC_VERSION__", "201710L");

#if defined(__APPLE__)
    define_object_like_macro(macro_table, "__APPLE__", "1");
#endif

#if defined(__MACH__)
    define_object_like_macro(macro_table, "__MACH__", "1");
#endif

#if defined(__LP64__)
    define_object_like_macro(macro_table, "__LP64__", "1");
#endif

#if defined(__arm64__)
    define_object_like_macro(macro_table, "__arm64__", "1");
#endif

#if defined(__aarch64__)
    define_object_like_macro(macro_table, "__aarch64__", "1");
#endif

#if defined(__x86_64__)
    define_object_like_macro(macro_table, "__x86_64__", "1");
#endif

#if defined(__i386__)
    define_object_like_macro(macro_table, "__i386__", "1");
#endif

#if defined(__arm__)
    define_object_like_macro(macro_table, "__arm__", "1");
#endif

#if defined(__clang__)
    define_object_like_macro(macro_table, "__clang__", "1");
    define_object_like_macro(macro_table, "__clang_major__",
                             std::to_string(__clang_major__));
    define_object_like_macro(macro_table, "__clang_minor__",
                             std::to_string(__clang_minor__));
    define_object_like_macro(macro_table, "__clang_patchlevel__",
                             std::to_string(__clang_patchlevel__));
#endif

#if defined(__GNUC__)
    define_object_like_macro(macro_table, "__GNUC__",
                             std::to_string(__GNUC__));
#endif

#if defined(__GNUC_MINOR__)
    define_object_like_macro(macro_table, "__GNUC_MINOR__",
                             std::to_string(__GNUC_MINOR__));
#endif

#if defined(__GNUC_PATCHLEVEL__)
    define_object_like_macro(macro_table, "__GNUC_PATCHLEVEL__",
                             std::to_string(__GNUC_PATCHLEVEL__));
#endif
}

} // namespace sysycc::preprocess::detail
