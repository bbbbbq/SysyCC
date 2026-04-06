#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

struct CoreIrInstCombineStats {
    std::size_t visited_instructions = 0;
    std::size_t rewrites = 0;
};

class CoreIrInstCombinePass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    CoreIrPassMetadata Metadata() const noexcept override {
        return CoreIrPassMetadata::core_ir_transform();
    }
    PassResult Run(CompilerContext &context) override;

  private:
    CoreIrInstCombineStats last_stats_{};

    friend const CoreIrInstCombineStats &
    get_instcombine_stats_for_testing(const CoreIrInstCombinePass &pass);
};

const CoreIrInstCombineStats &
get_instcombine_stats_for_testing(const CoreIrInstCombinePass &pass);

} // namespace sysycc
