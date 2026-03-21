#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sysycc {

enum class TokenKind : uint8_t;

class LexerKeywordRegistry {
  private:
    std::map<std::string, TokenKind, std::less<>> keywords_;
    std::vector<std::string> conflicts_;

  public:
    void add_keyword(std::string text, TokenKind kind);

    bool has_keyword(std::string_view text) const noexcept;

    TokenKind get_keyword_kind(std::string_view text) const noexcept;

    const std::map<std::string, TokenKind, std::less<>> &get_keywords() const
        noexcept;

    const std::vector<std::string> &get_conflicts() const noexcept;
};

} // namespace sysycc
