#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

struct Diagnostic {
    std::string stage;
    int line = -1;
    int column = -1;
    std::string message;
};

struct PassStatus {
    bool ok = true;
    std::string message;

    static PassStatus Success() { return {true, ""}; }

    static PassStatus Failure(std::string message) {
        return {false, std::move(message)};
    }
};

// Defines the minimal legacy pass interface kept for compatibility.
class Pass {
  public:
    virtual ~Pass() = default;
    virtual const char *Name() const = 0;
    virtual PassStatus Run(CompilerContext &context) = 0;
};

// Runs the legacy pass sequence defined under src/pass.
class PassManager {
  private:
    std::vector<std::unique_ptr<Pass>> passes_;

  public:
    void AddPass(std::unique_ptr<Pass> pass);
    PassStatus Run(CompilerContext &context);
};

} // namespace sysycc
