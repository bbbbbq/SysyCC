#include <cassert>
#include <memory>
#include <string>

#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/dialects/dialect.hpp"
#include "frontend/dialects/dialect_manager.hpp"

using namespace sysycc;

namespace {

class FirstKeywordDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-keyword-owner";
    }

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const
        override {
        registry.add_keyword("shared_keyword", TokenKind::KwInt);
        registry.add_keyword("same_keyword", TokenKind::KwSigned);
    }
};

class SecondKeywordDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-keyword-owner";
    }

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const
        override {
        registry.add_keyword("shared_keyword", TokenKind::KwFloat);
        registry.add_keyword("same_keyword", TokenKind::KwSigned);
    }
};

} // namespace

int main() {
    DialectManager dialect_manager;
    dialect_manager.register_dialect(std::make_unique<FirstKeywordDialect>());
    dialect_manager.register_dialect(std::make_unique<SecondKeywordDialect>());

    const auto &keyword_registry = dialect_manager.get_lexer_keyword_registry();
    assert(keyword_registry.get_keyword_kind("shared_keyword") ==
           TokenKind::KwInt);
    assert(keyword_registry.get_keyword_kind("same_keyword") ==
           TokenKind::KwSigned);
    assert(keyword_registry.get_conflicts().size() == 1);

    const auto &registration_errors = dialect_manager.get_registration_errors();
    assert(registration_errors.size() == 1);
    assert(registration_errors.front().find("second-keyword-owner") !=
           std::string::npos);
    assert(registration_errors.front().find("shared_keyword") !=
           std::string::npos);

    return 0;
}
