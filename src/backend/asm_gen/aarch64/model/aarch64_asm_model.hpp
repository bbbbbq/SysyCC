#pragma once

namespace sysycc {

// Asm-only metadata belongs here. Do not route it back through
// AArch64ObjectModule or AArch64MachineModule.
enum class AArch64AsmArchProfile : unsigned char {
    Armv8A,
    Armv82AWithFp16,
};

class AArch64AsmModule {
  private:
    AArch64AsmArchProfile arch_profile_ = AArch64AsmArchProfile::Armv8A;

  public:
    AArch64AsmArchProfile get_arch_profile() const noexcept { return arch_profile_; }
    void set_arch_profile(AArch64AsmArchProfile arch_profile) noexcept {
        arch_profile_ = arch_profile;
    }
};

} // namespace sysycc
