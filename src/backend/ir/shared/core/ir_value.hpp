#pragma once

#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>

#include "common/source_span.hpp"

namespace sysycc {

class CoreIrInstruction;
class CoreIrType;

class CoreIrUse {
  private:
    CoreIrInstruction *user_ = nullptr;
    std::size_t operand_index_ = 0;

  public:
    CoreIrUse(CoreIrInstruction *user, std::size_t operand_index)
        : user_(user), operand_index_(operand_index) {}

    CoreIrInstruction *get_user() const noexcept { return user_; }

    std::size_t get_operand_index() const noexcept { return operand_index_; }
};

class CoreIrValue {
  private:
    const CoreIrType *type_ = nullptr;
    std::string name_;
    SourceSpan source_span_;
    std::vector<CoreIrUse> uses_;

  public:
    explicit CoreIrValue(const CoreIrType *type, std::string name = {})
        : type_(type), name_(std::move(name)) {}

    virtual ~CoreIrValue() = default;

    const CoreIrType *get_type() const noexcept { return type_; }

    const std::string &get_name() const noexcept { return name_; }

    void set_name(std::string name) { name_ = std::move(name); }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    void set_source_span(SourceSpan source_span) noexcept {
        source_span_ = source_span;
    }

    const std::vector<CoreIrUse> &get_uses() const noexcept { return uses_; }

    void add_use(CoreIrInstruction *user, std::size_t operand_index) {
        uses_.emplace_back(user, operand_index);
    }

    void remove_use(CoreIrInstruction *user, std::size_t operand_index) {
        uses_.erase(
            std::remove_if(uses_.begin(), uses_.end(),
                           [user, operand_index](const CoreIrUse &use) {
                               return use.get_user() == user &&
                                      use.get_operand_index() == operand_index;
                           }),
            uses_.end());
    }
};

} // namespace sysycc
