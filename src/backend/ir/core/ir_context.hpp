#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "backend/ir/core/ir_constant.hpp"
#include "backend/ir/core/ir_module.hpp"
#include "backend/ir/core/ir_type.hpp"

namespace sysycc {

class CoreIrContext {
  private:
    std::vector<std::unique_ptr<CoreIrType>> owned_types_;
    std::vector<std::unique_ptr<CoreIrConstant>> owned_constants_;
    std::vector<std::unique_ptr<CoreIrModule>> owned_modules_;

  public:
    template <typename T, typename... Args>
    T *create_type(Args &&...args) {
        auto type = std::make_unique<T>(std::forward<Args>(args)...);
        T *type_ptr = type.get();
        owned_types_.push_back(std::move(type));
        return type_ptr;
    }

    template <typename T, typename... Args>
    T *create_constant(Args &&...args) {
        auto constant = std::make_unique<T>(std::forward<Args>(args)...);
        T *constant_ptr = constant.get();
        owned_constants_.push_back(std::move(constant));
        return constant_ptr;
    }

    template <typename T, typename... Args>
    T *create_module(Args &&...args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        T *module_ptr = module.get();
        owned_modules_.push_back(std::move(module));
        return module_ptr;
    }
};

} // namespace sysycc
