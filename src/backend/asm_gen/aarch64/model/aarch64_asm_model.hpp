#pragma once

#include <string>
#include <utility>
#include <vector>

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
    std::vector<std::string> module_asm_lines_;

  public:
    AArch64AsmArchProfile get_arch_profile() const noexcept { return arch_profile_; }
    void set_arch_profile(AArch64AsmArchProfile arch_profile) noexcept {
        arch_profile_ = arch_profile;
    }
    void append_module_asm_line(std::string line) {
        module_asm_lines_.push_back(std::move(line));
    }
    const std::vector<std::string> &get_module_asm_lines() const noexcept {
        return module_asm_lines_;
    }
};

} // namespace sysycc
