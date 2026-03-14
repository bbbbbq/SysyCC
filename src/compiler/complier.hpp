#pragma once

#include "compiler/complier_option.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass.hpp"

namespace sysycc {

class Complier {
  private:
    ComplierOption option_;
    CompilerContext context_;
    PassManager pass_manager_;
    bool pipeline_initialized_ = false;

    void InitializePasses();

  public:
    Complier() = default;
    explicit Complier(ComplierOption option);

    void set_option(ComplierOption option);
    const ComplierOption &get_option() const noexcept;

    CompilerContext &get_context() noexcept;
    const CompilerContext &get_context() const noexcept;

    void AddPass(std::unique_ptr<Pass> pass);
    PassResult Run();
};

} // namespace sysycc
