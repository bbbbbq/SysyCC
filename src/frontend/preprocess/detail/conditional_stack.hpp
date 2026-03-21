#pragma once

#include <cstddef>
#include <vector>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Manages nested conditional compilation state for one preprocessing session.
class ConditionalStack {
  private:
    struct ConditionalFrame {
        bool parent_active = true;
        bool current_active = true;
        bool branch_taken = false;
        bool has_else = false;
    };

    std::vector<ConditionalFrame> frames_;

  public:
    void clear();
    bool is_in_active_region() const noexcept;
    bool has_frame() const noexcept;
    std::size_t get_frame_count() const noexcept;
    bool get_should_evaluate_if_condition() const noexcept;
    bool get_should_evaluate_elif_condition() const noexcept;
    PassResult push_if(bool condition);
    PassResult handle_elif(bool condition);
    PassResult handle_else();
    PassResult handle_endif();
};

} // namespace sysycc::preprocess::detail
