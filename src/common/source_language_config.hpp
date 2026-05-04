#pragma once

namespace sysycc {

enum class BakedSourceLanguageStandard {
    SysY22,
    C,
};

#if defined(SYSYCC_SOURCE_LANGUAGE_STANDARD_C)
inline constexpr BakedSourceLanguageStandard kBakedSourceLanguageStandard =
    BakedSourceLanguageStandard::C;
#else
inline constexpr BakedSourceLanguageStandard kBakedSourceLanguageStandard =
    BakedSourceLanguageStandard::SysY22;
#endif

inline constexpr bool kUseSysY22FloatingLiteralSemantics =
    kBakedSourceLanguageStandard == BakedSourceLanguageStandard::SysY22;

inline constexpr bool kInstallSysY22RuntimePredeclaredFunctions =
    kBakedSourceLanguageStandard == BakedSourceLanguageStandard::SysY22;

} // namespace sysycc
