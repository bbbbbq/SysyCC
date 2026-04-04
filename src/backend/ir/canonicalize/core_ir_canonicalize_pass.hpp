#pragma once

#include "compiler/pass/pass.hpp"

namespace sysycc {

// Core IR 规范化 pass：
// 在常量折叠和 DCE 之前，把 IR 先整理成更稳定、更统一的形状。
class CoreIrCanonicalizePass : public Pass {
  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
