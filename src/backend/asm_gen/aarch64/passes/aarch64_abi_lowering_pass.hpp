#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrFunction;
class CoreIrCallInst;

struct AArch64HfaInfo {
    AArch64VirtualRegKind element_kind = AArch64VirtualRegKind::Float32;
    std::vector<std::size_t> member_offsets;
};

enum class AArch64AbiValueClass : unsigned char {
    GeneralScalar,
    FloatingScalar,
    GeneralComposite,
    HfaComposite,
    IndirectComposite,
    Void,
};

enum class AArch64AbiLocationKind : unsigned char {
    GeneralRegister,
    FloatingRegister,
    Stack,
    IndirectResultRegister,
};

struct AArch64AbiLocation {
    AArch64AbiLocationKind kind = AArch64AbiLocationKind::Stack;
    unsigned physical_reg = 0;
    std::size_t stack_offset = 0;
    std::size_t source_offset = 0;
    std::size_t size = 0;
    AArch64VirtualRegKind reg_kind = AArch64VirtualRegKind::General32;
};

struct AArch64AbiAssignment {
    AArch64AbiValueClass value_class = AArch64AbiValueClass::Void;
    const CoreIrType *type = nullptr;
    std::vector<AArch64AbiLocation> locations;
    std::size_t stack_size = 0;
    bool is_indirect = false;
};

struct AArch64FunctionAbiInfo {
    AArch64AbiAssignment return_value;
    std::vector<AArch64AbiAssignment> parameters;
};

class AArch64AbiLoweringPass {
  public:
    AArch64AbiAssignment classify_return(const CoreIrType *type) const;
    AArch64AbiAssignment classify_parameter(const CoreIrType *type,
                                           unsigned &next_gpr,
                                           unsigned &next_fpr,
                                           std::size_t &next_stack_offset) const;
    AArch64FunctionAbiInfo classify_function(const CoreIrFunction &function) const;
    AArch64FunctionAbiInfo classify_call(const CoreIrCallInst &call) const;

  private:
    std::optional<AArch64HfaInfo> classify_hfa(const CoreIrType *type,
                                               std::size_t base_offset = 0) const;
};

} // namespace sysycc
