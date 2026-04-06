#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"

namespace sysycc {

class CoreIrContext;

class CoreIrModule {
  private:
    std::string name_;
    CoreIrContext *parent_context_ = nullptr;
    std::vector<std::unique_ptr<CoreIrGlobal>> globals_;
    std::vector<std::unique_ptr<CoreIrFunction>> functions_;

  public:
    explicit CoreIrModule(std::string name) : name_(std::move(name)) {}

    const std::string &get_name() const noexcept { return name_; }

    CoreIrContext *get_parent_context() const noexcept { return parent_context_; }

    void set_parent_context(CoreIrContext *parent_context) noexcept {
        parent_context_ = parent_context;
    }

    const std::vector<std::unique_ptr<CoreIrGlobal>> &get_globals() const noexcept {
        return globals_;
    }

    std::vector<std::unique_ptr<CoreIrGlobal>> &get_globals() noexcept {
        return globals_;
    }

    const std::vector<std::unique_ptr<CoreIrFunction>> &
    get_functions() const noexcept {
        return functions_;
    }

    std::vector<std::unique_ptr<CoreIrFunction>> &get_functions() noexcept {
        return functions_;
    }

    CoreIrGlobal *find_global(const std::string &name) const noexcept {
        for (const auto &global : globals_) {
            if (global != nullptr && global->get_name() == name) {
                return global.get();
            }
        }
        return nullptr;
    }

    CoreIrFunction *find_function(const std::string &name) const noexcept {
        for (const auto &function : functions_) {
            if (function != nullptr && function->get_name() == name) {
                return function.get();
            }
        }
        return nullptr;
    }

    CoreIrGlobal *append_global(std::unique_ptr<CoreIrGlobal> global) {
        if (global == nullptr) {
            return nullptr;
        }
        global->set_parent(this);
        CoreIrGlobal *global_ptr = global.get();
        globals_.push_back(std::move(global));
        return global_ptr;
    }

    CoreIrFunction *append_function(std::unique_ptr<CoreIrFunction> function) {
        if (function == nullptr) {
            return nullptr;
        }
        function->set_parent(this);
        CoreIrFunction *function_ptr = function.get();
        functions_.push_back(std::move(function));
        return function_ptr;
    }

    template <typename T, typename... Args>
    T *create_global(Args &&...args) {
        auto global = std::make_unique<T>(std::forward<Args>(args)...);
        T *global_ptr = global.get();
        append_global(std::move(global));
        return global_ptr;
    }

    template <typename T, typename... Args>
    T *create_function(Args &&...args) {
        auto function = std::make_unique<T>(std::forward<Args>(args)...);
        T *function_ptr = function.get();
        append_function(std::move(function));
        return function_ptr;
    }
};

} // namespace sysycc
