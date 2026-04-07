#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_machine_ir.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"

namespace sysycc {

struct AArch64FunctionValueState {
    AArch64VirtualReg indirect_result_address;
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> value_locations;
    std::unordered_map<const CoreIrValue *, std::size_t> aggregate_value_offsets;
    std::unordered_set<const CoreIrStackSlot *> promoted_stack_slots;
    std::unordered_map<const CoreIrStackSlot *, AArch64VirtualReg>
        promoted_stack_slot_values;
};

struct AArch64FunctionCallState {
    AArch64FunctionAbiInfo abi_info;
    std::unordered_map<const CoreIrCallInst *, std::vector<std::size_t>>
        indirect_call_argument_copy_offsets;
};

struct AArch64FunctionControlState {
    std::unordered_map<AArch64PhiEdgeKey, std::string, AArch64PhiEdgeKeyHash>
        phi_edge_labels;
    std::vector<AArch64PhiEdgePlan> phi_edge_plans;
};

struct AArch64FunctionDebugState {
    unsigned last_debug_file_id = 0;
    int last_debug_line = 0;
    int last_debug_column = 0;
};

struct AArch64FunctionLoweringState {
    AArch64MachineFunction *machine_function = nullptr;
    AArch64FunctionCallState call_state;
    AArch64FunctionValueState value_state;
    AArch64FunctionControlState control_state;
    AArch64FunctionDebugState debug_state;
};

} // namespace sysycc
