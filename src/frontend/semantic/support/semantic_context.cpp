#include "frontend/semantic/support/semantic_context.hpp"

#include <optional>
#include <utility>

namespace sysycc::detail {

SemanticContext::SemanticContext(CompilerContext &compiler_context,
                                 std::unique_ptr<SemanticModel> semantic_model)
    : compiler_context_(compiler_context),
      semantic_model_(std::move(semantic_model)) {}

CompilerContext &SemanticContext::get_compiler_context() noexcept {
    return compiler_context_;
}

SemanticModel &SemanticContext::get_semantic_model() noexcept {
    return *semantic_model_;
}

const SemanticModel &SemanticContext::get_semantic_model() const noexcept {
    return *semantic_model_;
}

std::unique_ptr<SemanticModel> SemanticContext::release_semantic_model() {
    return std::move(semantic_model_);
}

const FunctionDecl *SemanticContext::get_current_function() const noexcept {
    return current_function_;
}

void SemanticContext::set_current_function(
    const FunctionDecl *current_function) noexcept {
    current_function_ = current_function;
}

const SemanticType *SemanticContext::get_current_return_type() const noexcept {
    return current_return_type_;
}

void SemanticContext::set_current_return_type(
    const SemanticType *current_return_type) noexcept {
    current_return_type_ = current_return_type;
}

int SemanticContext::get_loop_depth() const noexcept { return loop_depth_; }

void SemanticContext::enter_loop() noexcept { ++loop_depth_; }

void SemanticContext::leave_loop() noexcept {
    if (loop_depth_ > 0) {
        --loop_depth_;
    }
}

int SemanticContext::get_switch_depth() const noexcept { return switch_depth_; }

void SemanticContext::enter_switch() noexcept {
    ++switch_depth_;
    switch_frames_.push_back(SwitchFrame{});
}

void SemanticContext::leave_switch() noexcept {
    if (switch_depth_ > 0) {
        --switch_depth_;
    }
    if (!switch_frames_.empty()) {
        switch_frames_.pop_back();
    }
}

bool SemanticContext::record_case_value(long long value) noexcept {
    if (switch_frames_.empty()) {
        return false;
    }
    return switch_frames_.back().case_values.insert(value).second;
}

bool SemanticContext::record_default_label() noexcept {
    if (switch_frames_.empty()) {
        return false;
    }
    if (switch_frames_.back().has_default) {
        return false;
    }
    switch_frames_.back().has_default = true;
    return true;
}

std::optional<long long> SemanticContext::get_current_switch_case_count() const
    noexcept {
    if (switch_frames_.empty()) {
        return std::nullopt;
    }
    return static_cast<long long>(switch_frames_.back().case_values.size());
}

} // namespace sysycc::detail
