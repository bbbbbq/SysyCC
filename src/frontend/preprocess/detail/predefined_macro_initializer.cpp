#include "frontend/preprocess/detail/predefined_macro_initializer.hpp"

#include <cfloat>
#include <climits>
#include <cstdint>
#include <string>

namespace sysycc::preprocess::detail {

namespace {

#define SYSYCC_STRINGIZE_IMPL(value) #value
#define SYSYCC_STRINGIZE(value) SYSYCC_STRINGIZE_IMPL(value)

void define_object_like_macro(MacroTable &macro_table, const std::string &name,
                              const std::string &replacement) {
    const PassResult result =
        macro_table.define_macro(MacroDefinition(name, replacement));
    (void)result;
}

} // namespace

void initialize_predefined_macros(
    MacroTable &macro_table,
    const PreprocessFeatureRegistry &preprocess_feature_registry) {
    if (!preprocess_feature_registry.has_feature(
            PreprocessFeature::GnuPredefinedMacros)) {
        return;
    }

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

#if defined(INT8_MIN)
    define_object_like_macro(macro_table, "INT8_MIN", SYSYCC_STRINGIZE(INT8_MIN));
#endif
#if defined(INT8_MAX)
    define_object_like_macro(macro_table, "INT8_MAX", SYSYCC_STRINGIZE(INT8_MAX));
#endif
#if defined(UINT8_MAX)
    define_object_like_macro(macro_table, "UINT8_MAX",
                             SYSYCC_STRINGIZE(UINT8_MAX));
#endif
#if defined(INT16_MIN)
    define_object_like_macro(macro_table, "INT16_MIN",
                             SYSYCC_STRINGIZE(INT16_MIN));
#endif
#if defined(INT16_MAX)
    define_object_like_macro(macro_table, "INT16_MAX",
                             SYSYCC_STRINGIZE(INT16_MAX));
#endif
#if defined(UINT16_MAX)
    define_object_like_macro(macro_table, "UINT16_MAX",
                             SYSYCC_STRINGIZE(UINT16_MAX));
#endif
#if defined(INT32_MIN)
    define_object_like_macro(macro_table, "INT32_MIN",
                             SYSYCC_STRINGIZE(INT32_MIN));
#endif
#if defined(INT32_MAX)
    define_object_like_macro(macro_table, "INT32_MAX",
                             SYSYCC_STRINGIZE(INT32_MAX));
#endif
#if defined(UINT32_MAX)
    define_object_like_macro(macro_table, "UINT32_MAX",
                             SYSYCC_STRINGIZE(UINT32_MAX));
#endif
#if defined(INT64_MIN)
    define_object_like_macro(macro_table, "INT64_MIN",
                             SYSYCC_STRINGIZE(INT64_MIN));
#endif
#if defined(INT64_MAX)
    define_object_like_macro(macro_table, "INT64_MAX",
                             SYSYCC_STRINGIZE(INT64_MAX));
#endif
#if defined(UINT64_MAX)
    define_object_like_macro(macro_table, "UINT64_MAX",
                             SYSYCC_STRINGIZE(UINT64_MAX));
#endif
#if defined(INTPTR_MIN)
    define_object_like_macro(macro_table, "INTPTR_MIN",
                             SYSYCC_STRINGIZE(INTPTR_MIN));
#endif
#if defined(INTPTR_MAX)
    define_object_like_macro(macro_table, "INTPTR_MAX",
                             SYSYCC_STRINGIZE(INTPTR_MAX));
#endif
#if defined(UINTPTR_MAX)
    define_object_like_macro(macro_table, "UINTPTR_MAX",
                             SYSYCC_STRINGIZE(UINTPTR_MAX));
#endif
#if defined(SIZE_MAX)
    define_object_like_macro(macro_table, "SIZE_MAX",
                             SYSYCC_STRINGIZE(SIZE_MAX));
#endif

#if defined(FLT_MIN)
    define_object_like_macro(macro_table, "__FLT_MIN__",
                             SYSYCC_STRINGIZE(FLT_MIN));
#endif
#if defined(FLT_MAX)
    define_object_like_macro(macro_table, "__FLT_MAX__",
                             SYSYCC_STRINGIZE(FLT_MAX));
#endif
#if defined(DBL_MIN)
    define_object_like_macro(macro_table, "__DBL_MIN__",
                             SYSYCC_STRINGIZE(DBL_MIN));
#endif
#if defined(DBL_MAX)
    define_object_like_macro(macro_table, "__DBL_MAX__",
                             SYSYCC_STRINGIZE(DBL_MAX));
#endif
#if defined(LDBL_MIN)
    define_object_like_macro(macro_table, "__LDBL_MIN__",
                             SYSYCC_STRINGIZE(LDBL_MIN));
#endif
#if defined(LDBL_MAX)
    define_object_like_macro(macro_table, "__LDBL_MAX__",
                             SYSYCC_STRINGIZE(LDBL_MAX));
#endif
}

} // namespace sysycc::preprocess::detail
