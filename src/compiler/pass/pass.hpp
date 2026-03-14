#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

enum class PassKind { Lex, Parse, Semantic, IRGen, CodeGen };

struct PassResult {
    bool ok = true;
    std::string message;

    static PassResult Success() { return {true, ""}; }

    static PassResult Failure(std::string msg) {
        return {false, std::move(msg)};
    }
};

class Pass {
  public:
    virtual ~Pass() = default;
    virtual PassKind Kind() const = 0;
    virtual const char *Name() const = 0;
    virtual PassResult Run(CompilerContext &context) = 0;
};

class PassManager {
  private:
    std::vector<std::unique_ptr<Pass>> passes_;

  public:
    void AddPass(std::unique_ptr<Pass> pass);
    PassManager() = default;
    Pass *get_pass_by_kind(PassKind kind) const;
    PassResult Run(CompilerContext &context);
};

} // namespace sysycc
