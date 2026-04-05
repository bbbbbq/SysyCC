#include "frontend/semantic/support/semantic_context.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

#include <filesystem>
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

void SemanticContext::begin_function_labels() noexcept {
    function_local_symbols_.clear();
    defined_labels_.clear();
    defined_label_definitions_.clear();
    referenced_labels_.clear();
    goto_references_.clear();
}

void SemanticContext::end_function_labels() noexcept {
    function_local_symbols_.clear();
    defined_labels_.clear();
    defined_label_definitions_.clear();
    referenced_labels_.clear();
    goto_references_.clear();
}

void SemanticContext::record_function_local_symbol(const SemanticSymbol *symbol) {
    if (symbol == nullptr) {
        return;
    }
    function_local_symbols_.push_back(symbol);
}

const std::vector<const SemanticSymbol *> &
SemanticContext::get_function_local_symbols() const noexcept {
    return function_local_symbols_;
}

bool SemanticContext::record_label_definition(const std::string &label_name,
                                              const SourceSpan &source_span) {
    const bool inserted = defined_labels_.insert(label_name).second;
    if (inserted) {
        defined_label_definitions_.push_back(LabelDefinition{label_name, source_span});
    }
    return inserted;
}

void SemanticContext::record_goto_reference(std::string label_name,
                                            SourceSpan source_span) {
    referenced_labels_.insert(label_name);
    goto_references_.push_back(
        GotoReference{std::move(label_name), std::move(source_span)});
}

std::vector<GotoReference> SemanticContext::get_undefined_goto_references() const {
    std::vector<GotoReference> undefined_references;
    for (const auto &reference : goto_references_) {
        if (defined_labels_.find(reference.label_name) == defined_labels_.end()) {
            undefined_references.push_back(reference);
        }
    }
    return undefined_references;
}

std::vector<LabelDefinition> SemanticContext::get_unused_label_definitions() const {
    std::vector<LabelDefinition> unused_labels;
    for (const auto &label_definition : defined_label_definitions_) {
        if (referenced_labels_.find(label_definition.label_name) !=
            referenced_labels_.end()) {
            continue;
        }
        unused_labels.push_back(label_definition);
    }
    return unused_labels;
}

bool SemanticContext::is_system_header_path(const std::string &file_path) const {
    if (file_path.empty()) {
        return false;
    }

    const std::filesystem::path normalized_file_path =
        std::filesystem::path(file_path).lexically_normal();
    for (const std::string &system_directory :
         compiler_context_.get_system_include_directories()) {
        const std::filesystem::path normalized_system_directory =
            std::filesystem::path(system_directory).lexically_normal();
        if (normalized_file_path == normalized_system_directory) {
            return true;
        }

        const std::string directory_with_separator =
            normalized_system_directory.string() +
            std::filesystem::path::preferred_separator;
        const std::string normalized_file_string = normalized_file_path.string();
        if (normalized_file_string.rfind(directory_with_separator, 0) == 0) {
            return true;
        }
    }

    return false;
}

bool SemanticContext::is_system_header_span(
    const SourceSpan &source_span) const {
    const SourceFile *source_file = source_span.get_begin().get_file();
    if (source_file == nullptr) {
        return false;
    }
    return is_system_header_path(source_file->get_path());
}

} // namespace sysycc::detail
