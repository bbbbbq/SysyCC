#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sysycc {

std::string llvm_import_trim_copy(const std::string &text);

bool llvm_import_starts_with(std::string_view text, std::string_view prefix);

bool llvm_import_is_identifier_char(char ch);

std::string llvm_import_strip_comment(const std::string &line);

std::optional<std::string>
llvm_import_unquote_string_literal(const std::string &text);

std::vector<std::string> llvm_import_split_top_level(const std::string &text,
                                                     char delimiter);

std::string
llvm_import_strip_trailing_alignment_suffix(const std::string &text);

std::optional<std::string>
llvm_import_consume_type_token(const std::string &text, std::size_t &position);

std::optional<std::string> llvm_import_parse_symbol_name(const std::string &text,
                                                         std::size_t &position,
                                                         char prefix);

bool llvm_import_is_modifier_token(const std::string &token);

std::string llvm_import_strip_leading_modifiers(const std::string &text);

std::string llvm_import_strip_metadata_suffix(const std::string &text);

std::optional<std::uint64_t>
llvm_import_parse_integer_literal(const std::string &text);

} // namespace sysycc
