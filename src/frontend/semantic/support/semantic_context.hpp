#pragma once

#include <optional>
#include <unordered_set>
#include <memory>
#include <vector>

#include "frontend/semantic/model/semantic_model.hpp"

namespace sysycc {

class CompilerContext;
class FunctionDecl;
class SemanticType;

namespace detail {

// Tracks semantic state for one active switch statement.
struct SwitchFrame {
    std::unordered_set<long long> case_values;
    bool has_default = false;
};

// Holds transient state for one semantic analysis run.
class SemanticContext {
  private:
    CompilerContext &compiler_context_;
    std::unique_ptr<SemanticModel> semantic_model_;
    const FunctionDecl *current_function_ = nullptr;
    const SemanticType *current_return_type_ = nullptr;
    int loop_depth_ = 0;
    int switch_depth_ = 0;
    std::vector<SwitchFrame> switch_frames_;

  public:
    SemanticContext(CompilerContext &compiler_context,
                    std::unique_ptr<SemanticModel> semantic_model);

    CompilerContext &get_compiler_context() noexcept;
    SemanticModel &get_semantic_model() noexcept;
    const SemanticModel &get_semantic_model() const noexcept;
    std::unique_ptr<SemanticModel> release_semantic_model();

    const FunctionDecl *get_current_function() const noexcept;
    void set_current_function(const FunctionDecl *current_function) noexcept;

    const SemanticType *get_current_return_type() const noexcept;
    void set_current_return_type(
        const SemanticType *current_return_type) noexcept;

    int get_loop_depth() const noexcept;
    void enter_loop() noexcept;
    void leave_loop() noexcept;

    int get_switch_depth() const noexcept;
    void enter_switch() noexcept;
    void leave_switch() noexcept;

    bool record_case_value(long long value) noexcept;
    bool record_default_label() noexcept;
    std::optional<long long> get_current_switch_case_count() const noexcept;
};

} // namespace detail
} // namespace sysycc
