#include "frontend/preprocess/detail/predefined_macro_initializer.hpp"

#include <cfloat>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

void define_function_like_macro(MacroTable &macro_table, const std::string &name,
                                const std::string &replacement,
                                std::vector<std::string> parameters) {
    const PassResult result = macro_table.define_macro(
        MacroDefinition(name, replacement, true, false, std::move(parameters)));
    (void)result;
}

std::string escape_string_literal(std::string text) {
    std::string escaped;
    escaped.reserve(text.size() + 2);
    escaped.push_back('"');
    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

void initialize_predefined_macros(
    MacroTable &macro_table,
    const PreprocessFeatureRegistry &preprocess_feature_registry,
    const std::string &primary_input_file) {
    if (!preprocess_feature_registry.has_feature(
            PreprocessFeature::GnuPredefinedMacros)) {
        return;
    }

    const std::string input_file_name =
        primary_input_file.empty()
            ? std::string("<unknown>")
            : std::filesystem::path(primary_input_file).filename().string();

    define_object_like_macro(macro_table, "__STDC__", "1");
    define_object_like_macro(macro_table, "__STDC_HOSTED__", "1");
    define_object_like_macro(macro_table, "__STDC_VERSION__", "201710L");
    define_object_like_macro(
        macro_table, "__FILE__",
        escape_string_literal(primary_input_file.empty() ? "<unknown>"
                                                         : primary_input_file));
    define_object_like_macro(macro_table, "__FILE_NAME__",
                             escape_string_literal(input_file_name));
    define_object_like_macro(macro_table, "__LINE__", "1");
    define_object_like_macro(macro_table, "__func__", "\"\"");
    define_function_like_macro(macro_table, "__builtin_expect", "(__value)",
                               {"__value", "__expected"});

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

#if defined(__CHAR_BIT__)
    define_object_like_macro(macro_table, "__CHAR_BIT__",
                             SYSYCC_STRINGIZE(__CHAR_BIT__));
#endif
#if defined(__SCHAR_MAX__)
    define_object_like_macro(macro_table, "__SCHAR_MAX__",
                             SYSYCC_STRINGIZE(__SCHAR_MAX__));
#endif
#if defined(__SHRT_MAX__)
    define_object_like_macro(macro_table, "__SHRT_MAX__",
                             SYSYCC_STRINGIZE(__SHRT_MAX__));
#endif
#if defined(__INT_MAX__)
    define_object_like_macro(macro_table, "__INT_MAX__",
                             SYSYCC_STRINGIZE(__INT_MAX__));
#endif
#if defined(__LONG_MAX__)
    define_object_like_macro(macro_table, "__LONG_MAX__",
                             SYSYCC_STRINGIZE(__LONG_MAX__));
#endif
#if defined(__LONG_LONG_MAX__)
    define_object_like_macro(macro_table, "__LONG_LONG_MAX__",
                             SYSYCC_STRINGIZE(__LONG_LONG_MAX__));
#endif
#if defined(__INT8_MAX__)
    define_object_like_macro(macro_table, "__INT8_MAX__",
                             SYSYCC_STRINGIZE(__INT8_MAX__));
#endif
#if defined(__INT16_MAX__)
    define_object_like_macro(macro_table, "__INT16_MAX__",
                             SYSYCC_STRINGIZE(__INT16_MAX__));
#endif
#if defined(__INT32_MAX__)
    define_object_like_macro(macro_table, "__INT32_MAX__",
                             SYSYCC_STRINGIZE(__INT32_MAX__));
#endif
#if defined(__INT64_MAX__)
    define_object_like_macro(macro_table, "__INT64_MAX__",
                             SYSYCC_STRINGIZE(__INT64_MAX__));
#endif
#if defined(__SIZE_MAX__)
    define_object_like_macro(macro_table, "__SIZE_MAX__",
                             SYSYCC_STRINGIZE(__SIZE_MAX__));
#endif
#if defined(__FLT_RADIX__)
    define_object_like_macro(macro_table, "__FLT_RADIX__",
                             SYSYCC_STRINGIZE(__FLT_RADIX__));
#endif
#if defined(__FLT_EVAL_METHOD__)
    define_object_like_macro(macro_table, "__FLT_EVAL_METHOD__",
                             SYSYCC_STRINGIZE(__FLT_EVAL_METHOD__));
#endif
#if defined(__FLT_MANT_DIG__)
    define_object_like_macro(macro_table, "__FLT_MANT_DIG__",
                             SYSYCC_STRINGIZE(__FLT_MANT_DIG__));
#endif
#if defined(__DBL_MANT_DIG__)
    define_object_like_macro(macro_table, "__DBL_MANT_DIG__",
                             SYSYCC_STRINGIZE(__DBL_MANT_DIG__));
#endif
#if defined(__LDBL_MANT_DIG__)
    define_object_like_macro(macro_table, "__LDBL_MANT_DIG__",
                             SYSYCC_STRINGIZE(__LDBL_MANT_DIG__));
#endif
#if defined(__FLT_DIG__)
    define_object_like_macro(macro_table, "__FLT_DIG__",
                             SYSYCC_STRINGIZE(__FLT_DIG__));
#endif
#if defined(__DBL_DIG__)
    define_object_like_macro(macro_table, "__DBL_DIG__",
                             SYSYCC_STRINGIZE(__DBL_DIG__));
#endif
#if defined(__LDBL_DIG__)
    define_object_like_macro(macro_table, "__LDBL_DIG__",
                             SYSYCC_STRINGIZE(__LDBL_DIG__));
#endif
#if defined(__FLT_EPSILON__)
    define_object_like_macro(macro_table, "__FLT_EPSILON__",
                             SYSYCC_STRINGIZE(__FLT_EPSILON__));
#endif
#if defined(__DBL_EPSILON__)
    define_object_like_macro(macro_table, "__DBL_EPSILON__",
                             SYSYCC_STRINGIZE(__DBL_EPSILON__));
#endif
#if defined(__LDBL_EPSILON__)
    define_object_like_macro(macro_table, "__LDBL_EPSILON__",
                             SYSYCC_STRINGIZE(__LDBL_EPSILON__));
#endif

#if defined(INT8_MIN)
    define_object_like_macro(macro_table, "INT8_MIN", SYSYCC_STRINGIZE(INT8_MIN));
#endif
#if defined(CHAR_BIT)
    define_object_like_macro(macro_table, "CHAR_BIT",
                             SYSYCC_STRINGIZE(CHAR_BIT));
#endif
#if defined(SCHAR_MIN)
    define_object_like_macro(macro_table, "SCHAR_MIN",
                             SYSYCC_STRINGIZE(SCHAR_MIN));
#endif
#if defined(SCHAR_MAX)
    define_object_like_macro(macro_table, "SCHAR_MAX",
                             SYSYCC_STRINGIZE(SCHAR_MAX));
#endif
#if defined(UCHAR_MAX)
    define_object_like_macro(macro_table, "UCHAR_MAX",
                             SYSYCC_STRINGIZE(UCHAR_MAX));
#endif
#if defined(CHAR_MIN)
    define_object_like_macro(macro_table, "CHAR_MIN",
                             SYSYCC_STRINGIZE(CHAR_MIN));
#endif
#if defined(CHAR_MAX)
    define_object_like_macro(macro_table, "CHAR_MAX",
                             SYSYCC_STRINGIZE(CHAR_MAX));
#endif
#if defined(SHRT_MIN)
    define_object_like_macro(macro_table, "SHRT_MIN",
                             SYSYCC_STRINGIZE(SHRT_MIN));
#endif
#if defined(SHRT_MAX)
    define_object_like_macro(macro_table, "SHRT_MAX",
                             SYSYCC_STRINGIZE(SHRT_MAX));
#endif
#if defined(USHRT_MAX)
    define_object_like_macro(macro_table, "USHRT_MAX",
                             SYSYCC_STRINGIZE(USHRT_MAX));
#endif
#if defined(INT_MIN)
    define_object_like_macro(macro_table, "INT_MIN",
                             SYSYCC_STRINGIZE(INT_MIN));
#endif
#if defined(INT_MAX)
    define_object_like_macro(macro_table, "INT_MAX",
                             SYSYCC_STRINGIZE(INT_MAX));
#endif
#if defined(UINT_MAX)
    define_object_like_macro(macro_table, "UINT_MAX",
                             SYSYCC_STRINGIZE(UINT_MAX));
#endif
#if defined(LONG_MIN)
    define_object_like_macro(macro_table, "LONG_MIN",
                             SYSYCC_STRINGIZE(LONG_MIN));
#endif
#if defined(LONG_MAX)
    define_object_like_macro(macro_table, "LONG_MAX",
                             SYSYCC_STRINGIZE(LONG_MAX));
#endif
#if defined(ULONG_MAX)
    define_object_like_macro(macro_table, "ULONG_MAX",
                             SYSYCC_STRINGIZE(ULONG_MAX));
#endif
#if defined(LLONG_MIN)
    define_object_like_macro(macro_table, "LLONG_MIN",
                             SYSYCC_STRINGIZE(LLONG_MIN));
#endif
#if defined(LLONG_MAX)
    define_object_like_macro(macro_table, "LLONG_MAX",
                             SYSYCC_STRINGIZE(LLONG_MAX));
#endif
#if defined(ULLONG_MAX)
    define_object_like_macro(macro_table, "ULLONG_MAX",
                             SYSYCC_STRINGIZE(ULLONG_MAX));
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
