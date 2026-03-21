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
    Warning,
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
    bool has_text_payload_ = false;
    std::string text_payload_;
    bool has_line_number_ = false;
    int line_number_ = 0;
    std::string line_file_path_;

  public:
    Directive() = default;

    Directive(DirectiveKind kind, std::string keyword,
              std::vector<std::string> arguments,
              bool is_function_like_macro = false,
              bool is_variadic_macro = false,
              std::vector<std::string> macro_parameters = {},
              bool has_text_payload = false,
              std::string text_payload = {},
              bool has_line_number = false, int line_number = 0,
              std::string line_file_path = {})
        : kind_(kind), keyword_(std::move(keyword)),
          arguments_(std::move(arguments)),
          is_function_like_macro_(is_function_like_macro),
          is_variadic_macro_(is_variadic_macro),
          macro_parameters_(std::move(macro_parameters)),
          has_text_payload_(has_text_payload),
          text_payload_(std::move(text_payload)),
          has_line_number_(has_line_number), line_number_(line_number),
          line_file_path_(std::move(line_file_path)) {}

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
    bool get_has_text_payload() const noexcept { return has_text_payload_; }
    const std::string &get_text_payload() const noexcept { return text_payload_; }
    bool get_has_line_number() const noexcept { return has_line_number_; }
    int get_line_number() const noexcept { return line_number_; }
    const std::string &get_line_file_path() const noexcept {
        return line_file_path_;
    }
};

// Parses raw preprocess directive lines into structured directives.
class DirectiveParser {
  public:
    bool is_directive(const std::string &line) const;
    PassResult parse(const std::string &line, Directive &directive,
                     bool validate_syntax = true) const;
};

} // namespace sysycc::preprocess::detail
