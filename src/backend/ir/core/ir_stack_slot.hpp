#pragma once

#include <cstddef>
#include <string>

namespace sysycc {

class CoreIrType;

class CoreIrStackSlot {
  private:
    std::string name_;
    const CoreIrType *allocated_type_ = nullptr;
    std::size_t alignment_ = 0;

  public:
    CoreIrStackSlot(std::string name, const CoreIrType *allocated_type,
                    std::size_t alignment)
        : name_(std::move(name)),
          allocated_type_(allocated_type),
          alignment_(alignment) {}

    const std::string &get_name() const noexcept { return name_; }

    const CoreIrType *get_allocated_type() const noexcept {
        return allocated_type_;
    }

    std::size_t get_alignment() const noexcept { return alignment_; }
};

} // namespace sysycc
