#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

class CoreIrIpsccpPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    CoreIrPassMetadata Metadata() const noexcept override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
