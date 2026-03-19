#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

// Runs semantic analysis over the lowered AST and records semantic results.
class SemanticPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
