#include "frontend/preprocess/detail/conditional_stack.hpp"

namespace sysycc::preprocess::detail {

void ConditionalStack::clear() { frames_.clear(); }

bool ConditionalStack::is_in_active_region() const noexcept {
    if (frames_.empty()) {
        return true;
    }

    return frames_.back().current_active;
}

bool ConditionalStack::has_frame() const noexcept { return !frames_.empty(); }

std::size_t ConditionalStack::get_frame_count() const noexcept {
    return frames_.size();
}

PassResult ConditionalStack::push_if(bool condition) {
    const bool parent_active = is_in_active_region();
    frames_.push_back(
        {parent_active, parent_active && condition, condition, false});
    return PassResult::Success();
}

PassResult ConditionalStack::handle_elif(bool condition) {
    if (!has_frame()) {
        return PassResult::Failure("unmatched #elif directive");
    }
    if (frames_.back().has_else) {
        return PassResult::Failure("#elif after #else is not allowed");
    }

    if (frames_.back().branch_taken) {
        frames_.back().current_active = false;
        return PassResult::Success();
    }

    frames_.back().current_active = frames_.back().parent_active && condition;
    frames_.back().branch_taken = condition;
    return PassResult::Success();
}

PassResult ConditionalStack::handle_else() {
    if (!has_frame()) {
        return PassResult::Failure("unmatched #else directive");
    }
    if (frames_.back().has_else) {
        return PassResult::Failure("duplicate #else directive");
    }

    frames_.back().has_else = true;
    frames_.back().current_active =
        frames_.back().parent_active && !frames_.back().branch_taken;
    frames_.back().branch_taken = true;
    return PassResult::Success();
}

PassResult ConditionalStack::handle_endif() {
    if (!has_frame()) {
        return PassResult::Failure("unmatched #endif directive");
    }

    frames_.pop_back();
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
