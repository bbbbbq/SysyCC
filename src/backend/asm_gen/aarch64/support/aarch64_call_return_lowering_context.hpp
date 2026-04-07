#pragma once

#include <cstddef>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"

namespace sysycc {

class CoreIrCallInst;

class AArch64CallReturnLoweringContext {
  public:
    virtual ~AArch64CallReturnLoweringContext() = default;

    virtual AArch64FunctionAbiInfo
    classify_call(const CoreIrCallInst &call) const = 0;
    virtual const std::vector<std::size_t> *
    lookup_indirect_call_copy_offsets(const CoreIrCallInst &call) const = 0;
    virtual const AArch64FunctionAbiInfo &function_abi_info() const = 0;
    virtual const AArch64VirtualReg &indirect_result_address() const = 0;
};

} // namespace sysycc
