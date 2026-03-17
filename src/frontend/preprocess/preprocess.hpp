#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

// Adapts the preprocess stage to the compiler pass framework.
class PreprocessPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
