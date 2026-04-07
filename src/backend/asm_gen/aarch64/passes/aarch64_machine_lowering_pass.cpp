#include "backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_machine_lowering_state.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_instruction_dispatch_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_lowering_context_adapter_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_lowering_context_factory_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_conversion_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_support.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr const char *kDefaultTargetTriple = "aarch64-unknown-linux-gnu";
std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

bool is_byte_string_global(const CoreIrGlobal &global) {
    const auto *array_type =
        dynamic_cast<const CoreIrArrayType *>(global.get_type());
    const auto *byte_string =
        dynamic_cast<const CoreIrConstantByteString *>(global.get_initializer());
    if (array_type == nullptr || byte_string == nullptr) {
        return false;
    }
    const auto *element_type = as_integer_type(array_type->get_element_type());
    return element_type != nullptr && element_type->get_bit_width() == 8 &&
           byte_string->get_bytes().size() == array_type->get_element_count();
}

std::string general_register_name(unsigned index, bool use_64bit) {
    return std::string(use_64bit ? "x" : "w") + std::to_string(index);
}

std::string floating_register_name(unsigned index, AArch64VirtualRegKind kind) {
    return std::string(1, virtual_reg_suffix(kind)) + std::to_string(index);
}

std::string zero_register_name(bool use_64bit) {
    return use_64bit ? "xzr" : "wzr";
}

std::string scalar_directive(const CoreIrType *type) {
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return ".xword";
    }
    if (get_storage_size(type) == 2) {
        return ".hword";
    }
    if (get_storage_size(type) == 1) {
        return ".byte";
    }
    return ".word";
}

std::optional<long long> get_signed_integer_constant(const CoreIrConstant *constant) {
    const auto *int_constant = dynamic_cast<const CoreIrConstantInt *>(constant);
    if (int_constant == nullptr) {
        return std::nullopt;
    }
    const auto *integer_type = as_integer_type(constant->get_type());
    if (integer_type == nullptr) {
        return std::nullopt;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    if (bit_width == 0 || bit_width > 64) {
        return std::nullopt;
    }
    const std::uint64_t value = int_constant->get_value();
    if (bit_width == 64) {
        return static_cast<long long>(value);
    }
    const std::uint64_t sign_bit = 1ULL << (bit_width - 1);
    const std::uint64_t mask =
        bit_width == 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
    const std::uint64_t masked = value & mask;
    if ((masked & sign_bit) == 0) {
        return static_cast<long long>(masked);
    }
    return static_cast<long long>(masked | ~mask);
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

std::unordered_set<unsigned>
collect_physical_regs_in_operand_text(const std::string &text) {
    std::unordered_set<unsigned> regs;
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if ((text[index] != 'x' && text[index] != 'w') ||
            !std::isdigit(static_cast<unsigned char>(text[index + 1]))) {
            continue;
        }
        if (index > 0) {
            const char previous = text[index - 1];
            if (std::isalnum(static_cast<unsigned char>(previous)) != 0 ||
                previous == '%') {
                continue;
            }
        }
        std::size_t cursor = index + 1;
        unsigned reg = 0;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            reg = (reg * 10U) +
                  static_cast<unsigned>(text[cursor] - '0');
            ++cursor;
        }
        regs.insert(reg);
        index = cursor - 1;
    }
    return regs;
}

struct AArch64InstructionLiveness {
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
};

struct AArch64BlockLiveness {
    std::vector<AArch64InstructionLiveness> instructions;
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
    std::unordered_set<std::size_t> live_in;
    std::unordered_set<std::size_t> live_out;
    std::vector<std::size_t> successors;
};

class AArch64LoweringSession : public AArch64MemoryAccessContext,
                               public AArch64ConstantMaterializationContext {
  private:
    template <typename Session, typename State>
    friend class ::sysycc::AArch64LoweringContextFactory;

    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64CodegenContext &codegen_context_;
    AArch64AbiLoweringPass abi_lowering_pass_;
    AArch64MachineModule *machine_module_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;
    using FunctionState = AArch64FunctionLoweringState;

  public:
    AArch64LoweringSession(AArch64CodegenContext &codegen_context)
        : module_(*codegen_context.module),
          backend_options_(*codegen_context.backend_options),
          diagnostic_engine_(*codegen_context.diagnostic_engine),
          codegen_context_(codegen_context) {}

    bool Lower() {
        if (!backend_options_.get_target_triple().empty() &&
            backend_options_.get_target_triple() != kDefaultTargetTriple) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported AArch64 native target triple: " +
                    backend_options_.get_target_triple());
            return false;
        }

        machine_module_ = &codegen_context_.machine_module;
        machine_module_->append_preamble_line(module_uses_float16()
                                                  ? ".arch armv8.2-a+fp16"
                                                  : ".arch armv8-a");

        auto global_data_context = make_global_data_lowering_context();
        if (!sysycc::append_globals(*machine_module_, module_, global_data_context) ||
            !append_functions(*machine_module_)) {
            return false;
        }
        return true;
    }

  private:
    void record_symbol_definition(const std::string &name, AArch64SymbolKind kind,
                                  AArch64SectionKind section_kind,
                                  bool is_global_symbol) {
        if (machine_module_ == nullptr) {
            return;
        }
        machine_module_->record_symbol(name, kind, section_kind, true,
                                       is_global_symbol, false);
    }

    void record_symbol_reference(const std::string &name, AArch64SymbolKind kind) {
        if (machine_module_ == nullptr) {
            return;
        }
        machine_module_->record_symbol(name, kind, std::nullopt, false, false, true);
    }

    unsigned record_debug_file(const SourceFile *source_file) {
        if (machine_module_ == nullptr || source_file == nullptr || source_file->empty()) {
            return 0;
        }
        return machine_module_->record_debug_file(source_file->get_path());
    }

    bool is_nonpreemptible_global_symbol(const std::string &symbol_name) const {
        const CoreIrGlobal *global = module_.find_global(symbol_name);
        return global != nullptr && global->get_is_internal_linkage();
    }

    bool is_nonpreemptible_function_symbol(const std::string &symbol_name) const {
        const CoreIrFunction *function = module_.find_function(symbol_name);
        return function != nullptr && function->get_is_internal_linkage();
    }

    bool module_uses_float16() const {
        for (const auto &global : module_.get_globals()) {
            if (type_contains_float_kind(global->get_type(), CoreIrFloatKind::Float16) ||
                (global->get_initializer() != nullptr &&
                 type_contains_float_kind(global->get_initializer()->get_type(),
                                          CoreIrFloatKind::Float16))) {
                return true;
            }
        }
        for (const auto &function : module_.get_functions()) {
            if (type_contains_float_kind(function->get_function_type(),
                                         CoreIrFloatKind::Float16)) {
                return true;
            }
            for (const auto &stack_slot : function->get_stack_slots()) {
                if (type_contains_float_kind(stack_slot->get_allocated_type(),
                                             CoreIrFloatKind::Float16)) {
                    return true;
                }
            }
            for (const auto &basic_block : function->get_basic_blocks()) {
                for (const auto &instruction : basic_block->get_instructions()) {
                    if (type_contains_float_kind(instruction->get_type(),
                                                 CoreIrFloatKind::Float16)) {
                        return true;
                    }
                    for (CoreIrValue *operand : instruction->get_operands()) {
                        if (operand != nullptr &&
                            type_contains_float_kind(operand->get_type(),
                                                     CoreIrFloatKind::Float16)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                         const CoreIrType *type) const {
        return function.create_virtual_reg(classify_virtual_reg_kind(type));
    }

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) override {
        return function.create_virtual_reg(AArch64VirtualRegKind::General64);
    }

    auto make_function_planning_context() {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this)
            .make_function_planning_context();
    }

    auto make_phi_plan_context() {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this)
            .make_phi_plan_context();
    }

    auto make_phi_copy_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_phi_copy_context();
    }

    auto make_instruction_dispatch_context() {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this)
            .make_instruction_dispatch_context();
    }

    auto make_abi_emission_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_abi_emission_context();
    }

    auto make_address_materialization_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_address_materialization_context();
    }

    auto make_value_materialization_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_value_materialization_context();
    }

    auto make_memory_value_lowering_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_memory_value_lowering_context();
    }

    auto make_float_helper_lowering_context() {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this)
            .make_float_helper_lowering_context();
    }

    AArch64CallbackGlobalDataLoweringContext make_global_data_lowering_context() {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this)
            .make_global_data_lowering_context();
    }

    auto make_memory_instruction_lowering_context(FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_memory_instruction_lowering_context();
    }

    auto make_scalar_lowering_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_scalar_lowering_context();
    }

    auto make_call_return_lowering_context(const FunctionState &state) {
        return make_aarch64_lowering_context_factory<AArch64LoweringSession,
                                                     FunctionState>(*this, state)
            .make_call_return_lowering_context();
    }

    void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &target_reg,
                                       unsigned physical_reg,
                                       AArch64VirtualRegKind reg_kind) override {
        ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                physical_reg, reg_kind);
    }

    void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                     unsigned physical_reg,
                                     AArch64VirtualRegKind reg_kind,
                                     const AArch64VirtualReg &source_reg) override {
        ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                              reg_kind, source_reg);
    }

    void report_error(const std::string &message) override {
        add_backend_error(diagnostic_engine_, message);
    }

    void append_helper_call(AArch64MachineBlock &machine_block,
                            const std::string &symbol_name) override {
        record_symbol_reference(symbol_name, AArch64SymbolKind::Helper);
        machine_block.append_instruction(AArch64MachineInstr(
            "bl", {AArch64MachineOperand(symbol_name)},
            AArch64InstructionFlags{.is_call = true}, {}, {},
            make_default_aarch64_call_clobber_mask()));
    }

    const AArch64ValueLocation *
    lookup_value_location(const FunctionState &state,
                          const CoreIrValue *value) const {
        const auto it = state.value_state.value_locations.find(value);
        if (it == state.value_state.value_locations.end()) {
            return nullptr;
        }
        return &it->second;
    }

    bool require_canonical_vreg(const FunctionState &state,
                                const CoreIrValue *value,
                                AArch64VirtualReg &out) const {
        const AArch64ValueLocation *location = lookup_value_location(state, value);
        if (location == nullptr ||
            location->kind != AArch64ValueLocationKind::VirtualReg ||
            !location->virtual_reg.is_valid()) {
            add_backend_error(diagnostic_engine_,
                              "missing canonical AArch64 virtual register for Core IR value");
            return false;
        }
        out = location->virtual_reg;
        return true;
    }

    bool require_canonical_memory_address(const FunctionState &state,
                                          const CoreIrValue *value,
                                          AArch64VirtualReg &out,
                                          std::size_t &offset) const {
        const AArch64ValueLocation *location = lookup_value_location(state, value);
        if (location == nullptr ||
            location->kind != AArch64ValueLocationKind::MemoryAddress ||
            !location->virtual_reg.is_valid()) {
            add_backend_error(
                diagnostic_engine_,
                "missing canonical AArch64 aggregate memory location for Core IR value");
            return false;
        }
        const auto offset_it = state.value_state.aggregate_value_offsets.find(value);
        if (offset_it == state.value_state.aggregate_value_offsets.end()) {
            add_backend_error(
                diagnostic_engine_,
                "missing frame offset for canonical AArch64 aggregate value");
            return false;
        }
        out = location->virtual_reg;
        offset = offset_it->second;
        return true;
    }

    bool seed_function_value_locations(const CoreIrFunction &function,
                                       FunctionState &state,
                                       std::size_t &current_offset) {
        auto planning_context = make_function_planning_context();
        return sysycc::seed_function_value_locations(
            function, *state.machine_function, state.value_state.value_locations,
            state.value_state.aggregate_value_offsets, current_offset,
            planning_context);
    }

    std::string resolve_branch_target_label(const FunctionState &state,
                                            const CoreIrBasicBlock *predecessor,
                                            const CoreIrBasicBlock *successor) const {
        const auto edge_it =
            state.control_state.phi_edge_labels.find(
                AArch64PhiEdgeKey{predecessor, successor});
        if (edge_it != state.control_state.phi_edge_labels.end()) {
            return edge_it->second;
        }
        return block_labels_.at(successor);
    }

    void emit_debug_location(AArch64MachineBlock &machine_block,
                             const SourceSpan &source_span,
                             FunctionState &state) {
        if (!backend_options_.get_debug_info() || source_span.empty()) {
            return;
        }
        const unsigned debug_file_id = record_debug_file(source_span.get_file());
        const int line = source_span.get_line_begin();
        const int column = std::max(1, source_span.get_col_begin());
        if (debug_file_id == 0 || line <= 0) {
            return;
        }
        if (state.debug_state.last_debug_file_id == debug_file_id &&
            state.debug_state.last_debug_line == line &&
            state.debug_state.last_debug_column == column) {
            return;
        }
        machine_block.append_instruction(".loc " + std::to_string(debug_file_id) + " " +
                                         std::to_string(line) + " " +
                                         std::to_string(column));
        state.debug_state.last_debug_file_id = debug_file_id;
        state.debug_state.last_debug_line = line;
        state.debug_state.last_debug_column = column;
    }

    bool is_promoted_stack_slot(const FunctionState &state,
                                const CoreIrStackSlot *stack_slot) const {
        return stack_slot != nullptr &&
               state.value_state.promoted_stack_slots.find(stack_slot) !=
                   state.value_state.promoted_stack_slots.end();
    }

    bool append_functions(AArch64MachineModule &machine_module) {
        for (const auto &function : module_.get_functions()) {
            if (!append_function(machine_module, *function)) {
                return false;
            }
        }
        return true;
    }

    AArch64MachineFunction &
    append_machine_function(AArch64MachineModule &machine_module,
                            const CoreIrFunction &function) {
        const std::string function_name = function.get_name();
        AArch64MachineFunction &machine_function = machine_module.append_function(
            function_name, !function.get_is_internal_linkage(),
            make_aarch64_function_epilogue_label(function_name));
        record_symbol_definition(function_name, AArch64SymbolKind::Function,
                                 AArch64SectionKind::Text,
                                 !function.get_is_internal_linkage());
        machine_function.set_section_kind(AArch64SectionKind::Text);
        return machine_function;
    }

    void finalize_function_frame_layout(AArch64MachineFunction &machine_function,
                                        std::size_t current_offset) {
        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);
        initialize_aarch64_function_frame_record(machine_function, frame_size);
    }

    bool plan_function_stack_and_values(const CoreIrFunction &function,
                                        FunctionState &state) {
        std::size_t current_offset = 0;
        sysycc::layout_stack_slots(*state.machine_function, function, current_offset);

        if (!seed_function_value_locations(function, state, current_offset)) {
            return false;
        }
        auto copy_slot_planning_context = make_function_planning_context();
        sysycc::seed_call_argument_copy_slots(
            function, state.call_state.indirect_call_argument_copy_offsets,
            current_offset,
            copy_slot_planning_context);
        sysycc::seed_promoted_stack_slots(function, state.value_state.value_locations,
                                          state.value_state.promoted_stack_slots);
        finalize_function_frame_layout(*state.machine_function, current_offset);
        return true;
    }

    bool plan_function_control_flow(const CoreIrFunction &function,
                                    FunctionState &state) {
        block_labels_ =
            build_aarch64_function_block_labels(function, function.get_name());
        state.control_state.phi_edge_labels.clear();
        state.control_state.phi_edge_plans.clear();
        auto phi_plan_context = make_phi_plan_context();
        if (!sysycc::build_phi_edge_plans(function, phi_plan_context,
                                          state.control_state.phi_edge_labels,
                                          state.control_state.phi_edge_plans)) {
            return false;
        }
        return true;
    }

    bool initialize_function_state(AArch64MachineModule &machine_module,
                                   const CoreIrFunction &function,
                                   FunctionState &state) {
        auto planning_context = make_function_planning_context();
        if (!sysycc::validate_function_lowering_readiness(function,
                                                          planning_context)) {
            return false;
        }

        state.machine_function = &append_machine_function(machine_module, function);
        state.call_state.abi_info = abi_lowering_pass_.classify_function(function);

        if (!plan_function_stack_and_values(function, state)) {
            return false;
        }

        return plan_function_control_flow(function, state);
    }

    bool append_function(AArch64MachineModule &machine_module,
                         const CoreIrFunction &function) {
        if (function.get_basic_blocks().empty()) {
            return true;
        }

        FunctionState state;
        if (!initialize_function_state(machine_module, function, state)) {
            return false;
        }

        return emit_function(*state.machine_function, function, state);
    }

    bool emit_function_entry(AArch64MachineFunction &machine_function,
                             const CoreIrFunction &function,
                             FunctionState &state) {
        auto abi_context = make_abi_emission_context(state);
        AArch64MachineBlock &prologue_block =
            machine_function.append_block(function.get_name());
        append_aarch64_standard_prologue(
            prologue_block, machine_function.get_frame_info().get_frame_size());
        if (state.call_state.abi_info.return_value.is_indirect) {
            state.value_state.indirect_result_address =
                create_pointer_virtual_reg(machine_function);
            append_copy_from_physical_reg(
                prologue_block, state.value_state.indirect_result_address,
                static_cast<unsigned>(AArch64PhysicalReg::X8),
                AArch64VirtualRegKind::General64);
        }
        return lower_function_entry_parameters(prologue_block, function,
                                               state.call_state.abi_info,
                                               *state.machine_function,
                                               abi_context);
    }

    bool emit_function_body(AArch64MachineFunction &machine_function,
                            const CoreIrFunction &function, FunctionState &state) {
        for (const auto &basic_block : function.get_basic_blocks()) {
            AArch64MachineBlock &machine_block =
                machine_function.append_block(block_labels_.at(basic_block.get()));
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!emit_instruction(machine_function, machine_block,
                                      basic_block.get(), *instruction, state)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool emit_phi_edge_blocks(AArch64MachineFunction &machine_function,
                              const FunctionState &state) {
        for (const AArch64PhiEdgePlan &plan : state.control_state.phi_edge_plans) {
            if (!emit_phi_edge_block(machine_function, plan, state)) {
                return false;
            }
        }
        return true;
    }

    void emit_function_exit(AArch64MachineFunction &machine_function) {
        AArch64MachineBlock &epilogue_block =
            machine_function.append_block(machine_function.get_epilogue_label());
        append_aarch64_standard_epilogue(
            epilogue_block, machine_function.get_frame_info().get_frame_size());
    }

    bool emit_function(AArch64MachineFunction &machine_function,
                       const CoreIrFunction &function, FunctionState &state) {
        if (!emit_function_entry(machine_function, function, state)) {
            return false;
        }
        if (!emit_function_body(machine_function, function, state)) {
            return false;
        }
        if (!emit_phi_edge_blocks(machine_function, state)) {
            return false;
        }
        emit_function_exit(machine_function);
        return true;
    }

    bool emit_instruction(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrBasicBlock *current_block,
                          const CoreIrInstruction &instruction,
                          FunctionState &state) {
        auto dispatch_context = make_instruction_dispatch_context();
        return sysycc::dispatch_aarch64_lowered_instruction(
            dispatch_context, machine_function, machine_block, current_block,
            instruction, state);
    }

    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value,
                           const AArch64VirtualReg &target_reg,
                           const FunctionState &state) {
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::VirtualReg) {
            if (location->virtual_reg.get_id() != target_reg.get_id()) {
                machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                                 use_vreg(location->virtual_reg));
            }
            return true;
        }

        return materialize_noncanonical_value(machine_block, value, target_reg, state);
    }

    bool materialize_noncanonical_value(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        const AArch64VirtualReg &target_reg,
                                        const FunctionState &state) {
        auto value_context = make_value_materialization_context(state);
        return sysycc::materialize_noncanonical_value(
            machine_block, value_context, value, target_reg,
            *state.machine_function);
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              const FunctionState &state,
                              AArch64VirtualReg &out) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 lowering");
            return false;
        }
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::VirtualReg) {
            out = location->virtual_reg;
            return true;
        }
        out = create_virtual_reg(*state.machine_function, value->get_type());
        return materialize_noncanonical_value(machine_block, value, out, state);
    }

    bool emit_phi_edge_block(AArch64MachineFunction &machine_function,
                             const AArch64PhiEdgePlan &plan,
                             const FunctionState &state) {
        AArch64MachineBlock &edge_block =
            machine_function.append_block(plan.edge_label);
        auto phi_context = make_phi_copy_context(state);
        if (!sysycc::emit_parallel_phi_copies(edge_block, plan, machine_function,
                                              phi_context)) {
            return false;
        }
        edge_block.append_instruction("b " + block_labels_.at(plan.edge.successor));
        return true;
    }

    const CoreIrType *create_fake_pointer_type() const override {
        static CoreIrVoidType void_type;
        static CoreIrPointerType pointer_type(&void_type);
        return &pointer_type;
    }

    std::string load_mnemonic_for_type(const CoreIrType *type) const {
        return sysycc::load_mnemonic_for_type(type);
    }

    std::string store_mnemonic_for_type(const CoreIrType *type) const {
        return sysycc::store_mnemonic_for_type(type);
    }

    bool append_memory_store(AArch64MachineBlock &machine_block, const CoreIrType *type,
                             const std::string &source_reg,
                             const AArch64VirtualReg &address_reg,
                             std::size_t offset,
                             AArch64MachineFunction &function) {
        return sysycc::append_memory_store(machine_block, *this, type, source_reg,
                                           address_reg, offset, function);
    }

    bool emit_zero_fill(AArch64MachineBlock &machine_block,
                        const AArch64VirtualReg &address_reg,
                        const CoreIrType *type,
                        AArch64MachineFunction &function) {
        return sysycc::emit_zero_fill(machine_block, *this, address_reg, type,
                                      function);
    }

    void append_load_from_frame(AArch64MachineBlock &machine_block,
                                const CoreIrType *type,
                                const AArch64VirtualReg &target_reg,
                                std::size_t offset,
                                AArch64MachineFunction &function) {
        sysycc::append_load_from_frame(machine_block, *this, type, target_reg, offset,
                                       function);
    }

    void append_store_to_frame(AArch64MachineBlock &machine_block,
                               const CoreIrType *type,
                               const AArch64VirtualReg &source_reg,
                               std::size_t offset,
                               AArch64MachineFunction &function) {
        sysycc::append_store_to_frame(machine_block, *this, type, source_reg, offset,
                                      function);
    }

    void append_load_from_incoming_stack_arg(AArch64MachineBlock &machine_block,
                                             const CoreIrType *type,
                                             const AArch64VirtualReg &target_reg,
                                             std::size_t offset,
                                             AArch64MachineFunction &function) {
        sysycc::append_load_from_incoming_stack_arg(machine_block, *this, type,
                                                    target_reg, offset, function);
    }

    bool append_load_from_address(AArch64MachineBlock &machine_block,
                                  const CoreIrType *type,
                                  const AArch64VirtualReg &target_reg,
                                  const AArch64VirtualReg &address_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) {
        return sysycc::append_load_from_address(machine_block, *this, type,
                                                target_reg, address_reg, offset,
                                                function);
    }

    bool append_store_to_address(AArch64MachineBlock &machine_block,
                                 const CoreIrType *type,
                                 const AArch64VirtualReg &source_reg,
                                 const AArch64VirtualReg &address_reg,
                                 std::size_t offset,
                                 AArch64MachineFunction &function) {
        return sysycc::append_store_to_address(machine_block, *this, type,
                                               source_reg, address_reg, offset,
                                               function);
    }

    bool emit_memory_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &destination_address,
                          const AArch64VirtualReg &source_address,
                          const CoreIrType *type,
                          AArch64MachineFunction &function) {
        return sysycc::emit_memory_copy(machine_block, *this, destination_address,
                                        source_address, type, function);
    }

    bool materialize_canonical_memory_address(AArch64MachineBlock &machine_block,
                                              const FunctionState &state,
                                              const CoreIrValue *value,
                                              AArch64VirtualReg &address_reg) {
        std::size_t offset = 0;
        if (!require_canonical_memory_address(state, value, address_reg, offset)) {
            return false;
        }
        append_frame_address(machine_block, address_reg, offset, *state.machine_function);
        return true;
    }

    bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        const FunctionState &state,
                                        AArch64VirtualReg &address_reg) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null aggregate Core IR value during AArch64 lowering");
            return false;
        }
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::MemoryAddress) {
            address_reg = location->virtual_reg;
            return true;
        }
        add_backend_error(
            diagnostic_engine_,
            "aggregate Core IR value does not have a canonical memory location in "
            "the AArch64 native backend");
        return false;
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override {
        sysycc::apply_truncate_to_virtual_reg(machine_block, reg, type);
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        sysycc::apply_zero_extend_to_virtual_reg(machine_block, dst_reg, source_type,
                                                 target_type);
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        sysycc::apply_sign_extend_to_virtual_reg(machine_block, dst_reg, source_type,
                                                 target_type);
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override {
        if (offset <= 4095) {
            machine_block.append_instruction("sub " + def_vreg(target_reg) +
                                             ", x29, #" + std::to_string(offset));
            return;
        }
        const AArch64VirtualReg offset_reg = create_pointer_virtual_reg(function);
        sysycc::materialize_integer_constant(machine_block, *this,
                                             create_fake_pointer_type(),
                                             static_cast<std::uint64_t>(offset),
                                             offset_reg);
        machine_block.append_instruction("sub " + def_vreg(target_reg) + ", x29, " +
                                         use_vreg(offset_reg));
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg,
                             std::uint64_t magnitude, bool is_negative,
                             AArch64MachineFunction &function) {
        if (magnitude == 0) {
            return true;
        }
        if (magnitude <= 4095) {
            machine_block.append_instruction(
                std::string(is_negative ? "sub " : "add ") + def_vreg(base_reg) +
                ", " + use_vreg(base_reg) + ", #" +
                std::to_string(magnitude));
            return true;
        }
        const AArch64VirtualReg offset_reg = create_pointer_virtual_reg(function);
        if (!sysycc::materialize_integer_constant(machine_block, *this,
                                                  create_fake_pointer_type(),
                                                  magnitude, offset_reg)) {
            return false;
        }
        machine_block.append_instruction(
            std::string(is_negative ? "sub " : "add ") + def_vreg(base_reg) +
            ", " + use_vreg(base_reg) + ", " + use_vreg(offset_reg));
        return true;
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) override {
        const bool is_negative = offset < 0;
        const std::uint64_t magnitude =
            is_negative ? static_cast<std::uint64_t>(-(offset + 1)) + 1ULL
                        : static_cast<std::uint64_t>(offset);
        return add_constant_offset(machine_block, base_reg, magnitude, is_negative,
                                   function);
    }

    bool emit_address_of_stack_slot(AArch64MachineBlock &machine_block,
                                    const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
                                    const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_stack_slot, target_reg)) {
            return false;
        }
        auto value_context = make_value_materialization_context(state);
        return sysycc::emit_address_of_stack_slot_value(
            machine_block, value_context, address_of_stack_slot, target_reg,
            *state.machine_function);
    }

    bool emit_address_of_global(AArch64MachineBlock &machine_block,
                                const CoreIrAddressOfGlobalInst &address_of_global,
                                const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_global, target_reg)) {
            return false;
        }
        auto value_context = make_value_materialization_context(state);
        return sysycc::emit_address_of_global_value(machine_block, value_context,
                                                    address_of_global, target_reg);
    }

    bool emit_address_of_function(AArch64MachineBlock &machine_block,
                                  const CoreIrAddressOfFunctionInst &address_of_function,
                                  const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_function, target_reg)) {
            return false;
        }
        auto value_context = make_value_materialization_context(state);
        return sysycc::emit_address_of_function_value(
            machine_block, value_context, address_of_function, target_reg);
    }

    bool emit_getelementptr(AArch64MachineBlock &machine_block,
                            const CoreIrGetElementPtrInst &gep,
                            const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &gep, target_reg)) {
            return false;
        }
        auto value_context = make_value_materialization_context(state);
        return sysycc::emit_getelementptr_value(machine_block, value_context, gep,
                                                target_reg, *state.machine_function);
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   FunctionState &state) {
        auto memory_instruction_context =
            make_memory_instruction_lowering_context(state);
        auto memory_value_context = make_memory_value_lowering_context(state);
        return sysycc::emit_load_instruction(
            machine_block, memory_instruction_context, memory_value_context, load,
            *state.machine_function);
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, FunctionState &state) {
        auto memory_instruction_context =
            make_memory_instruction_lowering_context(state);
        auto memory_value_context = make_memory_value_lowering_context(state);
        return sysycc::emit_store_instruction(
            machine_block, memory_instruction_context, memory_value_context, store,
            *state.machine_function);
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary, const FunctionState &state) {
        auto scalar_context = make_scalar_lowering_context(state);
        return sysycc::emit_binary_instruction(machine_block, scalar_context, binary,
                                               *state.machine_function);
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        auto scalar_context = make_scalar_lowering_context(state);
        return sysycc::emit_unary_instruction(machine_block, scalar_context, unary,
                                              *state.machine_function);
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        auto scalar_context = make_scalar_lowering_context(state);
        return sysycc::emit_compare_instruction(machine_block, scalar_context,
                                                compare, *state.machine_function);
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        auto scalar_context = make_scalar_lowering_context(state);
        return sysycc::emit_cast_instruction(machine_block, scalar_context, cast,
                                             *state.machine_function);
    }

    bool emit_call(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                   const FunctionState &state) {
        auto abi_context = make_abi_emission_context(state);
        auto call_return_context = make_call_return_lowering_context(state);
        return sysycc::emit_call_instruction(
            machine_block, call_return_context, abi_context, call,
            *state.machine_function);
    }

    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state,
                        const CoreIrBasicBlock *current_block) {
        AArch64VirtualReg condition_reg;
        if (!ensure_value_in_vreg(machine_block, cond_jump.get_condition(), state,
                                  condition_reg)) {
            return false;
        }
        machine_block.append_instruction(
            "cbnz " + use_vreg(condition_reg) + ", " +
            resolve_branch_target_label(state, current_block,
                                        cond_jump.get_true_block()));
        machine_block.append_instruction(
            "b " + resolve_branch_target_label(state, current_block,
                                               cond_jump.get_false_block()));
        return true;
    }

    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state) {
        auto abi_context = make_abi_emission_context(state);
        auto call_return_context = make_call_return_lowering_context(state);
        return sysycc::emit_return_instruction(
            machine_function, machine_block, call_return_context, abi_context,
            return_inst);
    }
};

} // namespace

bool AArch64MachineLoweringPass::run(AArch64CodegenContext &codegen_context) const {
    AArch64LoweringSession session(codegen_context);
    return session.Lower();
}

} // namespace sysycc
