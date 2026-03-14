#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

class LexerDriver {
  public:
    PassResult Run(CompilerContext &context) const;
};

class LexerPass : public Pass {
  private:
    LexerDriver lexer_driver_;

  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
