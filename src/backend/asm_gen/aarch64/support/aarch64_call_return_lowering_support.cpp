#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

constexpr const char *kInlineVecSminV4I32 =
    "__sysycc_aarch64_inline_vec_smin_v4i32";
constexpr const char *kInlineVecSmaxV4I32 =
    "__sysycc_aarch64_inline_vec_smax_v4i32";
constexpr const char *kInlineVecAddV4I32 =
    "__sysycc_aarch64_inline_vec_add_v4i32";
constexpr const char *kInlineVecMulV4I32 =
    "__sysycc_aarch64_inline_vec_mul_v4i32";
constexpr const char *kInlineVecAddV4I32SplatLhs =
    "__sysycc_aarch64_inline_vec_add_v4i32_splat_lhs";
constexpr const char *kInlineVecAddV4I32SplatRhs =
    "__sysycc_aarch64_inline_vec_add_v4i32_splat_rhs";
constexpr const char *kInlineVecMulV4I32SplatLhs =
    "__sysycc_aarch64_inline_vec_mul_v4i32_splat_lhs";
constexpr const char *kInlineVecMulV4I32SplatRhs =
    "__sysycc_aarch64_inline_vec_mul_v4i32_splat_rhs";
constexpr const char *kInlineCopyV4I32 =
    "__sysycc_aarch64_inline_copy_v4i32";
constexpr const char *kInlineZeroV4I32 =
    "__sysycc_aarch64_inline_zero_v4i32";
constexpr const char *kInlineSplatLane0V4I32 =
    "__sysycc_aarch64_inline_splat_lane0_v4i32";
constexpr const char *kInlineSplatScalarV4I32 =
    "__sysycc_aarch64_inline_splat_scalar_v4i32";
constexpr const char *kInlineInsertLane0ZeroedV4I32 =
    "__sysycc_aarch64_inline_insert_lane0_zeroed_v4i32";
constexpr const char *kInlineInsertLane0V4I32 =
    "__sysycc_aarch64_inline_insert_lane0_v4i32";
constexpr const char *kInlineInsertLane1V4I32 =
    "__sysycc_aarch64_inline_insert_lane1_v4i32";
constexpr const char *kInlineInsertLane2V4I32 =
    "__sysycc_aarch64_inline_insert_lane2_v4i32";
constexpr const char *kInlineInsertLane3V4I32 =
    "__sysycc_aarch64_inline_insert_lane3_v4i32";
constexpr const char *kInlineReduceSminV4I32 =
    "__sysycc_aarch64_inline_reduce_smin_v4i32";
constexpr const char *kInlineReduceSmaxV4I32 =
    "__sysycc_aarch64_inline_reduce_smax_v4i32";
constexpr const char *kInlineReduceAddV4I32 =
    "__sysycc_aarch64_inline_reduce_add_v4i32";
constexpr const char *kInlineReduceSminPairV4I32 =
    "__sysycc_aarch64_inline_reduce_smin_pair_v4i32";
constexpr const char *kInlineReduceSmaxPairV4I32 =
    "__sysycc_aarch64_inline_reduce_smax_pair_v4i32";
constexpr const char *kInlineReduceAddPairV4I32 =
    "__sysycc_aarch64_inline_reduce_add_pair_v4i32";

AArch64MachineOperand def_v4s_operand(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand::def_vector_reg(reg, 4, 's');
}

AArch64MachineOperand use_v4s_operand(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand::use_vector_reg(reg, 4, 's');
}

AArch64MachineOperand def_v2d_operand(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand::def_vector_reg(reg, 2, 'd');
}

AArch64MachineOperand use_lane_s_operand(const AArch64VirtualReg &reg,
                                         unsigned lane) {
    return AArch64MachineOperand::use_vector_lane(reg, 's', lane);
}

AArch64MachineOperand def_lane_s_operand(const AArch64VirtualReg &reg,
                                         unsigned lane) {
    return AArch64MachineOperand::def_vector_lane(reg, 's', lane);
}

AArch64VirtualReg create_v4i32_reg(AArch64MachineFunction &function) {
    return function.create_virtual_reg(AArch64VirtualRegKind::Float128);
}

AArch64VirtualReg create_lane_scalar_reg(AArch64MachineFunction &function) {
    return function.create_virtual_reg(AArch64VirtualRegKind::Float32);
}

void append_load_v4i32(AArch64MachineBlock &machine_block,
                       const AArch64VirtualReg &target_reg,
                       const AArch64VirtualReg &address_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "ldr",
        {def_vreg_operand(target_reg),
         AArch64MachineOperand::memory_address_virtual_reg(address_reg, 0)}));
}

void append_store_v4i32(AArch64MachineBlock &machine_block,
                        const AArch64VirtualReg &source_reg,
                        const AArch64VirtualReg &address_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "str",
        {use_vreg_operand(source_reg),
         AArch64MachineOperand::memory_address_virtual_reg(address_reg, 0)}));
}

void append_v4i32_binary(AArch64MachineBlock &machine_block,
                         std::string mnemonic,
                         const AArch64VirtualReg &dst_reg,
                         const AArch64VirtualReg &lhs_reg,
                         const AArch64VirtualReg &rhs_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        std::move(mnemonic),
        {def_v4s_operand(dst_reg), use_v4s_operand(lhs_reg),
         use_v4s_operand(rhs_reg)}));
}

void append_zero_v4i32(AArch64MachineBlock &machine_block,
                       const AArch64VirtualReg &dst_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "movi", {def_v2d_operand(dst_reg), AArch64MachineOperand::immediate("#0")}));
}

void append_dup_v4i32_from_scalar(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &dst_reg,
                                  const AArch64VirtualReg &scalar_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "dup", {def_v4s_operand(dst_reg), use_vreg_operand_as(scalar_reg, false)}));
}

void append_dup_v4i32_from_lane0(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &dst_reg,
                                 const AArch64VirtualReg &lane_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "dup", {def_v4s_operand(dst_reg), use_lane_s_operand(lane_reg, 0)}));
}

void append_insert_v4i32_lane_from_scalar(AArch64MachineBlock &machine_block,
                                          AArch64MachineFunction &function,
                                          const AArch64VirtualReg &dst_reg,
                                          unsigned lane,
                                          const AArch64VirtualReg &scalar_reg) {
    const AArch64VirtualReg lane_reg = create_lane_scalar_reg(function);
    machine_block.append_instruction(AArch64MachineInstr(
        "fmov", {def_vreg_operand(lane_reg),
                  use_vreg_operand_as(scalar_reg, false)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_lane_s_operand(dst_reg, lane), use_lane_s_operand(lane_reg, 0)}));
}

bool emit_direct_fma_call(AArch64MachineBlock &machine_block,
                          AArch64AbiEmissionContext &abi_context,
                          const CoreIrCallInst &call) {
    if (!call.get_is_direct_call()) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    const auto *result_type = dynamic_cast<const CoreIrFloatType *>(call.get_type());
    if ((callee_name != "fma" && callee_name != "fmaf") || result_type == nullptr ||
        call.get_operands().size() != 3) {
        return false;
    }

    const CoreIrFloatKind float_kind = result_type->get_float_kind();
    if ((callee_name == "fma" && float_kind != CoreIrFloatKind::Float64) ||
        (callee_name == "fmaf" && float_kind != CoreIrFloatKind::Float32)) {
        return false;
    }

    AArch64VirtualReg dst_reg;
    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    AArch64VirtualReg acc_reg;
    if (!abi_context.require_canonical_vreg(&call, dst_reg) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          lhs_reg) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          rhs_reg) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[2],
                                          acc_reg)) {
        return true;
    }

    machine_block.append_instruction(AArch64MachineInstr(
        AArch64MachineOpcode::FloatMulAdd,
        {def_vreg_operand(dst_reg), use_vreg_operand(lhs_reg),
         use_vreg_operand(rhs_reg), use_vreg_operand(acc_reg)}));
    return true;
}

bool emit_native_v4i32_value_call(AArch64MachineBlock &machine_block,
                                  AArch64AbiEmissionContext &abi_context,
                                  const CoreIrCallInst &call) {
    if (!call.get_is_direct_call() || !is_i32x4_vector_type(call.get_type()) ||
        call.get_operands().size() != 2) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    const char *mnemonic = nullptr;
    if (callee_name == kInlineVecSminV4I32) {
        mnemonic = "smin";
    } else if (callee_name == kInlineVecSmaxV4I32) {
        mnemonic = "smax";
    } else if (callee_name == kInlineVecAddV4I32) {
        mnemonic = "add";
    } else if (callee_name == kInlineVecMulV4I32) {
        mnemonic = "mul";
    } else {
        return false;
    }

    AArch64VirtualReg dst_reg;
    AArch64VirtualReg lhs_reg;
    AArch64VirtualReg rhs_reg;
    if (!abi_context.require_canonical_vreg(&call, dst_reg) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          lhs_reg) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          rhs_reg)) {
        return true;
    }
    append_v4i32_binary(machine_block, mnemonic, dst_reg, lhs_reg, rhs_reg);
    return true;
}

bool emit_inline_v4i32_extremum_call(AArch64MachineBlock &machine_block,
                                     AArch64AbiEmissionContext &abi_context,
                                     const CoreIrCallInst &call,
                                     AArch64MachineFunction &function) {
    if (!call.get_is_direct_call()) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    const bool is_smin = callee_name == kInlineVecSminV4I32;
    const bool is_smax = callee_name == kInlineVecSmaxV4I32;
    if ((!is_smin && !is_smax) || !is_void_type(call.get_type()) ||
        call.get_operands().size() != 3) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg lhs_addr;
    AArch64VirtualReg rhs_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          lhs_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[2],
                                          rhs_addr)) {
        return true;
    }

    const AArch64VirtualReg lhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg rhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_load_v4i32(machine_block, lhs_vec, lhs_addr);
    append_load_v4i32(machine_block, rhs_vec, rhs_addr);
    append_v4i32_binary(machine_block, is_smin ? "smin" : "smax", dst_vec, lhs_vec,
                        rhs_vec);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_add_call(AArch64MachineBlock &machine_block,
                                AArch64AbiEmissionContext &abi_context,
                                const CoreIrCallInst &call,
                                AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || call.get_callee_name() != kInlineVecAddV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 3) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg lhs_addr;
    AArch64VirtualReg rhs_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          lhs_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[2],
                                          rhs_addr)) {
        return true;
    }

    const AArch64VirtualReg lhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg rhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_load_v4i32(machine_block, lhs_vec, lhs_addr);
    append_load_v4i32(machine_block, rhs_vec, rhs_addr);
    append_v4i32_binary(machine_block, "add", dst_vec, lhs_vec, rhs_vec);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_mul_call(AArch64MachineBlock &machine_block,
                                AArch64AbiEmissionContext &abi_context,
                                const CoreIrCallInst &call,
                                AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || call.get_callee_name() != kInlineVecMulV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 3) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg lhs_addr;
    AArch64VirtualReg rhs_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          lhs_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[2],
                                          rhs_addr)) {
        return true;
    }

    const AArch64VirtualReg lhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg rhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_load_v4i32(machine_block, lhs_vec, lhs_addr);
    append_load_v4i32(machine_block, rhs_vec, rhs_addr);
    append_v4i32_binary(machine_block, "mul", dst_vec, lhs_vec, rhs_vec);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_binary_splat_call(AArch64MachineBlock &machine_block,
                                         AArch64AbiEmissionContext &abi_context,
                                         const CoreIrCallInst &call,
                                         const char *callee_name,
                                         std::string_view mnemonic,
                                         bool splat_lhs,
                                         AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || call.get_callee_name() != callee_name ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 3) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg vector_addr;
    AArch64VirtualReg scalar_reg;
    const std::size_t vector_index = splat_lhs ? 2 : 1;
    const std::size_t scalar_index = splat_lhs ? 1 : 2;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block,
                                          call.get_operands()[vector_index],
                                          vector_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block,
                                          call.get_operands()[scalar_index],
                                          scalar_reg)) {
        return true;
    }

    const AArch64VirtualReg splat_vec = create_v4i32_reg(function);
    const AArch64VirtualReg memory_vec = create_v4i32_reg(function);
    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_dup_v4i32_from_scalar(machine_block, splat_vec, scalar_reg);
    append_load_v4i32(machine_block, memory_vec, vector_addr);
    append_v4i32_binary(machine_block, std::string(mnemonic), dst_vec,
                        splat_lhs ? splat_vec : memory_vec,
                        splat_lhs ? memory_vec : splat_vec);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_copy_call(AArch64MachineBlock &machine_block,
                                 AArch64AbiEmissionContext &abi_context,
                                 const CoreIrCallInst &call,
                                 AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || call.get_callee_name() != kInlineCopyV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 2) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg src_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          src_addr)) {
        return true;
    }

    const AArch64VirtualReg vec = create_v4i32_reg(function);
    append_load_v4i32(machine_block, vec, src_addr);
    append_store_v4i32(machine_block, vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_zero_call(AArch64MachineBlock &machine_block,
                                 AArch64AbiEmissionContext &abi_context,
                                 const CoreIrCallInst &call,
                                 AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || call.get_callee_name() != kInlineZeroV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 1) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr)) {
        return true;
    }

    const AArch64VirtualReg vec = create_v4i32_reg(function);
    append_zero_v4i32(machine_block, vec);
    append_store_v4i32(machine_block, vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_splat_lane0_call(AArch64MachineBlock &machine_block,
                                        AArch64AbiEmissionContext &abi_context,
                                        const CoreIrCallInst &call,
                                        AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() ||
        call.get_callee_name() != kInlineSplatLane0V4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 2) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg src_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          src_addr)) {
        return true;
    }

    const AArch64VirtualReg lane_reg = create_lane_scalar_reg(function);
    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    machine_block.append_instruction(AArch64MachineInstr(
        "ldr",
        {def_vreg_operand(lane_reg),
         AArch64MachineOperand::memory_address_virtual_reg(src_addr, 0)}));
    append_dup_v4i32_from_lane0(machine_block, dst_vec, lane_reg);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_splat_scalar_call(AArch64MachineBlock &machine_block,
                                         AArch64AbiEmissionContext &abi_context,
                                         const CoreIrCallInst &call,
                                         AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() ||
        call.get_callee_name() != kInlineSplatScalarV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 2) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg scalar_reg;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          scalar_reg)) {
        return true;
    }

    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_dup_v4i32_from_scalar(machine_block, dst_vec, scalar_reg);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_insert_lane0_zeroed_call(
    AArch64MachineBlock &machine_block,
    AArch64AbiEmissionContext &abi_context,
    const CoreIrCallInst &call,
    AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() ||
        call.get_callee_name() != kInlineInsertLane0ZeroedV4I32 ||
        !is_void_type(call.get_type()) || call.get_operands().size() != 2) {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg scalar_reg;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          scalar_reg)) {
        return true;
    }

    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_zero_v4i32(machine_block, dst_vec);
    append_insert_v4i32_lane_from_scalar(machine_block, function, dst_vec, 0,
                                         scalar_reg);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_insert_lane_call(AArch64MachineBlock &machine_block,
                                        AArch64AbiEmissionContext &abi_context,
                                        const CoreIrCallInst &call,
                                        AArch64MachineFunction &function) {
    if (!call.get_is_direct_call() || !is_void_type(call.get_type()) ||
        call.get_operands().size() != 3) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    int lane = -1;
    if (callee_name == kInlineInsertLane0V4I32) {
        lane = 0;
    } else if (callee_name == kInlineInsertLane1V4I32) {
        lane = 1;
    } else if (callee_name == kInlineInsertLane2V4I32) {
        lane = 2;
    } else if (callee_name == kInlineInsertLane3V4I32) {
        lane = 3;
    } else {
        return false;
    }

    AArch64VirtualReg dst_addr;
    AArch64VirtualReg src_addr;
    AArch64VirtualReg scalar_reg;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          dst_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          src_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[2],
                                          scalar_reg)) {
        return true;
    }

    const AArch64VirtualReg dst_vec = create_v4i32_reg(function);
    append_load_v4i32(machine_block, dst_vec, src_addr);
    append_insert_v4i32_lane_from_scalar(
        machine_block, function, dst_vec, static_cast<unsigned>(lane), scalar_reg);
    append_store_v4i32(machine_block, dst_vec, dst_addr);
    return true;
}

bool emit_inline_v4i32_reduce_call(AArch64MachineBlock &machine_block,
                                   AArch64AbiEmissionContext &abi_context,
                                   const CoreIrCallInst &call,
                                   AArch64MachineFunction &function) {
    if (!call.get_is_direct_call()) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    const bool is_smin = callee_name == kInlineReduceSminV4I32;
    const bool is_smax = callee_name == kInlineReduceSmaxV4I32;
    const bool is_add = callee_name == kInlineReduceAddV4I32;
    if ((!is_smin && !is_smax && !is_add) || call.get_operands().size() != 1) {
        return false;
    }

    AArch64VirtualReg dst_reg;
    if (!abi_context.require_canonical_vreg(&call, dst_reg)) {
        return true;
    }

    const AArch64VirtualReg scalar_vec = create_lane_scalar_reg(function);
    AArch64VirtualReg src_vec;
    if (is_i32x4_vector_type(call.get_operands()[0]->get_type())) {
        if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                              src_vec)) {
            return true;
        }
    } else {
        AArch64VirtualReg src_addr;
        if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                              src_addr)) {
            return true;
        }
        src_vec = create_v4i32_reg(function);
        append_load_v4i32(machine_block, src_vec, src_addr);
    }
    machine_block.append_instruction(AArch64MachineInstr(
        is_smin ? "sminv" : (is_smax ? "smaxv" : "addv"),
        {def_vreg_operand(scalar_vec), use_v4s_operand(src_vec)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "fmov", {def_vreg_operand_as(dst_reg, false),
                  use_vreg_operand(scalar_vec)}));
    return true;
}

bool emit_inline_v4i32_reduce_pair_call(AArch64MachineBlock &machine_block,
                                        AArch64AbiEmissionContext &abi_context,
                                        const CoreIrCallInst &call,
                                        AArch64MachineFunction &function) {
    if (!call.get_is_direct_call()) {
        return false;
    }
    const std::string &callee_name = call.get_callee_name();
    const bool is_smin = callee_name == kInlineReduceSminPairV4I32;
    const bool is_smax = callee_name == kInlineReduceSmaxPairV4I32;
    const bool is_add = callee_name == kInlineReduceAddPairV4I32;
    if ((!is_smin && !is_smax && !is_add) || call.get_operands().size() != 2) {
        return false;
    }

    AArch64VirtualReg lhs_addr;
    AArch64VirtualReg rhs_addr;
    if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[0],
                                          lhs_addr) ||
        !abi_context.ensure_value_in_vreg(machine_block, call.get_operands()[1],
                                          rhs_addr)) {
        return true;
    }

    AArch64VirtualReg dst_reg;
    if (!abi_context.require_canonical_vreg(&call, dst_reg)) {
        return true;
    }

    const AArch64VirtualReg lhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg rhs_vec = create_v4i32_reg(function);
    const AArch64VirtualReg merged_vec = create_v4i32_reg(function);
    const AArch64VirtualReg scalar_vec = create_lane_scalar_reg(function);
    append_load_v4i32(machine_block, lhs_vec, lhs_addr);
    append_load_v4i32(machine_block, rhs_vec, rhs_addr);
    append_v4i32_binary(machine_block, is_smin ? "smin" : (is_smax ? "smax" : "add"),
                        merged_vec, lhs_vec, rhs_vec);
    machine_block.append_instruction(AArch64MachineInstr(
        is_smin ? "sminv" : (is_smax ? "smaxv" : "addv"),
        {def_vreg_operand(scalar_vec), use_v4s_operand(merged_vec)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "fmov", {def_vreg_operand_as(dst_reg, false),
                  use_vreg_operand(scalar_vec)}));
    return true;
}

} // namespace

bool emit_call_instruction(AArch64MachineBlock &machine_block,
                           AArch64CallReturnLoweringContext &context,
                           AArch64AbiEmissionContext &abi_context,
                           const CoreIrCallInst &call,
                           AArch64MachineFunction &function) {
    if (emit_direct_fma_call(machine_block, abi_context, call)) {
        return true;
    }
    if (emit_native_v4i32_value_call(machine_block, abi_context, call)) {
        return true;
    }
    if (emit_inline_v4i32_extremum_call(machine_block, abi_context, call,
                                        function)) {
        return true;
    }
    if (emit_inline_v4i32_add_call(machine_block, abi_context, call, function)) {
        return true;
    }
    if (emit_inline_v4i32_binary_splat_call(machine_block, abi_context, call,
                                            kInlineVecAddV4I32SplatLhs, "add",
                                            true, function)) {
        return true;
    }
    if (emit_inline_v4i32_binary_splat_call(machine_block, abi_context, call,
                                            kInlineVecAddV4I32SplatRhs, "add",
                                            false, function)) {
        return true;
    }
    if (emit_inline_v4i32_mul_call(machine_block, abi_context, call, function)) {
        return true;
    }
    if (emit_inline_v4i32_binary_splat_call(machine_block, abi_context, call,
                                            kInlineVecMulV4I32SplatLhs, "mul",
                                            true, function)) {
        return true;
    }
    if (emit_inline_v4i32_binary_splat_call(machine_block, abi_context, call,
                                            kInlineVecMulV4I32SplatRhs, "mul",
                                            false, function)) {
        return true;
    }
    if (emit_inline_v4i32_copy_call(machine_block, abi_context, call, function)) {
        return true;
    }
    if (emit_inline_v4i32_zero_call(machine_block, abi_context, call, function)) {
        return true;
    }
    if (emit_inline_v4i32_splat_lane0_call(machine_block, abi_context, call,
                                           function)) {
        return true;
    }
    if (emit_inline_v4i32_splat_scalar_call(machine_block, abi_context, call,
                                            function)) {
        return true;
    }
    if (emit_inline_v4i32_insert_lane0_zeroed_call(machine_block, abi_context,
                                                   call, function)) {
        return true;
    }
    if (emit_inline_v4i32_insert_lane_call(machine_block, abi_context, call,
                                           function)) {
        return true;
    }
    if (emit_inline_v4i32_reduce_call(machine_block, abi_context, call, function)) {
        return true;
    }
    if (emit_inline_v4i32_reduce_pair_call(machine_block, abi_context, call,
                                           function)) {
        return true;
    }
    if (call.get_is_direct_call() &&
        call.get_callee_name().rfind("llvm.va_start", 0) == 0) {
        const auto variadic_state = context.variadic_va_list_state();
        if (!variadic_state.is_variadic_function || call.get_operands().empty()) {
            abi_context.report_error(
                "llvm.va_start requires a variadic AArch64 function context");
            return false;
        }
        static CoreIrIntegerType i32_type(32);
        const CoreIrType *ptr_type = abi_context.create_fake_pointer_type();
        AArch64VirtualReg va_list_address;
        if (!abi_context.ensure_value_in_vreg(machine_block, call.get_operands().front(),
                                              va_list_address)) {
            return false;
        }
        const AArch64VirtualReg stack_ptr_reg =
            abi_context.create_pointer_virtual_reg(function);
        if (!abi_context.materialize_incoming_stack_address(
                machine_block, stack_ptr_reg,
                variadic_state.incoming_stack_offset, function) ||
            !abi_context.append_store_to_address(machine_block, ptr_type,
                                                stack_ptr_reg, va_list_address, 0,
                                                function)) {
            return false;
        }
        const AArch64VirtualReg gpr_top_reg =
            abi_context.create_pointer_virtual_reg(function);
        if (variadic_state.gpr_save_area_offset.has_value()) {
            abi_context.append_frame_address(machine_block, gpr_top_reg,
                                            *variadic_state.gpr_save_area_offset,
                                            function);
            if (!abi_context.add_constant_offset(machine_block, gpr_top_reg, 64,
                                                 function)) {
                return false;
            }
        } else {
            abi_context.append_frame_address(machine_block, gpr_top_reg, 0, function);
        }
        if (!abi_context.append_store_to_address(machine_block, ptr_type, gpr_top_reg,
                                                 va_list_address, 8, function)) {
            return false;
        }
        const AArch64VirtualReg fpr_top_reg =
            abi_context.create_pointer_virtual_reg(function);
        if (variadic_state.fpr_save_area_offset.has_value()) {
            abi_context.append_frame_address(machine_block, fpr_top_reg,
                                            *variadic_state.fpr_save_area_offset,
                                            function);
            if (!abi_context.add_constant_offset(machine_block, fpr_top_reg, 128,
                                                 function)) {
                return false;
            }
        } else {
            abi_context.append_frame_address(machine_block, fpr_top_reg, 0, function);
        }
        if (!abi_context.append_store_to_address(machine_block, ptr_type, fpr_top_reg,
                                                 va_list_address, 16, function)) {
            return false;
        }
        AArch64VirtualReg gr_offs_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const int gr_offs = variadic_state.named_gpr_slots >= 8
                                ? 0
                                : -64 + static_cast<int>(variadic_state.named_gpr_slots) *
                                            8;
        CoreIrConstantInt gr_offs_constant(&i32_type,
                                           static_cast<std::uint32_t>(gr_offs));
        if (!abi_context.ensure_value_in_vreg(machine_block, &gr_offs_constant,
                                              gr_offs_reg) ||
            !abi_context.append_store_to_address(machine_block, &i32_type,
                                                 gr_offs_reg, va_list_address, 24,
                                                 function)) {
            return false;
        }
        AArch64VirtualReg vr_offs_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const int vr_offs = variadic_state.named_fpr_slots >= 8
                                ? 0
                                : -128 + static_cast<int>(variadic_state.named_fpr_slots) *
                                              16;
        CoreIrConstantInt vr_offs_constant(&i32_type,
                                           static_cast<std::uint32_t>(vr_offs));
        if (!abi_context.ensure_value_in_vreg(machine_block, &vr_offs_constant,
                                              vr_offs_reg) ||
            !abi_context.append_store_to_address(machine_block, &i32_type,
                                                 vr_offs_reg, va_list_address, 28,
                                                 function)) {
            return false;
        }
        return true;
    }
    return emit_call_with_abi(machine_block, call, context.classify_call(call),
                              function, abi_context,
                              context.lookup_indirect_call_copy_offsets(call));
}

bool emit_return_instruction(AArch64MachineFunction &machine_function,
                             AArch64MachineBlock &machine_block,
                             AArch64CallReturnLoweringContext &context,
                             AArch64AbiEmissionContext &abi_context,
                             const CoreIrReturnInst &return_inst) {
    return emit_function_return(machine_function, machine_block, return_inst,
                                context.function_abi_info(),
                                context.indirect_result_address(), abi_context);
}

} // namespace sysycc
