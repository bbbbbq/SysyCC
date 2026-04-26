#include "backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_machine_lowering_state.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_lowering_facade.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_instruction_dispatch_support.hpp"
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

bool machine_function_contains_call(const AArch64MachineFunction &function) {
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            if (instruction.get_flags().is_call ||
                instruction.get_call_clobber_mask().has_value()) {
                return true;
            }
        }
    }
    return false;
}

bool is_byte_string_global(const CoreIrGlobal &global) {
    const auto *array_type =
        dynamic_cast<const CoreIrArrayType *>(global.get_type());
    const auto *byte_string = dynamic_cast<const CoreIrConstantByteString *>(
        global.get_initializer());
    if (array_type == nullptr || byte_string == nullptr) {
        return false;
    }
    const auto *element_type = as_integer_type(array_type->get_element_type());
    return element_type != nullptr && element_type->get_bit_width() == 8 &&
           byte_string->get_bytes().size() == array_type->get_element_count();
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

std::optional<long long>
get_signed_integer_constant(const CoreIrConstant *constant) {
    const auto *int_constant =
        dynamic_cast<const CoreIrConstantInt *>(constant);
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

class AArch64LoweringSession : public AArch64LoweringFacadeServices {
  private:
    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64CodegenContext &codegen_context_;
    AArch64AbiLoweringPass abi_lowering_pass_;
    AArch64AsmModule *asm_module_ = nullptr;
    AArch64MachineModule *machine_module_ = nullptr;
    AArch64ObjectModule *object_module_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;
    using FunctionState = AArch64FunctionLoweringState;

  public:
    AArch64LoweringSession(AArch64CodegenContext &codegen_context)
        : module_(codegen_context.input->core_ir_module()),
          backend_options_(*codegen_context.backend_options),
          diagnostic_engine_(*codegen_context.diagnostic_engine),
          codegen_context_(codegen_context) {}

    DiagnosticEngine &diagnostic_engine() const override {
        return diagnostic_engine_;
    }

    bool Lower() {
        if (!backend_options_.get_target_triple().empty() &&
            backend_options_.get_target_triple() != kDefaultTargetTriple) {
            add_backend_error(diagnostic_engine_,
                              "unsupported AArch64 native target triple: " +
                                  backend_options_.get_target_triple());
            return false;
        }

        asm_module_ = &codegen_context_.asm_module;
        machine_module_ = &codegen_context_.machine_module;
        object_module_ = &codegen_context_.object_module;
        if (asm_module_ != nullptr) {
            asm_module_->set_arch_profile(
                module_uses_float16() ? AArch64AsmArchProfile::Armv82AWithFp16
                                      : AArch64AsmArchProfile::Armv8A);
        }

        AArch64GlobalDataLoweringFacade global_data_context(*this);
        if (!sysycc::append_globals(*object_module_, module_,
                                    global_data_context) ||
            !append_functions(*machine_module_)) {
            return false;
        }
        return true;
    }

  private:
    void record_symbol_definition(const std::string &name,
                                  AArch64SymbolKind kind,
                                  AArch64SectionKind section_kind,
                                  bool is_global_symbol) override {
        if (object_module_ == nullptr) {
            return;
        }
        object_module_->record_symbol(name, kind, section_kind, true,
                                      is_global_symbol, false);
    }

    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override {
        if (object_module_ == nullptr) {
            return;
        }
        object_module_->record_symbol(name, kind, std::nullopt, false, false,
                                      true);
    }

    AArch64SymbolReference
    make_symbol_reference(const std::string &name, AArch64SymbolKind kind,
                          AArch64SymbolBinding binding,
                          std::optional<AArch64SectionKind> section_kind = std::nullopt,
                          long long addend = 0,
                          bool is_defined = false) const override {
        if (object_module_ != nullptr) {
            return object_module_->make_symbol_reference(
                name, kind, binding, section_kind, addend, is_defined);
        }
        return AArch64SymbolReference::direct(name, kind, binding, section_kind,
                                              addend, is_defined);
    }

    unsigned record_debug_file(const SourceFile *source_file) {
        if (object_module_ == nullptr || source_file == nullptr ||
            source_file->empty()) {
            return 0;
        }
        return object_module_->record_debug_file(source_file->get_path());
    }

    bool is_nonpreemptible_global_symbol(
        const std::string &symbol_name) const override {
        const CoreIrGlobal *global = module_.find_global(symbol_name);
        return global != nullptr && global->get_is_internal_linkage();
    }

    bool is_nonpreemptible_function_symbol(
        const std::string &symbol_name) const override {
        const CoreIrFunction *function = module_.find_function(symbol_name);
        return function != nullptr && function->get_is_internal_linkage();
    }

    bool is_position_independent() const override {
        return backend_options_.get_position_independent();
    }

    bool module_uses_float16() const {
        for (const auto &global : module_.get_globals()) {
            if (type_contains_float_kind(global->get_type(),
                                         CoreIrFloatKind::Float16) ||
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
                for (const auto &instruction :
                     basic_block->get_instructions()) {
                    if (type_contains_float_kind(instruction->get_type(),
                                                 CoreIrFloatKind::Float16)) {
                        return true;
                    }
                    for (CoreIrValue *operand : instruction->get_operands()) {
                        if (operand != nullptr &&
                            type_contains_float_kind(
                                operand->get_type(),
                                CoreIrFloatKind::Float16)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    AArch64VirtualReg
    create_virtual_reg(AArch64MachineFunction &function,
                       const CoreIrType *type) const override {
        return function.create_virtual_reg(classify_virtual_reg_kind(type));
    }

    AArch64FunctionAbiInfo
    classify_call(const CoreIrCallInst &call) const override {
        return abi_lowering_pass_.classify_call(call);
    }

    const std::string &
    block_label(const CoreIrBasicBlock *block) const override {
        return block_labels_.at(block);
    }

    AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) override {
        return function.create_virtual_reg(AArch64VirtualRegKind::General64);
    }

    void append_copy_from_physical_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
        unsigned physical_reg, AArch64VirtualRegKind reg_kind) override {
        ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                physical_reg, reg_kind);
    }

    void
    append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
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
        const AArch64SymbolReference helper_symbol = make_symbol_reference(
            symbol_name, AArch64SymbolKind::Helper,
            AArch64SymbolBinding::Global);
        machine_block.append_instruction(
            AArch64MachineInstr("bl", {AArch64MachineOperand::symbol(helper_symbol)},
                                AArch64InstructionFlags{.is_call = true}, {},
                                {}, make_default_aarch64_call_clobber_mask()));
    }

    const AArch64ValueLocation *
    lookup_value_location(const FunctionState &state,
                          const CoreIrValue *value) const override {
        const auto it = state.value_state.value_locations.find(value);
        if (it == state.value_state.value_locations.end()) {
            return nullptr;
        }
        return &it->second;
    }

    bool require_canonical_vreg(const FunctionState &state,
                                const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        const AArch64ValueLocation *location =
            lookup_value_location(state, value);
        if (location == nullptr ||
            location->kind != AArch64ValueLocationKind::VirtualReg ||
            !location->virtual_reg.is_valid()) {
            std::string detail;
            if (value != nullptr && !value->get_name().empty()) {
                detail = " '" + value->get_name() + "'";
            }
            if (const auto *instruction =
                    dynamic_cast<const CoreIrInstruction *>(value);
                instruction != nullptr) {
                detail += " (opcode " +
                          std::to_string(
                              static_cast<unsigned>(instruction->get_opcode())) +
                          ")";
            }
            add_backend_error(
                diagnostic_engine_,
                "missing canonical AArch64 virtual register for Core IR value" +
                    detail);
            return false;
        }
        out = location->virtual_reg;
        return true;
    }

    bool require_canonical_memory_address(const FunctionState &state,
                                          const CoreIrValue *value,
                                          AArch64VirtualReg &out,
                                          std::size_t &offset) const override {
        const AArch64ValueLocation *location =
            lookup_value_location(state, value);
        if (location == nullptr ||
            location->kind != AArch64ValueLocationKind::MemoryAddress ||
            !location->virtual_reg.is_valid()) {
            add_backend_error(diagnostic_engine_,
                              "missing canonical AArch64 aggregate memory "
                              "location for Core IR value");
            return false;
        }
        const auto offset_it =
            state.value_state.aggregate_value_offsets.find(value);
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
                                       std::size_t &current_offset,
                                       AArch64FunctionLoweringFacade &facade) {
        return sysycc::seed_function_value_locations(
            function, *state.machine_function,
            state.value_state.value_locations,
            state.value_state.aggregate_value_offsets, current_offset, facade);
    }

    std::string resolve_branch_target_label(
        const FunctionState &state, const CoreIrBasicBlock *predecessor,
        const CoreIrBasicBlock *successor) const override {
        const auto edge_it = state.control_state.phi_edge_labels.find(
            AArch64PhiEdgeKey{predecessor, successor});
        if (edge_it != state.control_state.phi_edge_labels.end()) {
            return edge_it->second;
        }
        return block_labels_.at(successor);
    }

    void emit_debug_location(AArch64MachineBlock &machine_block,
                             const SourceSpan &source_span,
                             FunctionState &state) override {
        if (!backend_options_.get_debug_info() || source_span.empty()) {
            return;
        }
        const unsigned debug_file_id =
            record_debug_file(source_span.get_file());
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
        machine_block.set_pending_debug_location(
            AArch64DebugLocation{debug_file_id, line, column});
        state.debug_state.last_debug_file_id = debug_file_id;
        state.debug_state.last_debug_line = line;
        state.debug_state.last_debug_column = column;
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
        AArch64MachineFunction &machine_function =
            machine_module.append_function(
                function_name, !function.get_is_internal_linkage(),
                make_aarch64_function_epilogue_label(function_name));
        record_symbol_definition(function_name, AArch64SymbolKind::Function,
                                 AArch64SectionKind::Text,
                                 !function.get_is_internal_linkage());
        machine_function.set_section_kind(AArch64SectionKind::Text);
        return machine_function;
    }

    void
    finalize_function_frame_layout(AArch64MachineFunction &machine_function,
                                   std::size_t current_offset) {
        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);
        initialize_aarch64_function_frame_record(machine_function, frame_size,
                                                machine_function.get_has_calls());
    }

    void reserve_variadic_va_list_support_areas(
        const CoreIrFunction &function, const AArch64FunctionAbiInfo &abi_info,
        AArch64MachineFunction &machine_function, std::size_t &current_offset) {
        if (!function.get_is_variadic()) {
            return;
        }
        unsigned named_gpr_slots = 0;
        unsigned named_fpr_slots = 0;
        std::size_t incoming_stack_offset = 16;
        for (const AArch64AbiAssignment &assignment : abi_info.parameters) {
            incoming_stack_offset =
                std::max(incoming_stack_offset,
                         assignment.locations.empty()
                             ? incoming_stack_offset
                             : assignment.locations.front().kind ==
                                       AArch64AbiLocationKind::Stack
                                   ? assignment.locations.front().stack_offset +
                                         assignment.stack_size
                                   : incoming_stack_offset);
            for (const AArch64AbiLocation &location : assignment.locations) {
                if (location.kind == AArch64AbiLocationKind::GeneralRegister) {
                    named_gpr_slots = std::max(
                        named_gpr_slots,
                        location.physical_reg -
                            static_cast<unsigned>(AArch64PhysicalReg::X0) + 1U);
                } else if (location.kind ==
                           AArch64AbiLocationKind::FloatingRegister) {
                    named_fpr_slots = std::max(
                        named_fpr_slots,
                        location.physical_reg -
                            static_cast<unsigned>(AArch64PhysicalReg::V0) + 1U);
                }
            }
        }
        current_offset = align_to(current_offset, 16);
        current_offset += 8 * 8;
        machine_function.get_frame_info().set_variadic_gpr_save_area_offset(
            current_offset);
        current_offset = align_to(current_offset, 16);
        current_offset += 8 * 16;
        machine_function.get_frame_info().set_variadic_fpr_save_area_offset(
            current_offset);
        machine_function.get_frame_info().set_variadic_incoming_stack_offset(
            incoming_stack_offset);
        machine_function.get_frame_info().set_variadic_named_gpr_slots(
            named_gpr_slots);
        machine_function.get_frame_info().set_variadic_named_fpr_slots(
            named_fpr_slots);
    }

    bool plan_function_stack_and_values(const CoreIrFunction &function,
                                        FunctionState &state,
                                        AArch64FunctionLoweringFacade &facade) {
        std::size_t current_offset = 0;
        sysycc::layout_stack_slots(*state.machine_function, function,
                                   current_offset);

        if (!seed_function_value_locations(function, state, current_offset,
                                           facade)) {
            return false;
        }
        sysycc::seed_call_argument_copy_slots(
            function, state.call_state.indirect_call_argument_copy_offsets,
            current_offset, facade);
        sysycc::seed_promoted_stack_slots(
            function, state.value_state.value_locations,
            state.value_state.promoted_stack_slots);
        reserve_variadic_va_list_support_areas(function, state.call_state.abi_info,
                                               *state.machine_function,
                                               current_offset);
        finalize_function_frame_layout(*state.machine_function, current_offset);
        return true;
    }

    bool plan_function_control_flow(const CoreIrFunction &function,
                                    FunctionState &state,
                                    AArch64FunctionLoweringFacade &facade) {
        block_labels_ =
            build_aarch64_function_block_labels(function, function.get_name());
        for (const auto &[block, label] : block_labels_) {
            (void)block;
            record_symbol_definition(label, AArch64SymbolKind::Label,
                                     AArch64SectionKind::Text, false);
        }
        state.control_state.phi_edge_labels.clear();
        state.control_state.phi_edge_plans.clear();
        if (!sysycc::build_phi_edge_plans(function, facade,
                                          state.control_state.phi_edge_labels,
                                          state.control_state.phi_edge_plans)) {
            return false;
        }
        return true;
    }

    bool initialize_function_state(AArch64MachineModule &machine_module,
                                   const CoreIrFunction &function,
                                   FunctionState &state) {
        AArch64FunctionPlanningFacade planning_facade(*this);
        if (!sysycc::validate_function_lowering_readiness(function,
                                                          planning_facade)) {
            return false;
        }

        state.machine_function =
            &append_machine_function(machine_module, function);
        state.call_state.abi_info =
            abi_lowering_pass_.classify_function(function);
        AArch64FunctionLoweringFacade facade(*this, state);

        if (!plan_function_stack_and_values(function, state, facade)) {
            return false;
        }

        return plan_function_control_flow(function, state, facade);
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
                             FunctionState &state,
                             AArch64FunctionLoweringFacade &facade) {
        AArch64MachineBlock &prologue_block =
            machine_function.append_block(function.get_name());
        append_aarch64_standard_prologue(
            prologue_block, machine_function.get_frame_info().get_frame_size(),
            machine_function.get_has_calls());
        if (state.call_state.abi_info.return_value.is_indirect) {
            state.value_state.indirect_result_address =
                create_pointer_virtual_reg(machine_function);
            append_copy_from_physical_reg(
                prologue_block, state.value_state.indirect_result_address,
                static_cast<unsigned>(AArch64PhysicalReg::X8),
                AArch64VirtualRegKind::General64);
        }
        if (function.get_is_variadic()) {
            static CoreIrIntegerType i64_type(64);
            static CoreIrFloatType f128_type(CoreIrFloatKind::Float128);
            if (const auto gpr_offset =
                    machine_function.get_frame_info()
                        .get_variadic_gpr_save_area_offset();
                gpr_offset.has_value()) {
                const AArch64VirtualReg base =
                    create_pointer_virtual_reg(machine_function);
                append_frame_address(prologue_block, base, *gpr_offset,
                                     machine_function);
                for (unsigned index = 0; index < 8; ++index) {
                    const AArch64VirtualReg temp =
                        machine_function.create_virtual_reg(
                            AArch64VirtualRegKind::General64);
                    append_copy_from_physical_reg(
                        prologue_block, temp,
                        static_cast<unsigned>(AArch64PhysicalReg::X0) + index,
                        AArch64VirtualRegKind::General64);
                    if (!facade.append_store_to_address(
                            prologue_block, &i64_type, temp, base, index * 8,
                            machine_function)) {
                        return false;
                    }
                }
            }
            if (const auto fpr_offset =
                    machine_function.get_frame_info()
                        .get_variadic_fpr_save_area_offset();
                fpr_offset.has_value()) {
                const AArch64VirtualReg base =
                    create_pointer_virtual_reg(machine_function);
                append_frame_address(prologue_block, base, *fpr_offset,
                                     machine_function);
                for (unsigned index = 0; index < 8; ++index) {
                    const AArch64VirtualReg temp =
                        machine_function.create_virtual_reg(
                            AArch64VirtualRegKind::Float128);
                    append_copy_from_physical_reg(
                        prologue_block, temp,
                        static_cast<unsigned>(AArch64PhysicalReg::V0) + index,
                        AArch64VirtualRegKind::Float128);
                    if (!facade.append_store_to_address(
                            prologue_block, &f128_type, temp, base, index * 16,
                            machine_function)) {
                        return false;
                    }
                }
            }
        }
        return lower_function_entry_parameters(prologue_block, function,
                                               state.call_state.abi_info,
                                               *state.machine_function, facade);
    }

    bool emit_function_body(AArch64MachineFunction &machine_function,
                            const CoreIrFunction &function,
                            FunctionState &state,
                            AArch64FunctionLoweringFacade &facade) {
        for (const auto &basic_block : function.get_basic_blocks()) {
            AArch64MachineBlock &machine_block = machine_function.append_block(
                block_labels_.at(basic_block.get()));
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!emit_instruction(machine_function, machine_block,
                                      basic_block.get(), *instruction, state,
                                      facade)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool emit_phi_edge_blocks(AArch64MachineFunction &machine_function,
                              const FunctionState &state,
                              AArch64FunctionLoweringFacade &facade) {
        for (const AArch64PhiEdgePlan &plan :
             state.control_state.phi_edge_plans) {
            if (!emit_phi_edge_block(machine_function, plan, state, facade)) {
                return false;
            }
        }
        return true;
    }

    void emit_function_exit(AArch64MachineFunction &machine_function) {
        AArch64MachineBlock &epilogue_block = machine_function.append_block(
            machine_function.get_epilogue_label());
        append_aarch64_standard_epilogue(
            epilogue_block, machine_function.get_frame_info().get_frame_size(),
            machine_function.get_has_calls());
    }

    void refresh_function_shell_state(AArch64MachineFunction &machine_function) {
        const bool has_calls = machine_function_contains_call(machine_function);
        machine_function.set_has_calls(has_calls);

        const std::size_t frame_size = machine_function.get_frame_info().get_frame_size();
        AArch64MachineBlock &entry_block = machine_function.get_blocks().front();
        if (has_calls && frame_size == 0 &&
            count_aarch64_standard_prologue_prefix(entry_block.get_instructions()) == 0) {
            std::vector<AArch64MachineInstr> refreshed_prologue;
            for (const AArch64StandardFrameShellOp &op :
                 build_aarch64_standard_prologue_shell(frame_size, has_calls)) {
                refreshed_prologue.push_back(op.instruction);
            }
            refreshed_prologue.insert(refreshed_prologue.end(),
                                      entry_block.get_instructions().begin(),
                                      entry_block.get_instructions().end());
            entry_block.get_instructions() = std::move(refreshed_prologue);
        }

        initialize_aarch64_function_frame_record(machine_function, frame_size,
                                                has_calls);
    }

    bool emit_function(AArch64MachineFunction &machine_function,
                       const CoreIrFunction &function, FunctionState &state) {
        AArch64FunctionLoweringFacade facade(*this, state);
        if (!emit_function_entry(machine_function, function, state, facade)) {
            return false;
        }
        if (!emit_function_body(machine_function, function, state, facade)) {
            return false;
        }
        if (!emit_phi_edge_blocks(machine_function, state, facade)) {
            return false;
        }
        refresh_function_shell_state(machine_function);
        emit_function_exit(machine_function);
        return true;
    }

    bool emit_instruction(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrBasicBlock *current_block,
                          const CoreIrInstruction &instruction,
                          FunctionState &state,
                          AArch64FunctionLoweringFacade &facade) {
        return sysycc::dispatch_aarch64_lowered_instruction(
            facade, machine_function, machine_block, current_block, instruction,
            state);
    }

    bool materialize_value(
        AArch64MachineBlock &machine_block, const CoreIrValue *value,
        const AArch64VirtualReg &target_reg, const FunctionState &state,
        AArch64ValueMaterializationContext &context) override {
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::VirtualReg) {
            if (location->virtual_reg.get_id() != target_reg.get_id()) {
                append_register_copy(machine_block, target_reg,
                                     location->virtual_reg);
            }
            return true;
        }

        return materialize_noncanonical_value(machine_block, value, target_reg,
                                              state, context);
    }

    bool materialize_noncanonical_value(
        AArch64MachineBlock &machine_block, const CoreIrValue *value,
        const AArch64VirtualReg &target_reg, const FunctionState &state,
        AArch64ValueMaterializationContext &context) override {
        return sysycc::materialize_noncanonical_value(
            machine_block, context, value, target_reg, *state.machine_function);
    }

    bool emit_phi_edge_block(AArch64MachineFunction &machine_function,
                             const AArch64PhiEdgePlan &plan,
                             const FunctionState &state,
                             AArch64FunctionLoweringFacade &facade) {
        AArch64MachineBlock &edge_block =
            machine_function.append_block(plan.edge_label);
        if (!sysycc::emit_parallel_phi_copies(edge_block, plan,
                                              machine_function, facade)) {
            return false;
        }
        edge_block.append_instruction(AArch64MachineInstr(
            "b", {AArch64MachineOperand::label(
                     block_labels_.at(plan.edge.successor))}));
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

    bool append_memory_store(AArch64MachineBlock &machine_block,
                             const CoreIrType *type,
                             const AArch64MachineOperand &source_operand,
                             const AArch64VirtualReg &address_reg,
                             std::size_t offset,
                             AArch64MachineFunction &function) {
        return sysycc::append_memory_store(machine_block, *this, type,
                                           source_operand, address_reg, offset,
                                           function);
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
        sysycc::append_load_from_frame(machine_block, *this, type, target_reg,
                                       offset, function);
    }

    void append_store_to_frame(AArch64MachineBlock &machine_block,
                               const CoreIrType *type,
                               const AArch64VirtualReg &source_reg,
                               std::size_t offset,
                               AArch64MachineFunction &function) {
        sysycc::append_store_to_frame(machine_block, *this, type, source_reg,
                                      offset, function);
    }

    void append_load_from_incoming_stack_arg(
        AArch64MachineBlock &machine_block, const CoreIrType *type,
        const AArch64VirtualReg &target_reg, std::size_t offset,
        AArch64MachineFunction &function) override {
        sysycc::append_load_from_incoming_stack_arg(
            machine_block, *this, type, target_reg, offset, function);
    }

    bool append_load_from_address(AArch64MachineBlock &machine_block,
                                  const CoreIrType *type,
                                  const AArch64VirtualReg &target_reg,
                                  const AArch64VirtualReg &address_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override {
        return sysycc::append_load_from_address(machine_block, *this, type,
                                                target_reg, address_reg, offset,
                                                function);
    }

    bool append_store_to_address(AArch64MachineBlock &machine_block,
                                 const CoreIrType *type,
                                 const AArch64VirtualReg &source_reg,
                                 const AArch64VirtualReg &address_reg,
                                 std::size_t offset,
                                 AArch64MachineFunction &function) override {
        return sysycc::append_store_to_address(machine_block, *this, type,
                                               source_reg, address_reg, offset,
                                               function);
    }

    bool emit_memory_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &destination_address,
                          const AArch64VirtualReg &source_address,
                          const CoreIrType *type,
                          AArch64MachineFunction &function) override {
        return sysycc::emit_memory_copy(machine_block, *this,
                                        destination_address, source_address,
                                        type, function);
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override {
        sysycc::apply_truncate_to_virtual_reg(machine_block, reg, type);
    }

    void apply_zero_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) override {
        sysycc::apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                 source_type, target_type);
    }

    void apply_sign_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) override {
        sysycc::apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                 source_type, target_type);
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override {
        if (offset <= 4095) {
            machine_block.append_instruction(AArch64MachineInstr(
                "sub", {def_vreg_operand(target_reg),
                        AArch64MachineOperand::physical_reg(
                            static_cast<unsigned>(AArch64PhysicalReg::X29),
                            AArch64VirtualRegKind::General64),
                        AArch64MachineOperand::immediate("#" + std::to_string(offset))}));
            return;
        }
        const AArch64VirtualReg offset_reg =
            create_pointer_virtual_reg(function);
        sysycc::materialize_integer_constant(
            machine_block, *this, create_fake_pointer_type(),
            static_cast<std::uint64_t>(offset), offset_reg);
        machine_block.append_instruction(AArch64MachineInstr(
            "sub", {def_vreg_operand(target_reg),
                    AArch64MachineOperand::physical_reg(
                        static_cast<unsigned>(AArch64PhysicalReg::X29),
                        AArch64VirtualRegKind::General64),
                    use_vreg_operand(offset_reg)}));
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg,
                             std::uint64_t magnitude, bool is_negative,
                             AArch64MachineFunction &function) {
        if (magnitude == 0) {
            return true;
        }
        if (magnitude <= 4095) {
            machine_block.append_instruction(AArch64MachineInstr(
                is_negative ? "sub" : "add",
                {def_vreg_operand(base_reg), use_vreg_operand(base_reg),
                 AArch64MachineOperand::immediate("#" + std::to_string(magnitude))}));
            return true;
        }
        const AArch64VirtualReg offset_reg =
            create_pointer_virtual_reg(function);
        if (!sysycc::materialize_integer_constant(machine_block, *this,
                                                  create_fake_pointer_type(),
                                                  magnitude, offset_reg)) {
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            is_negative ? "sub" : "add",
            {def_vreg_operand(base_reg), use_vreg_operand(base_reg),
             use_vreg_operand(offset_reg)}));
        return true;
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg,
                             long long offset,
                             AArch64MachineFunction &function) override {
        const bool is_negative = offset < 0;
        const std::uint64_t magnitude =
            is_negative ? static_cast<std::uint64_t>(-(offset + 1)) + 1ULL
                        : static_cast<std::uint64_t>(offset);
        return add_constant_offset(machine_block, base_reg, magnitude,
                                   is_negative, function);
    }
};

} // namespace

bool AArch64MachineLoweringPass::run(
    AArch64CodegenContext &codegen_context) const {
    AArch64LoweringSession session(codegen_context);
    return session.Lower();
}

} // namespace sysycc
