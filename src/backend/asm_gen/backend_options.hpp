#pragma once

#include <string>
#include <utility>

#include "backend/asm_gen/backend_kind.hpp"

namespace sysycc {

class BackendOptions {
  private:
    BackendKind backend_kind_ = BackendKind::LlvmIr;
    std::string target_triple_;
    std::string output_file_;
    bool position_independent_ = false;
    bool debug_info_ = false;

  public:
    BackendKind get_backend_kind() const noexcept { return backend_kind_; }

    void set_backend_kind(BackendKind backend_kind) noexcept {
        backend_kind_ = backend_kind;
    }

    const std::string &get_target_triple() const noexcept {
        return target_triple_;
    }

    void set_target_triple(std::string target_triple) {
        target_triple_ = std::move(target_triple);
    }

    const std::string &get_output_file() const noexcept { return output_file_; }

    void set_output_file(std::string output_file) {
        output_file_ = std::move(output_file);
    }

    bool get_position_independent() const noexcept { return position_independent_; }

    void set_position_independent(bool position_independent) noexcept {
        position_independent_ = position_independent;
    }

    bool get_debug_info() const noexcept { return debug_info_; }

    void set_debug_info(bool debug_info) noexcept { debug_info_ = debug_info; }
};

} // namespace sysycc
