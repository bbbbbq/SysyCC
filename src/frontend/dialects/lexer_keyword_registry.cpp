#include "frontend/dialects/lexer_keyword_registry.hpp"

#include "compiler/compiler_context/compiler_context.hpp"

#include <string>
#include <string_view>

namespace sysycc {

namespace {

std::string get_token_kind_name(TokenKind kind) {
    return Token(kind, "").get_kind_name();
}

} // namespace

void LexerKeywordRegistry::add_keyword(std::string text, TokenKind kind) {
    const auto iterator = keywords_.find(text);
    if (iterator == keywords_.end()) {
        keywords_.emplace(std::move(text), kind);
        return;
    }

    if (iterator->second == kind) {
        return;
    }

    conflicts_.push_back("keyword '" + text + "' already maps to " +
                         get_token_kind_name(iterator->second) +
                         ", cannot also map to " + get_token_kind_name(kind));
}

bool LexerKeywordRegistry::has_keyword(std::string_view text) const noexcept {
    return keywords_.find(text) != keywords_.end();
}

TokenKind LexerKeywordRegistry::get_keyword_kind(
    std::string_view text) const noexcept {
    const auto iterator = keywords_.find(text);
    if (iterator == keywords_.end()) {
        return TokenKind::Invalid;
    }
    return iterator->second;
}

const std::map<std::string, TokenKind, std::less<>> &
LexerKeywordRegistry::get_keywords() const noexcept {
    return keywords_;
}

const std::vector<std::string> &LexerKeywordRegistry::get_conflicts() const
    noexcept {
    return conflicts_;
}

} // namespace sysycc
