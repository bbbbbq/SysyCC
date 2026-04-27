#pragma once

#include <memory>
#include <vector>

#include "compiler/compiler_option.hpp"
#include "compiler/pass/pass_result.hpp"

namespace sysycc {

class CompilerContext;
class FrontendDialect;
class Pass;
class PassManager;

// Owns the compiler pipeline and drives all registered passes.
class Compiler {
  private:
    CompilerOption option_;
    std::unique_ptr<CompilerContext> context_;
    std::unique_ptr<PassManager> pass_manager_;
    std::vector<std::unique_ptr<FrontendDialect>> extra_dialects_;
    bool pipeline_initialized_ = false;

    void InitializePasses();
    void sync_context_from_option();
    PassResult validate_driver_configuration();
    PassResult validate_dialect_configuration();
    PassResult validate_backend_configuration();

  public:
    Compiler();
    explicit Compiler(CompilerOption option);
    ~Compiler();

    Compiler(const Compiler &) = delete;
    Compiler &operator=(const Compiler &) = delete;
    Compiler(Compiler &&) noexcept;
    Compiler &operator=(Compiler &&) noexcept;

    void set_option(CompilerOption option);
    const CompilerOption &get_option() const noexcept;

    CompilerContext &get_context() noexcept;
    const CompilerContext &get_context() const noexcept;

    void register_dialect(std::unique_ptr<FrontendDialect> dialect);
    void AddPass(std::unique_ptr<Pass> pass);
    PassResult Run();
};

} // namespace sysycc
