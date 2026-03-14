#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

class ParserDriver {
  public:
    PassResult Run(CompilerContext &context) const;
};

class ParserPass : public Pass {
  private:
    ParserDriver parser_driver_;

  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
