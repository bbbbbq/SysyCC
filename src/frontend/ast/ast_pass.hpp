#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

// Builds the AST from the parser runtime tree and optionally dumps it.
class AstPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
