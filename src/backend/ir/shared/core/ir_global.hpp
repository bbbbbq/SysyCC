#pragma once

#include <string>

namespace sysycc {

class CoreIrConstant;
class CoreIrModule;
class CoreIrType;

class CoreIrGlobal {
  private:
    std::string name_;
    const CoreIrType *type_ = nullptr;
    const CoreIrConstant *initializer_ = nullptr;
    bool is_internal_linkage_ = false;
    bool is_constant_ = false;
    bool is_external_declaration_ = false;
    CoreIrModule *parent_ = nullptr;

  public:
    CoreIrGlobal(std::string name, const CoreIrType *type,
                 const CoreIrConstant *initializer, bool is_internal_linkage,
                 bool is_constant, bool is_external_declaration = false)
        : name_(std::move(name)),
          type_(type),
          initializer_(initializer),
          is_internal_linkage_(is_internal_linkage),
          is_constant_(is_constant),
          is_external_declaration_(is_external_declaration) {}

    const std::string &get_name() const noexcept { return name_; }

    CoreIrModule *get_parent() const noexcept { return parent_; }

    void set_parent(CoreIrModule *parent) noexcept { parent_ = parent; }

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

    void set_is_internal_linkage(bool is_internal_linkage) noexcept {
        is_internal_linkage_ = is_internal_linkage;
    }

    bool get_is_constant() const noexcept { return is_constant_; }

    void set_is_constant(bool is_constant) noexcept { is_constant_ = is_constant; }

    bool get_is_external_declaration() const noexcept {
        return is_external_declaration_;
    }

    void set_is_external_declaration(bool is_external_declaration) noexcept {
        is_external_declaration_ = is_external_declaration;
    }
};

} // namespace sysycc
