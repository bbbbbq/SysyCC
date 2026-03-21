#include <cassert>
#include <memory>
#include <string_view>

#include "compiler/complier.hpp"
#include "frontend/dialects/dialect.hpp"

using namespace sysycc;

namespace {

class FirstConflictDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "first-conflict-dialect";
    }

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const override {
        registry.add_keyword("shared_keyword", TokenKind::KwInt);
    }
};

class SecondConflictDialect : public FrontendDialect {
  public:
    std::string_view get_name() const noexcept override {
        return "second-conflict-dialect";
    }

    void contribute_lexer_keywords(LexerKeywordRegistry &registry) const override {
        registry.add_keyword("shared_keyword", TokenKind::KwFloat);
    }
};

} // namespace

int main() {
    Complier complier;
    complier.register_dialect(std::make_unique<FirstConflictDialect>());
    complier.register_dialect(std::make_unique<SecondConflictDialect>());

    PassResult result = complier.Run();
    assert(!result.ok);
    assert(result.message.find("invalid dialect configuration") !=
           std::string::npos);

    const auto &diagnostics =
        complier.get_context().get_diagnostic_engine().get_diagnostics();
    assert(!diagnostics.empty());
    assert(diagnostics.front().get_stage() == DiagnosticStage::Compiler);
    assert(diagnostics.front().get_message().find("invalid dialect configuration") !=
           std::string::npos);
    assert(diagnostics.size() >= 2);
    assert(diagnostics[1].get_message().find("second-conflict-dialect") !=
           std::string::npos);
    return 0;
}
