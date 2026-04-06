#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

class CoreIrIndVarSimplifyPass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    CoreIrPassMetadata Metadata() const noexcept override {
        return CoreIrPassMetadata::core_ir_transform();
    }
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc

