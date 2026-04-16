#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

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

} // namespace

bool emit_call_instruction(AArch64MachineBlock &machine_block,
                           AArch64CallReturnLoweringContext &context,
                           AArch64AbiEmissionContext &abi_context,
                           const CoreIrCallInst &call,
                           AArch64MachineFunction &function) {
    if (emit_direct_fma_call(machine_block, abi_context, call)) {
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
