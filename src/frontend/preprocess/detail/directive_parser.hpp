#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "compiler/pass/pass.hpp"

namespace sysycc::preprocess::detail {

enum class DirectiveKind : uint8_t {
    Define,
    Undef,
    Include,
    IncludeNext,
    Error,
    Pragma,
    Line,
    Ifdef,
    Ifndef,
    If,
    Elif,
    Elifdef,
    Elifndef,
    Else,
    Endif,
    Unknown,
};

// Represents one parsed preprocess directive line.
class Directive {
  private:
    DirectiveKind kind_ = DirectiveKind::Unknown;
    std::string keyword_;
    std::vector<std::string> arguments_;
    bool is_function_like_macro_ = false;
    bool is_variadic_macro_ = false;
    std::vector<std::string> macro_parameters_;

  public:
    Directive() = default;

    Directive(DirectiveKind kind, std::string keyword,
              std::vector<std::string> arguments,
              bool is_function_like_macro = false,
              bool is_variadic_macro = false,
              std::vector<std::string> macro_parameters = {})
        : kind_(kind), keyword_(std::move(keyword)),
          arguments_(std::move(arguments)),
          is_function_like_macro_(is_function_like_macro),
          is_variadic_macro_(is_variadic_macro),
          macro_parameters_(std::move(macro_parameters)) {}

    DirectiveKind get_kind() const noexcept { return kind_; }
    const std::string &get_keyword() const noexcept { return keyword_; }
    const std::vector<std::string> &get_arguments() const noexcept {
        return arguments_;
    }
    bool get_is_function_like_macro() const noexcept {
        return is_function_like_macro_;
    }
    bool get_is_variadic_macro() const noexcept { return is_variadic_macro_; }
    const std::vector<std::string> &get_macro_parameters() const noexcept {
        return macro_parameters_;
    }
};

// Parses raw preprocess directive lines into structured directives.
class DirectiveParser {
  public:
    bool is_directive(const std::string &line) const;
    PassResult parse(const std::string &line, Directive &directive) const;
};

} // namespace sysycc::preprocess::detail
