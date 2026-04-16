#pragma once

#include <string>
#include <utility>

namespace sysycc {

enum class AsmTargetKind : unsigned char {
    None,
    AArch64,
    Riscv64,
};

class AsmResult {
  private:
    AsmTargetKind target_kind_;
    std::string text_;

  public:
    AsmResult(AsmTargetKind target_kind, std::string text)
        : target_kind_(target_kind), text_(std::move(text)) {}

    AsmTargetKind get_target_kind() const noexcept { return target_kind_; }

    const std::string &get_text() const noexcept { return text_; }
};

} // namespace sysycc
