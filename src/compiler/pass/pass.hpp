#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass_result.hpp"

namespace sysycc {

class CoreIrFunction;

// Defines the common interface that every compiler pass must implement.
class Pass {
  public:
    virtual ~Pass() = default;
    virtual PassKind Kind() const = 0;
    virtual const char *Name() const = 0;
    virtual CoreIrPassMetadata Metadata() const noexcept { return {}; }
    virtual PassResult Run(CompilerContext &context) = 0;
};

// Owns pass objects and runs them in pipeline order.
class PassManager {
  private:
    struct FixedPointPassGroup {
        std::vector<std::unique_ptr<Pass>> passes;
        std::size_t max_iterations = 4;
        bool module_scope = false;
    };

    struct PipelineEntry {
        std::unique_ptr<Pass> pass;
        std::optional<FixedPointPassGroup> fixed_point_group;
    };

    std::vector<PipelineEntry> entries_;

  public:
    void AddPass(std::unique_ptr<Pass> pass);
    void AddCoreIrFixedPointGroup(std::vector<std::unique_ptr<Pass>> passes,
                                  std::size_t max_iterations = 4);
    void
    AddCoreIrModuleFixedPointGroup(std::vector<std::unique_ptr<Pass>> passes,
                                   std::size_t max_iterations = 4);
    PassManager() = default;
    Pass *get_pass_by_kind(PassKind kind) const;
    std::vector<PassKind> get_pipeline_kinds() const;
    PassResult Run(CompilerContext &context);
};

} // namespace sysycc
