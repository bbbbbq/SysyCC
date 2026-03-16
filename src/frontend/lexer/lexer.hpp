#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

class LexerPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
