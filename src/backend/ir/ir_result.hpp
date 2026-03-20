#pragma once

#include <string>
#include <utility>

#include "backend/ir/ir_kind.hpp"

namespace sysycc {

// Stores one IR generation result and its backend kind.
class IRResult {
  private:
    IrKind kind_;
    std::string text_;

  public:
    IRResult(IrKind kind, std::string text)
        : kind_(kind), text_(std::move(text)) {}

    IrKind get_kind() const noexcept { return kind_; }

    const std::string &get_text() const noexcept { return text_; }
};

} // namespace sysycc
