#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sysycc {

enum class ObjectTargetKind : unsigned char {
    None,
    ElfAArch64,
};

class ObjectResult {
  private:
    ObjectTargetKind target_kind_ = ObjectTargetKind::None;
    std::vector<std::uint8_t> bytes_;

  public:
    ObjectResult(ObjectTargetKind target_kind, std::vector<std::uint8_t> bytes)
        : target_kind_(target_kind), bytes_(std::move(bytes)) {}

    ObjectTargetKind get_target_kind() const noexcept { return target_kind_; }
    const std::vector<std::uint8_t> &get_bytes() const noexcept { return bytes_; }
};

} // namespace sysycc
