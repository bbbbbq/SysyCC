#pragma once

#include <optional>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
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

struct GotoReference {
    std::string label_name;
    SourceSpan source_span;
};

struct LabelDefinition {
    std::string label_name;
    SourceSpan source_span;
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
    std::vector<const SemanticSymbol *> function_local_symbols_;
    std::unordered_set<std::string> defined_labels_;
    std::vector<LabelDefinition> defined_label_definitions_;
    std::unordered_set<std::string> referenced_labels_;
    std::vector<GotoReference> goto_references_;

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

    void begin_function_labels() noexcept;
    void end_function_labels() noexcept;
    void record_function_local_symbol(const SemanticSymbol *symbol);
    const std::vector<const SemanticSymbol *> &get_function_local_symbols() const
        noexcept;
    bool record_label_definition(const std::string &label_name,
                                 const SourceSpan &source_span);
    void record_goto_reference(std::string label_name,
                               SourceSpan source_span);
    std::vector<GotoReference> get_undefined_goto_references() const;
    std::vector<LabelDefinition> get_unused_label_definitions() const;

    bool is_system_header_path(const std::string &file_path) const;
    bool is_system_header_span(const SourceSpan &source_span) const;
};

} // namespace detail
} // namespace sysycc
