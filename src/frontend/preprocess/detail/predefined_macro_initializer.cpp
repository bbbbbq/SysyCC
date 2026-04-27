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
    define_object_like_macro(macro_table, "__FUNCTION__", "\"\"");
    define_object_like_macro(macro_table, "__PRETTY_FUNCTION__", "\"\"");
    define_function_like_macro(macro_table, "__builtin_expect", "(__value)",
                               {"__value", "__expected"});
    define_function_like_macro(macro_table, "__builtin_huge_valf",
                               "(1.0f / 0.0f)", {});
    define_function_like_macro(macro_table, "__builtin_huge_val",
                               "(1.0 / 0.0)", {});
    define_function_like_macro(macro_table, "__builtin_huge_vall",
                               "(1.0L / 0.0L)", {});
    define_function_like_macro(macro_table, "__builtin_inff",
                               "(1.0f / 0.0f)", {});
    define_function_like_macro(macro_table, "__builtin_inf",
                               "(1.0 / 0.0)", {});
    define_function_like_macro(macro_table, "__builtin_infl",
                               "(1.0L / 0.0L)", {});
    define_function_like_macro(macro_table, "__builtin_nanf",
                               "(0.0f / 0.0f)", {"__tag"});
    define_function_like_macro(macro_table, "__builtin_nan",
                               "(0.0 / 0.0)", {"__tag"});
    define_function_like_macro(macro_table, "__builtin_nanl",
                               "(0.0L / 0.0L)", {"__tag"});
    define_function_like_macro(macro_table, "__builtin_isnan",
                               "((__value) != (__value))", {"__value"});
    define_function_like_macro(
        macro_table, "__builtin_isfinite",
        "(((__value) == (__value)) && ((__value) != (1.0 / 0.0)) && ((__value) != (-1.0 / 0.0)))",
        {"__value"});
    define_function_like_macro(
        macro_table, "__builtin_isinf_sign",
        "(((__value) == (1.0 / 0.0)) ? 1 : (((__value) == (-1.0 / 0.0)) ? -1 : 0))",
        {"__value"});
    define_function_like_macro(
        macro_table, "__builtin_signbit",
        "(((__value) < 0) || ((1.0 / (__value)) < 0))", {"__value"});
    define_function_like_macro(
        macro_table, "__builtin_offsetof",
        "((unsigned long)(&(((__type *)0)->__member)))",
        {"__type", "__member"});
    define_function_like_macro(macro_table, "__builtin_va_start", "((void)0)",
                               {"__ap", "__last"});
    define_function_like_macro(macro_table, "__builtin_va_end", "((void)0)",
                               {"__ap"});
    define_function_like_macro(macro_table, "__builtin_va_copy", "((void)0)",
                               {"__dst", "__src"});
    define_function_like_macro(macro_table, "__builtin_va_arg",
                               "((__type)0)", {"__ap", "__type"});
    if (!preprocess_feature_registry.has_feature(
            PreprocessFeature::GnuPredefinedMacros)) {
        define_object_like_macro(macro_table, "__STRICT_ANSI__", "1");
        define_function_like_macro(macro_table, "__attribute__", "",
                                   {"__attrs"});
        define_function_like_macro(macro_table, "__attribute", "",
                                   {"__attrs"});
        define_function_like_macro(macro_table, "__asm__", "", {"__label"});
        define_function_like_macro(macro_table, "__asm", "", {"__label"});
        define_object_like_macro(macro_table, "__extension__", "");
    }

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
    define_object_like_macro(macro_table, "__UINT32_MAX__", "4294967295U");
#if defined(__INT64_MAX__)
    define_object_like_macro(macro_table, "__INT64_MAX__",
                             SYSYCC_STRINGIZE(__INT64_MAX__));
#endif
    define_object_like_macro(macro_table, "__UINT64_MAX__",
                             "18446744073709551615ULL");
#if defined(__SIZE_MAX__)
    define_object_like_macro(macro_table, "__SIZE_MAX__",
                             SYSYCC_STRINGIZE(__SIZE_MAX__));
#endif
    define_object_like_macro(macro_table, "__SIZE_TYPE__", "unsigned long");
    define_object_like_macro(macro_table, "__PTRDIFF_TYPE__", "long");
    define_object_like_macro(macro_table, "__INTPTR_TYPE__", "long");
    define_object_like_macro(macro_table, "__UINTPTR_TYPE__", "unsigned long");
    define_object_like_macro(macro_table, "__INTMAX_TYPE__", "long");
    define_object_like_macro(macro_table, "__UINTMAX_TYPE__", "unsigned long");
    define_object_like_macro(macro_table, "__WCHAR_TYPE__", "int");
    define_object_like_macro(macro_table, "__WINT_TYPE__", "unsigned int");
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
#if defined(__FLT_MIN_EXP__)
    define_object_like_macro(macro_table, "__FLT_MIN_EXP__",
                             SYSYCC_STRINGIZE(__FLT_MIN_EXP__));
#elif defined(FLT_MIN_EXP)
    define_object_like_macro(macro_table, "__FLT_MIN_EXP__",
                             SYSYCC_STRINGIZE(FLT_MIN_EXP));
#endif
#if defined(__DBL_MIN_EXP__)
    define_object_like_macro(macro_table, "__DBL_MIN_EXP__",
                             SYSYCC_STRINGIZE(__DBL_MIN_EXP__));
#elif defined(DBL_MIN_EXP)
    define_object_like_macro(macro_table, "__DBL_MIN_EXP__",
                             SYSYCC_STRINGIZE(DBL_MIN_EXP));
#endif
#if defined(__LDBL_MIN_EXP__)
    define_object_like_macro(macro_table, "__LDBL_MIN_EXP__",
                             SYSYCC_STRINGIZE(__LDBL_MIN_EXP__));
#elif defined(LDBL_MIN_EXP)
    define_object_like_macro(macro_table, "__LDBL_MIN_EXP__",
                             SYSYCC_STRINGIZE(LDBL_MIN_EXP));
#endif
#if defined(__FLT_MAX_EXP__)
    define_object_like_macro(macro_table, "__FLT_MAX_EXP__",
                             SYSYCC_STRINGIZE(__FLT_MAX_EXP__));
#elif defined(FLT_MAX_EXP)
    define_object_like_macro(macro_table, "__FLT_MAX_EXP__",
                             SYSYCC_STRINGIZE(FLT_MAX_EXP));
#endif
#if defined(__DBL_MAX_EXP__)
    define_object_like_macro(macro_table, "__DBL_MAX_EXP__",
                             SYSYCC_STRINGIZE(__DBL_MAX_EXP__));
#elif defined(DBL_MAX_EXP)
    define_object_like_macro(macro_table, "__DBL_MAX_EXP__",
                             SYSYCC_STRINGIZE(DBL_MAX_EXP));
#endif
#if defined(__LDBL_MAX_EXP__)
    define_object_like_macro(macro_table, "__LDBL_MAX_EXP__",
                             SYSYCC_STRINGIZE(__LDBL_MAX_EXP__));
#elif defined(LDBL_MAX_EXP)
    define_object_like_macro(macro_table, "__LDBL_MAX_EXP__",
                             SYSYCC_STRINGIZE(LDBL_MAX_EXP));
#endif
#if defined(__FLT_MIN_10_EXP__)
    define_object_like_macro(macro_table, "__FLT_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(__FLT_MIN_10_EXP__));
#elif defined(FLT_MIN_10_EXP)
    define_object_like_macro(macro_table, "__FLT_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(FLT_MIN_10_EXP));
#endif
#if defined(__DBL_MIN_10_EXP__)
    define_object_like_macro(macro_table, "__DBL_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(__DBL_MIN_10_EXP__));
#elif defined(DBL_MIN_10_EXP)
    define_object_like_macro(macro_table, "__DBL_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(DBL_MIN_10_EXP));
#endif
#if defined(__LDBL_MIN_10_EXP__)
    define_object_like_macro(macro_table, "__LDBL_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(__LDBL_MIN_10_EXP__));
#elif defined(LDBL_MIN_10_EXP)
    define_object_like_macro(macro_table, "__LDBL_MIN_10_EXP__",
                             SYSYCC_STRINGIZE(LDBL_MIN_10_EXP));
#endif
#if defined(__FLT_MAX_10_EXP__)
    define_object_like_macro(macro_table, "__FLT_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(__FLT_MAX_10_EXP__));
#elif defined(FLT_MAX_10_EXP)
    define_object_like_macro(macro_table, "__FLT_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(FLT_MAX_10_EXP));
#endif
#if defined(__DBL_MAX_10_EXP__)
    define_object_like_macro(macro_table, "__DBL_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(__DBL_MAX_10_EXP__));
#elif defined(DBL_MAX_10_EXP)
    define_object_like_macro(macro_table, "__DBL_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(DBL_MAX_10_EXP));
#endif
#if defined(__LDBL_MAX_10_EXP__)
    define_object_like_macro(macro_table, "__LDBL_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(__LDBL_MAX_10_EXP__));
#elif defined(LDBL_MAX_10_EXP)
    define_object_like_macro(macro_table, "__LDBL_MAX_10_EXP__",
                             SYSYCC_STRINGIZE(LDBL_MAX_10_EXP));
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
