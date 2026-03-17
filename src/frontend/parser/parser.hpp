#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

// Runs syntax analysis and stores the parse tree into compiler context.
class ParserPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
