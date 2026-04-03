#pragma once

#include <string>

namespace sysycc {

class CoreIrConstant;
class CoreIrType;

class CoreIrGlobal {
  private:
    std::string name_;
    const CoreIrType *type_ = nullptr;
    const CoreIrConstant *initializer_ = nullptr;
    bool is_internal_linkage_ = false;
    bool is_constant_ = false;

  public:
    CoreIrGlobal(std::string name, const CoreIrType *type,
                 const CoreIrConstant *initializer, bool is_internal_linkage,
                 bool is_constant)
        : name_(std::move(name)),
          type_(type),
          initializer_(initializer),
          is_internal_linkage_(is_internal_linkage),
          is_constant_(is_constant) {}

    const std::string &get_name() const noexcept { return name_; }

    const CoreIrType *get_type() const noexcept { return type_; }

    const CoreIrConstant *get_initializer() const noexcept {
        return initializer_;
    }

    void set_initializer(const CoreIrConstant *initializer) noexcept {
        initializer_ = initializer;
    }

    bool get_is_internal_linkage() const noexcept {
        return is_internal_linkage_;
    }

    bool get_is_constant() const noexcept { return is_constant_; }

    void set_is_constant(bool is_constant) noexcept { is_constant_ = is_constant; }
};

} // namespace sysycc
