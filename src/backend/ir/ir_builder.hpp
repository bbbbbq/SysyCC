#pragma once

#include <memory>

namespace sysycc {

class CompilerContext;
class IRBackend;
class IRResult;

// Coordinates IR generation while staying backend-independent.
class IRBuilder {
  private:
    IRBackend &backend_;

  public:
    explicit IRBuilder(IRBackend &backend) : backend_(backend) {}

    std::unique_ptr<IRResult> Build(const CompilerContext &context);
};

} // namespace sysycc
