#pragma once

#include <memory>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"
#include "frontend/dialects/core/dialect.hpp"

namespace sysycc {

// Owns the compiler pipeline and drives all registered passes.
class Complier {
  private:
    ComplierOption option_;
    CompilerContext context_;
    PassManager pass_manager_;
    std::vector<std::unique_ptr<FrontendDialect>> extra_dialects_;
    bool pipeline_initialized_ = false;

    void InitializePasses();
    PassResult validate_dialect_configuration();

  public:
    Complier() = default;
    explicit Complier(ComplierOption option);

    void set_option(ComplierOption option);
    const ComplierOption &get_option() const noexcept;

    CompilerContext &get_context() noexcept;
    const CompilerContext &get_context() const noexcept;

    void register_dialect(std::unique_ptr<FrontendDialect> dialect);
    void AddPass(std::unique_ptr<Pass> pass);
    PassResult Run();
};

} // namespace sysycc
