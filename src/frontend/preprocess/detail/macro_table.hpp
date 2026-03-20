#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/source_span.hpp"
#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

// Describes one object-like or function-like macro definition in the
// preprocessor.
class MacroDefinition {
  private:
    std::string name_;
    std::string replacement_;
    bool is_function_like_ = false;
    std::vector<std::string> parameters_;
    SourceSpan source_span_;

  public:
    MacroDefinition() = default;

    MacroDefinition(std::string name, std::string replacement,
                    bool is_function_like = false,
                    std::vector<std::string> parameters = {},
                    SourceSpan source_span = {})
        : name_(std::move(name)), replacement_(std::move(replacement)),
          is_function_like_(is_function_like),
          parameters_(std::move(parameters)),
          source_span_(source_span) {}

    const std::string &get_name() const noexcept { return name_; }

    void set_name(std::string name) { name_ = std::move(name); }

    const std::string &get_replacement() const noexcept { return replacement_; }

    void set_replacement(std::string replacement) {
        replacement_ = std::move(replacement);
    }

    bool get_is_function_like() const noexcept { return is_function_like_; }

    void set_is_function_like(bool is_function_like) noexcept {
        is_function_like_ = is_function_like;
    }

    const std::vector<std::string> &get_parameters() const noexcept {
        return parameters_;
    }

    void set_parameters(std::vector<std::string> parameters) {
        parameters_ = std::move(parameters);
    }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    void set_source_span(SourceSpan source_span) {
        source_span_ = source_span;
    }
};

// Owns macro definitions for one preprocessing session.
class MacroTable {
  private:
    std::unordered_map<std::string, MacroDefinition> macro_definitions_;

  public:
    void clear();
    bool has_macro(const std::string &name) const;
    const MacroDefinition *
    get_macro_definition(const std::string &name) const noexcept;
    PassResult define_macro(const MacroDefinition &definition);
    void undefine_macro(const std::string &name);
};

} // namespace sysycc::preprocess::detail
