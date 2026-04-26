#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"

#include <algorithm>
#include <cctype>

namespace sysycc {

std::string llvm_import_trim_copy(const std::string &text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool llvm_import_starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool llvm_import_is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' ||
           ch == '.' || ch == '$' || ch == '-';
}

std::string llvm_import_strip_comment(const std::string &line) {
    bool in_quote = false;
    bool quote_escape = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (in_quote) {
            if (quote_escape) {
                quote_escape = false;
                continue;
            }
            if (ch == '\\') {
                quote_escape = true;
                continue;
            }
            if (ch == '"') {
                in_quote = false;
            }
            continue;
        }
        if (ch == '"') {
            in_quote = true;
            continue;
        }
        if (ch == ';') {
            return llvm_import_trim_copy(line.substr(0, index));
        }
    }
    return llvm_import_trim_copy(line);
}

std::optional<std::string>
llvm_import_unquote_string_literal(const std::string &text) {
    const std::string trimmed = llvm_import_trim_copy(text);
    if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"') {
        return std::nullopt;
    }

    std::string result;
    for (std::size_t index = 1; index + 1 < trimmed.size(); ++index) {
        char ch = trimmed[index];
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (index + 1 >= trimmed.size() - 1) {
            return std::nullopt;
        }
        const char escaped = trimmed[++index];
        switch (escaped) {
        case '\\':
        case '"':
            result.push_back(escaped);
            break;
        case 'n':
            result.push_back('\n');
            break;
        case 't':
            result.push_back('\t');
            break;
        default:
            result.push_back(escaped);
            break;
        }
    }
    return result;
}

std::vector<std::string> llvm_import_split_top_level(const std::string &text,
                                                     char delimiter) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    int square_depth = 0;
    int brace_depth = 0;
    int paren_depth = 0;
    int angle_depth = 0;
    bool in_quote = false;
    bool quote_escape = false;

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (in_quote) {
            if (quote_escape) {
                quote_escape = false;
                continue;
            }
            if (text[index] == '\\') {
                quote_escape = true;
                continue;
            }
            if (text[index] == '"') {
                in_quote = false;
            }
            continue;
        }

        switch (text[index]) {
        case '"':
            in_quote = true;
            break;
        case '[':
            ++square_depth;
            break;
        case ']':
            --square_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            --brace_depth;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            --paren_depth;
            break;
        case '<':
            ++angle_depth;
            break;
        case '>':
            --angle_depth;
            break;
        default:
            break;
        }

        if (text[index] == delimiter && square_depth == 0 && brace_depth == 0 &&
            paren_depth == 0 && angle_depth == 0) {
            parts.push_back(llvm_import_trim_copy(text.substr(begin, index - begin)));
            begin = index + 1;
        }
    }

    parts.push_back(llvm_import_trim_copy(text.substr(begin)));
    return parts;
}

std::string
llvm_import_strip_trailing_alignment_suffix(const std::string &text) {
    std::size_t square_depth = 0;
    std::size_t brace_depth = 0;
    std::size_t paren_depth = 0;
    bool in_quote = false;
    bool quote_escape = false;
    for (std::size_t index = 0; index + 8 < text.size(); ++index) {
        if (in_quote) {
            if (quote_escape) {
                quote_escape = false;
                continue;
            }
            if (text[index] == '\\') {
                quote_escape = true;
                continue;
            }
            if (text[index] == '"') {
                in_quote = false;
            }
            continue;
        }
        switch (text[index]) {
        case '"':
            in_quote = true;
            break;
        case '[':
            ++square_depth;
            break;
        case ']':
            --square_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            --brace_depth;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            --paren_depth;
            break;
        default:
            break;
        }
        if (square_depth == 0 && brace_depth == 0 && paren_depth == 0 &&
            llvm_import_starts_with(std::string_view(text).substr(index),
                                    ", align ")) {
            return llvm_import_trim_copy(text.substr(0, index));
        }
    }
    return llvm_import_trim_copy(text);
}

std::optional<std::string>
llvm_import_consume_type_token(const std::string &text, std::size_t &position) {
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
    if (position >= text.size()) {
        return std::nullopt;
    }

    const std::size_t start = position;
    if (llvm_import_starts_with(std::string_view(text).substr(position), "void")) {
        position += 4;
        return text.substr(start, position - start);
    }
    if (llvm_import_starts_with(std::string_view(text).substr(position), "ptr")) {
        position += 3;
        const std::size_t after_ptr = position;
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position])) != 0) {
            ++position;
        }
        if (llvm_import_starts_with(std::string_view(text).substr(position),
                                    "addrspace(")) {
            position += 10;
            while (position < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
                ++position;
            }
            if (position >= text.size() || text[position] != ')') {
                return std::nullopt;
            }
            ++position;
            return text.substr(start, position - start);
        }
        position = after_ptr;
        return text.substr(start, position - start);
    }
    if (llvm_import_starts_with(std::string_view(text).substr(position), "half")) {
        position += 4;
        return text.substr(start, position - start);
    }
    if (llvm_import_starts_with(std::string_view(text).substr(position),
                                "float")) {
        position += 5;
        return text.substr(start, position - start);
    }
    if (llvm_import_starts_with(std::string_view(text).substr(position),
                                "double")) {
        position += 6;
        return text.substr(start, position - start);
    }
    if (llvm_import_starts_with(std::string_view(text).substr(position),
                                "fp128")) {
        position += 5;
        return text.substr(start, position - start);
    }
    if (text[position] == '%') {
        ++position;
        while (position < text.size() &&
               llvm_import_is_identifier_char(text[position])) {
            ++position;
        }
        return text.substr(start, position - start);
    }
    if (text[position] == 'i') {
        ++position;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
            ++position;
        }
        return text.substr(start, position - start);
    }
    if (text[position] == '[' || text[position] == '{' || text[position] == '<') {
        const char open = text[position];
        const char close = open == '[' ? ']' : (open == '{' ? '}' : '>');
        int depth = 0;
        do {
            if (text[position] == open) {
                ++depth;
            } else if (text[position] == close) {
                --depth;
            }
            ++position;
        } while (position < text.size() && depth > 0);
        return text.substr(start, position - start);
    }

    return std::nullopt;
}

std::optional<std::string> llvm_import_parse_symbol_name(const std::string &text,
                                                         std::size_t &position,
                                                         char prefix) {
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
    if (position >= text.size() || text[position] != prefix) {
        return std::nullopt;
    }
    ++position;
    const std::size_t start = position;
    while (position < text.size() &&
           llvm_import_is_identifier_char(text[position])) {
        ++position;
    }
    if (position == start) {
        return std::nullopt;
    }
    return text.substr(start, position - start);
}

bool llvm_import_is_modifier_token(const std::string &token) {
    static const std::vector<std::string> keywords = {
        "dso_local",         "local_unnamed_addr", "unnamed_addr",
        "noundef",           "nocapture",          "nonnull",
        "noalias",           "readonly",           "readnone",
        "writeonly",         "returned",           "signext",
        "zeroext",           "inreg",              "immarg",
        "volatile",          "atomic",
        "writable",          "dead_on_unwind",
        "swiftself",         "swifterror",         "sret",
        "byval",             "align",              "dereferenceable",
        "dereferenceable_or_null",                 "nonnull",
        "nuw",               "nsw",                "nusw",
        "exact",             "disjoint",           "samesign",
        "nneg",
        "tail",              "musttail",           "notail",
        "fast",              "coldcc",             "fastcc",
        "ccc",               "internal",           "private",
        "external",          "extern_weak",        "weak",
        "linkonce",
        "linkonce_odr",      "available_externally", "common",
        "comdat",            "any",                "hidden",
    };
    for (const std::string &keyword : keywords) {
        if (token == keyword) {
            return true;
        }
    }
    return llvm_import_starts_with(token, "#") ||
           llvm_import_starts_with(token, "align") ||
           llvm_import_starts_with(token, "addrspace(") ||
           llvm_import_starts_with(token, "byval(") ||
           llvm_import_starts_with(token, "captures(") ||
           llvm_import_starts_with(token, "sret(") ||
           llvm_import_starts_with(token, "memory(") ||
           llvm_import_starts_with(token, "nofpclass(") ||
           llvm_import_starts_with(token, "range(") ||
           llvm_import_starts_with(token, "dereferenceable(") ||
           llvm_import_starts_with(token, "dereferenceable_or_null(");
}

std::optional<std::size_t>
llvm_import_consume_parenthesized_modifier_prefix(const std::string &text) {
    static const std::vector<std::string> prefixes = {
        "addrspace(", "byval(", "sret(", "dereferenceable(",
        "dereferenceable_or_null(", "elementtype(", "captures(",
        "memory(", "nofpclass(", "range(",
    };
    for (const std::string &prefix : prefixes) {
        if (!llvm_import_starts_with(text, prefix)) {
            continue;
        }
        std::size_t position = prefix.size();
        int depth = 1;
        while (position < text.size() && depth > 0) {
            if (text[position] == '(') {
                ++depth;
            } else if (text[position] == ')') {
                --depth;
            }
            ++position;
        }
        if (depth == 0) {
            return position;
        }
    }
    return std::nullopt;
}

std::string llvm_import_strip_leading_modifiers(const std::string &text) {
    std::string current = llvm_import_trim_copy(text);
    while (!current.empty()) {
        if (const auto parenthesized_modifier_end =
                llvm_import_consume_parenthesized_modifier_prefix(current);
            parenthesized_modifier_end.has_value()) {
            current = llvm_import_trim_copy(
                current.substr(*parenthesized_modifier_end));
            continue;
        }
        std::size_t token_end = 0;
        while (token_end < current.size() &&
               std::isspace(static_cast<unsigned char>(current[token_end])) == 0) {
            ++token_end;
        }
        const std::string token = current.substr(0, token_end);
        if (!llvm_import_is_modifier_token(token)) {
            break;
        }
        current = llvm_import_trim_copy(current.substr(token_end));
        if (token == "align") {
            std::size_t align_value_end = 0;
            while (align_value_end < current.size() &&
                   std::isspace(static_cast<unsigned char>(current[align_value_end])) == 0) {
                ++align_value_end;
            }
            const std::string align_value = current.substr(0, align_value_end);
            if (!align_value.empty() &&
                std::all_of(align_value.begin(), align_value.end(),
                            [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                current = llvm_import_trim_copy(current.substr(align_value_end));
            }
        }
    }
    return current;
}

std::string llvm_import_strip_metadata_suffix(const std::string &text) {
    std::size_t square_depth = 0;
    std::size_t brace_depth = 0;
    std::size_t paren_depth = 0;
    for (std::size_t index = 0; index + 2 < text.size(); ++index) {
        switch (text[index]) {
        case '[':
            ++square_depth;
            break;
        case ']':
            --square_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            --brace_depth;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            --paren_depth;
            break;
        default:
            break;
        }
        if (square_depth == 0 && brace_depth == 0 && paren_depth == 0 &&
            text[index] == ',' && text[index + 1] == ' ' &&
            text[index + 2] == '!') {
            return llvm_import_trim_copy(text.substr(0, index));
        }
    }
    return llvm_import_trim_copy(text);
}

std::optional<std::uint64_t>
llvm_import_parse_integer_literal(const std::string &text) {
    const std::string trimmed = llvm_import_trim_copy(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    if (trimmed == "true") {
        return static_cast<std::uint64_t>(1);
    }
    if (trimmed == "false") {
        return static_cast<std::uint64_t>(0);
    }

    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(trimmed, &consumed, 0);
        if (consumed != trimmed.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace sysycc
