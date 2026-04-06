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

#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_cast_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_compare_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_unary_lowering_support.hpp"
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
constexpr unsigned kCallerSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X9),
    static_cast<unsigned>(AArch64PhysicalReg::X10),
    static_cast<unsigned>(AArch64PhysicalReg::X11),
    static_cast<unsigned>(AArch64PhysicalReg::X12),
    static_cast<unsigned>(AArch64PhysicalReg::X13),
    static_cast<unsigned>(AArch64PhysicalReg::X14),
    static_cast<unsigned>(AArch64PhysicalReg::X15),
};
constexpr unsigned kCalleeSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X19),
    static_cast<unsigned>(AArch64PhysicalReg::X20),
    static_cast<unsigned>(AArch64PhysicalReg::X21),
    static_cast<unsigned>(AArch64PhysicalReg::X22),
    static_cast<unsigned>(AArch64PhysicalReg::X23),
};
constexpr unsigned kCallerSavedAllocatableFloatPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::V16),
    static_cast<unsigned>(AArch64PhysicalReg::V17),
    static_cast<unsigned>(AArch64PhysicalReg::V18),
    static_cast<unsigned>(AArch64PhysicalReg::V19),
    static_cast<unsigned>(AArch64PhysicalReg::V20),
    static_cast<unsigned>(AArch64PhysicalReg::V21),
    static_cast<unsigned>(AArch64PhysicalReg::V22),
    static_cast<unsigned>(AArch64PhysicalReg::V23),
    static_cast<unsigned>(AArch64PhysicalReg::V24),
    static_cast<unsigned>(AArch64PhysicalReg::V25),
    static_cast<unsigned>(AArch64PhysicalReg::V26),
    static_cast<unsigned>(AArch64PhysicalReg::V27),
};
constexpr unsigned kCalleeSavedAllocatableFloatPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::V8),
    static_cast<unsigned>(AArch64PhysicalReg::V9),
    static_cast<unsigned>(AArch64PhysicalReg::V10),
    static_cast<unsigned>(AArch64PhysicalReg::V11),
    static_cast<unsigned>(AArch64PhysicalReg::V12),
    static_cast<unsigned>(AArch64PhysicalReg::V13),
    static_cast<unsigned>(AArch64PhysicalReg::V14),
    static_cast<unsigned>(AArch64PhysicalReg::V15),
};
constexpr unsigned kSpillScratchPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X24),
    static_cast<unsigned>(AArch64PhysicalReg::X25),
    static_cast<unsigned>(AArch64PhysicalReg::X26),
    static_cast<unsigned>(AArch64PhysicalReg::X27),
};
constexpr unsigned kSpillAddressPhysicalReg =
    static_cast<unsigned>(AArch64PhysicalReg::X28);
constexpr unsigned kSpillScratchFloatPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::V28),
    static_cast<unsigned>(AArch64PhysicalReg::V29),
    static_cast<unsigned>(AArch64PhysicalReg::V30),
    static_cast<unsigned>(AArch64PhysicalReg::V31),
};

bool is_float_physical_reg(unsigned reg) {
    return reg >= static_cast<unsigned>(AArch64PhysicalReg::V0) &&
           reg <= static_cast<unsigned>(AArch64PhysicalReg::V31);
}

bool is_callee_saved_allocatable_physical_reg(unsigned reg) {
    return std::find(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                     std::end(kCalleeSavedAllocatablePhysicalRegs),
                     reg) != std::end(kCalleeSavedAllocatablePhysicalRegs);
}

bool is_callee_saved_allocatable_float_physical_reg(unsigned reg) {
    return std::find(std::begin(kCalleeSavedAllocatableFloatPhysicalRegs),
                     std::end(kCalleeSavedAllocatableFloatPhysicalRegs),
                     reg) != std::end(kCalleeSavedAllocatableFloatPhysicalRegs);
}

AArch64CallClobberMask make_default_call_clobber_mask() {
    std::set<unsigned> regs = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                               13, 14, 15, 16, 17};
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V0));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V1));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V2));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V3));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V4));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V5));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V6));
    regs.insert(static_cast<unsigned>(AArch64PhysicalReg::V7));
    regs.insert(std::begin(kCallerSavedAllocatableFloatPhysicalRegs),
                std::end(kCallerSavedAllocatableFloatPhysicalRegs));
    regs.insert(std::begin(kSpillScratchFloatPhysicalRegs),
                std::end(kSpillScratchFloatPhysicalRegs));
    return AArch64CallClobberMask(std::move(regs));
}

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

std::string sanitize_label_fragment(const std::string &text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "unnamed";
    }
    return sanitized;
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

void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &dst_reg,
                                   unsigned physical_reg,
                                   AArch64VirtualRegKind physical_kind) {
    const std::string physical_name =
        render_physical_register(physical_reg, physical_kind);
    if (dst_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(dst_reg.get_kind()) + " " +
                                         def_vreg(dst_reg) + ", " + physical_name);
        return;
    }
    machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                     physical_name);
}

void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                 unsigned physical_reg,
                                 AArch64VirtualRegKind physical_kind,
                                 const AArch64VirtualReg &src_reg) {
    const std::string physical_name =
        render_physical_register(physical_reg, physical_kind);
    if (src_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(src_reg.get_kind()) + " " +
                                         physical_name + ", " + use_vreg(src_reg));
        return;
    }
    machine_block.append_instruction("mov " + physical_name + ", " +
                                     use_vreg_as_kind(
                                         src_reg,
                                         uses_general_64bit_register(physical_kind)
                                             ? AArch64VirtualRegKind::General64
                                             : AArch64VirtualRegKind::General32));
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
    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64CodegenContext &codegen_context_;
    AArch64AbiLoweringPass abi_lowering_pass_;
    AArch64MachineModule *machine_module_ = nullptr;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;

    struct FunctionState {
        AArch64MachineFunction *machine_function = nullptr;
        AArch64FunctionAbiInfo abi_info;
        AArch64VirtualReg indirect_result_address;
        std::unordered_map<const CoreIrParameter *, std::size_t>
            incoming_stack_argument_offsets;
        std::unordered_map<const CoreIrValue *, AArch64ValueLocation> value_locations;
        std::unordered_map<const CoreIrValue *, std::size_t> aggregate_value_offsets;
        std::unordered_map<const CoreIrCallInst *, std::vector<std::size_t>>
            indirect_call_argument_copy_offsets;
        std::unordered_set<const CoreIrStackSlot *> promoted_stack_slots;
        std::unordered_map<const CoreIrStackSlot *, AArch64VirtualReg>
            promoted_stack_slot_values;
        std::unordered_map<AArch64PhiEdgeKey, std::string, AArch64PhiEdgeKeyHash>
            phi_edge_labels;
        std::vector<AArch64PhiEdgePlan> phi_edge_plans;
        unsigned last_debug_file_id = 0;
        int last_debug_line = 0;
        int last_debug_column = 0;
    };

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

        GlobalDataLoweringContext global_data_context(*this);
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

    class FunctionPlanningContextAdapter final
        : public AArch64FunctionPlanningContext {
      private:
        AArch64LoweringSession &session_;
        const AArch64AbiLoweringPass &abi_lowering_pass_;

      public:
        FunctionPlanningContextAdapter(
            AArch64LoweringSession &session,
            const AArch64AbiLoweringPass &abi_lowering_pass)
            : session_(session), abi_lowering_pass_(abi_lowering_pass) {}

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }

        AArch64VirtualReg
        create_virtual_reg(AArch64MachineFunction &function,
                           const CoreIrType *type) override {
            return session_.create_virtual_reg(function, type);
        }

        AArch64VirtualReg
        create_pointer_virtual_reg(AArch64MachineFunction &function) override {
            return session_.create_pointer_virtual_reg(function);
        }

        AArch64FunctionAbiInfo classify_call(
            const CoreIrCallInst &call) const override {
            return abi_lowering_pass_.classify_call(call);
        }
    };

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
            make_default_call_clobber_mask()));
    }

    const AArch64ValueLocation *
    lookup_value_location(const FunctionState &state,
                          const CoreIrValue *value) const {
        const auto it = state.value_locations.find(value);
        if (it == state.value_locations.end()) {
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
        const auto offset_it = state.aggregate_value_offsets.find(value);
        if (offset_it == state.aggregate_value_offsets.end()) {
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
        FunctionPlanningContextAdapter planning_context(*this, abi_lowering_pass_);
        return sysycc::seed_function_value_locations(
            function, *state.machine_function, state.value_locations,
            state.aggregate_value_offsets, current_offset, planning_context);
    }

    std::string resolve_branch_target_label(const FunctionState &state,
                                            const CoreIrBasicBlock *predecessor,
                                            const CoreIrBasicBlock *successor) const {
        const auto edge_it =
            state.phi_edge_labels.find(AArch64PhiEdgeKey{predecessor, successor});
        if (edge_it != state.phi_edge_labels.end()) {
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
        if (state.last_debug_file_id == debug_file_id &&
            state.last_debug_line == line &&
            state.last_debug_column == column) {
            return;
        }
        machine_block.append_instruction(".loc " + std::to_string(debug_file_id) + " " +
                                         std::to_string(line) + " " +
                                         std::to_string(column));
        state.last_debug_file_id = debug_file_id;
        state.last_debug_line = line;
        state.last_debug_column = column;
    }

    bool is_promoted_stack_slot(const FunctionState &state,
                                const CoreIrStackSlot *stack_slot) const {
        return stack_slot != nullptr &&
               state.promoted_stack_slots.find(stack_slot) !=
                   state.promoted_stack_slots.end();
    }

    bool append_functions(AArch64MachineModule &machine_module) {
        for (const auto &function : module_.get_functions()) {
            if (!append_function(machine_module, *function)) {
                return false;
            }
        }
        return true;
    }

    bool append_function(AArch64MachineModule &machine_module,
                         const CoreIrFunction &function) {
        if (function.get_basic_blocks().empty()) {
            return true;
        }
        FunctionPlanningContextAdapter planning_context(*this, abi_lowering_pass_);
        if (!sysycc::validate_function_lowering_readiness(function,
                                                          planning_context)) {
            return false;
        }

        const std::string function_name = function.get_name();
        AArch64MachineFunction &machine_function = machine_module.append_function(
            function_name, !function.get_is_internal_linkage(),
            ".L" + sanitize_label_fragment(function_name) + "_epilogue");
        record_symbol_definition(function_name, AArch64SymbolKind::Function,
                                 AArch64SectionKind::Text,
                                 !function.get_is_internal_linkage());
        FunctionState state;
        state.machine_function = &machine_function;
        state.abi_info = abi_lowering_pass_.classify_function(function);

        std::size_t current_offset = 0;
        sysycc::seed_incoming_stack_argument_offsets(
            function, state.abi_info, state.incoming_stack_argument_offsets);
        sysycc::layout_stack_slots(machine_function, function, current_offset);

        if (!seed_function_value_locations(function, state, current_offset)) {
            return false;
        }
        FunctionPlanningContextAdapter copy_slot_planning_context(
            *this, abi_lowering_pass_);
        sysycc::seed_call_argument_copy_slots(
            function, state.indirect_call_argument_copy_offsets, current_offset,
            copy_slot_planning_context);
        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);
        machine_function.set_section_kind(AArch64SectionKind::Text);
        machine_function.get_frame_record().set_stack_frame_size(frame_size);
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::StartProcedure});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfa,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), 16});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), -16});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::Offset,
                                static_cast<unsigned>(AArch64PhysicalReg::X30), -8});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaRegister,
                                static_cast<unsigned>(AArch64PhysicalReg::X29), 0});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::DefCfaOffset,
                                static_cast<unsigned>(AArch64PhysicalReg::X29),
                                static_cast<long long>(frame_size + 16)});
        machine_function.get_frame_record().append_cfi_directive(
            AArch64CfiDirective{AArch64CfiDirectiveKind::EndProcedure});
        sysycc::seed_promoted_stack_slots(function, state.value_locations,
                                          state.promoted_stack_slots);

        block_labels_.clear();
        for (const auto &basic_block : function.get_basic_blocks()) {
            block_labels_.emplace(
                basic_block.get(),
                ".L" + sanitize_label_fragment(function_name) + "_" +
                    sanitize_label_fragment(basic_block->get_name()));
        }
        state.phi_edge_labels.clear();
        state.phi_edge_plans.clear();
        class PhiPlanContext final : public AArch64PhiPlanContext {
          private:
            const AArch64LoweringSession &session_;

          public:
            explicit PhiPlanContext(const AArch64LoweringSession &session)
                : session_(session) {}

            void report_error(const std::string &message) const override {
                add_backend_error(session_.diagnostic_engine_, message);
            }

            const std::string &
            block_label(const CoreIrBasicBlock *block) const override {
                return session_.block_labels_.at(block);
            }
        };

        PhiPlanContext phi_plan_context(*this);
        if (!sysycc::build_phi_edge_plans(function, phi_plan_context,
                                          state.phi_edge_labels,
                                          state.phi_edge_plans)) {
            return false;
        }

        return emit_function(machine_function, function, state);
    }

    bool emit_function(AArch64MachineFunction &machine_function,
                       const CoreIrFunction &function, FunctionState &state) {
        AbiEmissionContext abi_context(*this, state);
        AArch64MachineBlock &prologue_block =
            machine_function.append_block(function.get_name());
        prologue_block.append_instruction("stp x29, x30, [sp, #-16]!");
        prologue_block.append_instruction("mov x29, sp");
        if (machine_function.get_frame_info().get_frame_size() > 0) {
            prologue_block.append_instruction(
                "sub sp, sp, #" +
                std::to_string(machine_function.get_frame_info().get_frame_size()));
        }
        if (state.abi_info.return_value.is_indirect) {
            state.indirect_result_address =
                create_pointer_virtual_reg(machine_function);
            append_copy_from_physical_reg(
                prologue_block, state.indirect_result_address,
                static_cast<unsigned>(AArch64PhysicalReg::X8),
                AArch64VirtualRegKind::General64);
        }
        if (!lower_function_entry_parameters(prologue_block, function, state.abi_info,
                                             *state.machine_function,
                                             abi_context)) {
            return false;
        }

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
        for (const AArch64PhiEdgePlan &plan : state.phi_edge_plans) {
            if (!emit_phi_edge_block(machine_function, plan, state)) {
                return false;
            }
        }

        AArch64MachineBlock &epilogue_block =
            machine_function.append_block(machine_function.get_epilogue_label());
        if (machine_function.get_frame_info().get_frame_size() > 0) {
            epilogue_block.append_instruction(
                "add sp, sp, #" +
                std::to_string(machine_function.get_frame_info().get_frame_size()));
        }
        epilogue_block.append_instruction("ldp x29, x30, [sp], #16");
        epilogue_block.append_instruction("ret");
        return true;
    }

    bool emit_instruction(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrBasicBlock *current_block,
                          const CoreIrInstruction &instruction,
                          FunctionState &state) {
        switch (instruction.get_opcode()) {
        case CoreIrOpcode::Phi:
            return true;
        case CoreIrOpcode::Load:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_load(machine_block,
                             static_cast<const CoreIrLoadInst &>(instruction),
                             state);
        case CoreIrOpcode::Store:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_store(machine_block,
                              static_cast<const CoreIrStoreInst &>(instruction),
                              state);
        case CoreIrOpcode::Binary:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_binary(machine_block,
                               static_cast<const CoreIrBinaryInst &>(instruction),
                               state);
        case CoreIrOpcode::Unary:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_unary(machine_block,
                              static_cast<const CoreIrUnaryInst &>(instruction),
                              state);
        case CoreIrOpcode::Compare:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_compare(machine_block,
                                static_cast<const CoreIrCompareInst &>(instruction),
                                state);
        case CoreIrOpcode::Cast:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_cast(machine_block,
                             static_cast<const CoreIrCastInst &>(instruction), state);
        case CoreIrOpcode::Call:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_call(machine_block,
                             static_cast<const CoreIrCallInst &>(instruction), state);
        case CoreIrOpcode::Jump:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            machine_block.append_instruction(
                "b " + resolve_branch_target_label(
                          state, current_block,
                          static_cast<const CoreIrJumpInst &>(instruction)
                              .get_target_block()));
            return true;
        case CoreIrOpcode::CondJump:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_cond_jump(machine_block,
                                  static_cast<const CoreIrCondJumpInst &>(instruction),
                                  state, current_block);
        case CoreIrOpcode::Return:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_return(machine_function, machine_block,
                               static_cast<const CoreIrReturnInst &>(instruction),
                               state);
        case CoreIrOpcode::AddressOfStackSlot:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_address_of_stack_slot(
                machine_block,
                static_cast<const CoreIrAddressOfStackSlotInst &>(instruction), state);
        case CoreIrOpcode::AddressOfGlobal:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_address_of_global(
                machine_block,
                static_cast<const CoreIrAddressOfGlobalInst &>(instruction), state);
        case CoreIrOpcode::AddressOfFunction:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_address_of_function(
                machine_block,
                static_cast<const CoreIrAddressOfFunctionInst &>(instruction), state);
        case CoreIrOpcode::GetElementPtr:
            emit_debug_location(machine_block, instruction.get_source_span(), state);
            return emit_getelementptr(machine_block,
                                      static_cast<const CoreIrGetElementPtrInst &>(instruction),
                                      state);
        }
        return false;
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
        ValueMaterializationContext value_context(*this, state);
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
        class PhiCopyContext final : public AArch64PhiCopyLoweringContext {
          private:
            AArch64LoweringSession &session_;
            const FunctionState &state_;

          public:
            PhiCopyContext(AArch64LoweringSession &session, const FunctionState &state)
                : session_(session), state_(state) {}

            void report_error(const std::string &message) override {
                add_backend_error(session_.diagnostic_engine_, message);
            }

            bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const override {
                return session_.require_canonical_vreg(state_, value, out);
            }

            bool try_get_value_vreg(const CoreIrValue *value,
                                    AArch64VirtualReg &out) const override {
                if (const AArch64ValueLocation *location =
                        session_.lookup_value_location(state_, value);
                    location != nullptr &&
                    location->kind == AArch64ValueLocationKind::VirtualReg &&
                    location->virtual_reg.is_valid()) {
                    out = location->virtual_reg;
                    return true;
                }
                return false;
            }

            bool materialize_value(AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   const AArch64VirtualReg &target_reg) override {
                return session_.materialize_value(machine_block, value, target_reg,
                                                 state_);
            }
        };

        PhiCopyContext phi_context(*this, state);
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

    class AbiEmissionContext final : public AArch64AbiEmissionContext {
      private:
        AArch64LoweringSession &session_;
        const FunctionState &state_;

      public:
        AbiEmissionContext(AArch64LoweringSession &session,
                           const FunctionState &state)
            : session_(session), state_(state) {}

        AArch64VirtualReg
        create_pointer_virtual_reg(AArch64MachineFunction &function) override {
            return session_.create_pointer_virtual_reg(function);
        }

        const CoreIrType *create_fake_pointer_type() const override {
            return session_.create_fake_pointer_type();
        }

        void append_frame_address(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &target_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override {
            session_.append_frame_address(machine_block, target_reg, offset,
                                          function);
        }

        bool add_constant_offset(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &base_reg,
                                 long long offset,
                                 AArch64MachineFunction &function) override {
            return session_.add_constant_offset(machine_block, base_reg, offset,
                                                function);
        }

        bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                  const CoreIrValue *value,
                                  AArch64VirtualReg &out) override {
            return session_.ensure_value_in_vreg(machine_block, value, state_, out);
        }

        bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                            const CoreIrValue *value,
                                            AArch64VirtualReg &out) override {
            return session_.ensure_value_in_memory_address(machine_block, value,
                                                           state_, out);
        }

        bool materialize_canonical_memory_address(
            AArch64MachineBlock &machine_block, const CoreIrValue *value,
            AArch64VirtualReg &out) override {
            return session_.materialize_canonical_memory_address(machine_block,
                                                                 state_, value, out);
        }

        bool require_canonical_vreg(const CoreIrValue *value,
                                    AArch64VirtualReg &out) const override {
            return session_.require_canonical_vreg(state_, value, out);
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

        void append_load_from_incoming_stack_arg(
            AArch64MachineBlock &machine_block, const CoreIrType *type,
            const AArch64VirtualReg &target_reg, std::size_t stack_offset,
            AArch64MachineFunction &function) override {
            sysycc::append_load_from_incoming_stack_arg(machine_block, *this, type,
                                                        target_reg, stack_offset,
                                                        function);
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

        bool materialize_incoming_stack_address(
            AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
            std::size_t stack_offset, AArch64MachineFunction &function) override {
            machine_block.append_instruction("mov " + def_vreg(target_reg) + ", x29");
            return session_.add_constant_offset(machine_block, target_reg,
                                                static_cast<long long>(stack_offset),
                                                function);
        }

        void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &reg,
                                           const CoreIrType *type) override {
            session_.apply_truncate_to_virtual_reg(machine_block, reg, type);
        }

        bool emit_memory_copy(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &destination_address,
                              const AArch64VirtualReg &source_address,
                              const CoreIrType *value_type,
                              AArch64MachineFunction &function) override {
            return sysycc::emit_memory_copy(machine_block, *this,
                                            destination_address, source_address,
                                            value_type, function);
        }

        std::optional<AArch64VirtualReg>
        prepare_stack_argument_area(AArch64MachineBlock &machine_block,
                                    std::size_t stack_arg_bytes,
                                    AArch64MachineFunction &function) override {
            if (stack_arg_bytes == 0) {
                return std::nullopt;
            }
            machine_block.append_instruction("sub sp, sp, #" +
                                             std::to_string(stack_arg_bytes));
            AArch64VirtualReg stack_base =
                session_.create_pointer_virtual_reg(function);
            machine_block.append_instruction("mov " + def_vreg(stack_base) +
                                             ", sp");
            return stack_base;
        }

        void finish_stack_argument_area(AArch64MachineBlock &machine_block,
                                        std::size_t stack_arg_bytes) override {
            if (stack_arg_bytes > 0) {
                machine_block.append_instruction("add sp, sp, #" +
                                                 std::to_string(stack_arg_bytes));
            }
        }

        void emit_direct_call(AArch64MachineBlock &machine_block,
                              const std::string &callee_name) override {
            session_.record_symbol_reference(callee_name,
                                             AArch64SymbolKind::Function);
            machine_block.append_instruction(AArch64MachineInstr(
                "bl", {AArch64MachineOperand(callee_name)},
                AArch64InstructionFlags{.is_call = true}, {}, {},
                make_default_call_clobber_mask()));
        }

        bool emit_indirect_call(AArch64MachineBlock &machine_block,
                                const AArch64VirtualReg &callee_reg) override {
            machine_block.append_instruction(AArch64MachineInstr(
                "blr", {AArch64MachineOperand(use_vreg(callee_reg))},
                AArch64InstructionFlags{.is_call = true}, {}, {},
                make_default_call_clobber_mask()));
            return true;
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }
    };

    class AddressMaterializationContext final
        : public AArch64AddressMaterializationContext {
      private:
        AArch64LoweringSession &session_;
        const FunctionState &state_;

      public:
        AddressMaterializationContext(AArch64LoweringSession &session,
                                      const FunctionState &state)
            : session_(session), state_(state) {}

        AArch64VirtualReg
        create_pointer_virtual_reg(AArch64MachineFunction &function) override {
            return session_.create_pointer_virtual_reg(function);
        }

        const CoreIrType *create_fake_pointer_type() const override {
            return session_.create_fake_pointer_type();
        }

        bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                  const CoreIrValue *value,
                                  AArch64VirtualReg &out) override {
            return session_.ensure_value_in_vreg(machine_block, value, state_, out);
        }

        bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                          const CoreIrType *type,
                                          std::uint64_t value,
                                          const AArch64VirtualReg &target_reg,
                                          AArch64MachineFunction &function) override {
            return sysycc::materialize_integer_constant(machine_block, session_, type,
                                                        value, target_reg);
        }

        bool add_constant_offset(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &base_reg,
                                 long long offset,
                                 AArch64MachineFunction &function) override {
            return session_.add_constant_offset(machine_block, base_reg, offset,
                                                function);
        }

        void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        void record_symbol_reference(const std::string &name,
                                     AArch64SymbolKind kind) override {
            session_.record_symbol_reference(name, kind);
        }

        bool is_position_independent() const override {
            return session_.backend_options_.get_position_independent();
        }

        bool is_nonpreemptible_global_symbol(const std::string &name) const override {
            return session_.is_nonpreemptible_global_symbol(name);
        }

        bool is_nonpreemptible_function_symbol(const std::string &name) const override {
            return session_.is_nonpreemptible_function_symbol(name);
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }
    };

    class ValueMaterializationContext final
        : public AArch64ValueMaterializationContext {
      private:
        AArch64LoweringSession &session_;
        const FunctionState &state_;

      public:
        ValueMaterializationContext(AArch64LoweringSession &session,
                                    const FunctionState &state)
            : session_(session), state_(state) {}

        AArch64VirtualReg
        create_pointer_virtual_reg(AArch64MachineFunction &function) override {
            return session_.create_pointer_virtual_reg(function);
        }

        const CoreIrType *create_fake_pointer_type() const override {
            return session_.create_fake_pointer_type();
        }

        bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                  const CoreIrValue *value,
                                  AArch64VirtualReg &out) override {
            return session_.ensure_value_in_vreg(machine_block, value, state_, out);
        }

        bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                          const CoreIrType *type,
                                          std::uint64_t value,
                                          const AArch64VirtualReg &target_reg,
                                          AArch64MachineFunction &function) override {
            return sysycc::materialize_integer_constant(machine_block, session_, type,
                                                        value, target_reg);
        }

        bool add_constant_offset(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &base_reg,
                                 long long offset,
                                 AArch64MachineFunction &function) override {
            return session_.add_constant_offset(machine_block, base_reg, offset,
                                                function);
        }

        void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        void record_symbol_reference(const std::string &name,
                                     AArch64SymbolKind kind) override {
            session_.record_symbol_reference(name, kind);
        }

        bool is_position_independent() const override {
            return session_.backend_options_.get_position_independent();
        }

        bool is_nonpreemptible_global_symbol(const std::string &name) const override {
            return session_.is_nonpreemptible_global_symbol(name);
        }

        bool is_nonpreemptible_function_symbol(const std::string &name) const override {
            return session_.is_nonpreemptible_function_symbol(name);
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }

        void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &reg,
                                           const CoreIrType *type) override {
            session_.apply_truncate_to_virtual_reg(machine_block, reg, type);
        }

        void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                         unsigned physical_reg,
                                         AArch64VirtualRegKind reg_kind,
                                         const AArch64VirtualReg &source_reg) override {
            ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                                  reg_kind, source_reg);
        }

        void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &target_reg,
                                           unsigned physical_reg,
                                           AArch64VirtualRegKind reg_kind) override {
            ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                    physical_reg, reg_kind);
        }

        void append_helper_call(AArch64MachineBlock &machine_block,
                                const std::string &symbol_name) override {
            session_.append_helper_call(machine_block, symbol_name);
        }

        void append_frame_address(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &target_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override {
            session_.append_frame_address(machine_block, target_reg, offset,
                                          function);
        }

        std::size_t get_stack_slot_offset(
            const CoreIrStackSlot *stack_slot) const override {
            return state_.machine_function->get_frame_info().get_stack_slot_offset(
                stack_slot);
        }
    };

    class MemoryValueLoweringContext final
        : public AArch64MemoryValueLoweringContext {
      private:
        AArch64LoweringSession &session_;
        const FunctionState &state_;

      public:
        MemoryValueLoweringContext(AArch64LoweringSession &session,
                                   const FunctionState &state)
            : session_(session), state_(state) {}

        AArch64VirtualReg
        create_pointer_virtual_reg(AArch64MachineFunction &function) override {
            return session_.create_pointer_virtual_reg(function);
        }

        const CoreIrType *create_fake_pointer_type() const override {
            return session_.create_fake_pointer_type();
        }

        void append_frame_address(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &target_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override {
            session_.append_frame_address(machine_block, target_reg, offset,
                                          function);
        }

        bool add_constant_offset(AArch64MachineBlock &machine_block,
                                 const AArch64VirtualReg &base_reg,
                                 long long offset,
                                 AArch64MachineFunction &function) override {
            return session_.add_constant_offset(machine_block, base_reg, offset,
                                                function);
        }

        bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                  const CoreIrValue *value,
                                  AArch64VirtualReg &out) override {
            return session_.ensure_value_in_vreg(machine_block, value, state_, out);
        }

        bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                            const CoreIrValue *value,
                                            AArch64VirtualReg &out) override {
            return session_.ensure_value_in_memory_address(machine_block, value,
                                                           state_, out);
        }

        bool materialize_canonical_memory_address(
            AArch64MachineBlock &machine_block, const CoreIrValue *value,
            AArch64VirtualReg &out) override {
            return session_.materialize_canonical_memory_address(machine_block,
                                                                 state_, value, out);
        }

        bool require_canonical_vreg(const CoreIrValue *value,
                                    AArch64VirtualReg &out) const override {
            return session_.require_canonical_vreg(state_, value, out);
        }

        std::size_t get_stack_slot_offset(
            const CoreIrStackSlot *stack_slot) const override {
            return state_.machine_function->get_frame_info().get_stack_slot_offset(
                stack_slot);
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }
    };

    class FloatHelperLoweringContext final
        : public AArch64FloatHelperLoweringContext {
      private:
        AArch64LoweringSession &session_;

      public:
        explicit FloatHelperLoweringContext(AArch64LoweringSession &session)
            : session_(session) {}

        const CoreIrType *create_fake_pointer_type() const override {
            return session_.create_fake_pointer_type();
        }

        void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                         unsigned physical_reg,
                                         AArch64VirtualRegKind reg_kind,
                                         const AArch64VirtualReg &source_reg) override {
            ::sysycc::append_copy_to_physical_reg(machine_block, physical_reg,
                                                  reg_kind, source_reg);
        }

        void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &target_reg,
                                           unsigned physical_reg,
                                           AArch64VirtualRegKind reg_kind) override {
            ::sysycc::append_copy_from_physical_reg(machine_block, target_reg,
                                                    physical_reg, reg_kind);
        }

        void append_helper_call(AArch64MachineBlock &machine_block,
                                const std::string &symbol_name) override {
            session_.append_helper_call(machine_block, symbol_name);
        }

        void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                           const AArch64VirtualReg &reg,
                                           const CoreIrType *type) override {
            session_.apply_truncate_to_virtual_reg(machine_block, reg, type);
        }

        void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                              const AArch64VirtualReg &dst_reg,
                                              const CoreIrType *source_type,
                                              const CoreIrType *target_type) override {
            session_.apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                      source_type, target_type);
        }

        AArch64VirtualReg promote_float16_to_float32(
            AArch64MachineBlock &machine_block, const AArch64VirtualReg &source_reg,
            AArch64MachineFunction &function) override {
            return sysycc::promote_float16_to_float32(machine_block, source_reg,
                                                      function);
        }

        void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &source_reg,
                                       const AArch64VirtualReg &target_reg) override {
            sysycc::demote_float32_to_float16(machine_block, source_reg, target_reg);
        }

        bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                        const CoreIrConstantFloat &constant,
                                        const AArch64VirtualReg &target_reg,
                                        AArch64MachineFunction &function) override {
            return sysycc::materialize_float_constant(machine_block, session_,
                                                      constant, target_reg,
                                                      function);
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }
    };

    class GlobalDataLoweringContext final
        : public AArch64GlobalDataLoweringContext {
      private:
        AArch64LoweringSession &session_;

      public:
        explicit GlobalDataLoweringContext(AArch64LoweringSession &session)
            : session_(session) {}

        void record_symbol_definition(const std::string &name,
                                      AArch64SymbolKind kind,
                                      AArch64SectionKind section_kind,
                                      bool is_global_symbol) override {
            session_.record_symbol_definition(name, kind, section_kind,
                                              is_global_symbol);
        }

        void record_symbol_reference(const std::string &name,
                                     AArch64SymbolKind kind) override {
            session_.record_symbol_reference(name, kind);
        }

        void report_error(const std::string &message) override {
            add_backend_error(session_.diagnostic_engine_, message);
        }
    };

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
        append_frame_address(
            machine_block, target_reg,
            state.machine_function->get_frame_info().get_stack_slot_offset(
                address_of_stack_slot.get_stack_slot()),
            *state.machine_function);
        return true;
    }

    bool emit_address_of_global(AArch64MachineBlock &machine_block,
                                const CoreIrAddressOfGlobalInst &address_of_global,
                                const FunctionState &state) {
        AddressMaterializationContext address_context(*this, state);
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_global, target_reg)) {
            return false;
        }
        return sysycc::materialize_global_address(
            machine_block, address_context, address_of_global.get_global()->get_name(),
            target_reg);
    }

    bool emit_address_of_function(AArch64MachineBlock &machine_block,
                                  const CoreIrAddressOfFunctionInst &address_of_function,
                                  const FunctionState &state) {
        AddressMaterializationContext address_context(*this, state);
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_function, target_reg)) {
            return false;
        }
        return sysycc::materialize_global_address(
            machine_block, address_context,
            address_of_function.get_function()->get_name(), target_reg,
            AArch64SymbolKind::Function);
    }

    bool emit_getelementptr(AArch64MachineBlock &machine_block,
                            const CoreIrGetElementPtrInst &gep,
                            const FunctionState &state) {
        AddressMaterializationContext address_context(*this, state);
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &gep, target_reg)) {
            return false;
        }
        const auto *base_pointer_type =
            dynamic_cast<const CoreIrPointerType *>(gep.get_base()->get_type());
        if (base_pointer_type == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "unsupported gep base in AArch64 native backend");
            return false;
        }
        return sysycc::materialize_gep_value(
            machine_block, address_context, gep.get_base(),
            base_pointer_type->get_pointee_type(),
            gep.get_index_count(),
            [&gep](std::size_t index) -> CoreIrValue * { return gep.get_index(index); },
            target_reg, *state.machine_function);
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   const FunctionState &state) {
        if (load.get_stack_slot() != nullptr) {
            if (is_promoted_stack_slot(state, load.get_stack_slot())) {
                AArch64VirtualReg value_reg;
                if (!require_canonical_vreg(state, &load, value_reg)) {
                    return false;
                }
                const auto value_it =
                    state.promoted_stack_slot_values.find(load.get_stack_slot());
                if (value_it == state.promoted_stack_slot_values.end()) {
                    add_backend_error(
                        diagnostic_engine_,
                        "promoted stack slot loaded before it has a canonical value");
                    return false;
                }
            if (value_it->second.get_id() != value_reg.get_id()) {
                    append_register_copy(machine_block, value_reg, value_it->second);
                }
                return true;
            }
        }
        MemoryValueLoweringContext memory_context(*this, state);
        return sysycc::emit_nonpromoted_load(machine_block, memory_context, load,
                                             *state.machine_function);
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, FunctionState &state) {
        if (store.get_stack_slot() != nullptr &&
            is_promoted_stack_slot(state, store.get_stack_slot())) {
            AArch64VirtualReg value_reg;
            if (!ensure_value_in_vreg(machine_block, store.get_value(), state, value_reg)) {
                return false;
            }
            state.promoted_stack_slot_values[store.get_stack_slot()] = value_reg;
            return true;
        }
        MemoryValueLoweringContext memory_context(*this, state);
        return sysycc::emit_nonpromoted_store(machine_block, memory_context, store,
                                              *state.machine_function);
    }

    bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                     CoreIrBinaryOpcode opcode,
                                     const AArch64VirtualReg &lhs_reg,
                                     const AArch64VirtualReg &rhs_reg,
                                     const AArch64VirtualReg &dst_reg) {
        FloatHelperLoweringContext float_context(*this);
        return sysycc::emit_float128_binary_helper(machine_block, float_context, opcode,
                                                   lhs_reg, rhs_reg, dst_reg);
    }

    bool emit_float128_compare_helper(AArch64MachineBlock &machine_block,
                                      CoreIrComparePredicate predicate,
                                      const AArch64VirtualReg &lhs_reg,
                                      const AArch64VirtualReg &rhs_reg,
                                      const AArch64VirtualReg &dst_reg,
                                      AArch64MachineFunction &function) {
        FloatHelperLoweringContext float_context(*this);
        return sysycc::emit_float128_compare_helper(
            machine_block, float_context, predicate, lhs_reg, rhs_reg, dst_reg,
            function);
    }

    bool prepare_integer_value_for_runtime_helper(AArch64MachineBlock &machine_block,
                                                  const CoreIrType *source_type,
                                                  CoreIrCastKind cast_kind,
                                                  const AArch64VirtualReg &source_reg,
                                                  const AArch64VirtualReg &prepared_reg) {
        append_register_copy(machine_block, prepared_reg, source_reg);
        if (cast_kind == CoreIrCastKind::SignedIntToFloat) {
            apply_sign_extend_to_virtual_reg(machine_block, prepared_reg, source_type,
                                             prepared_reg.get_kind() ==
                                                     AArch64VirtualRegKind::General64
                                                 ? create_fake_pointer_type()
                                                 : source_type);
        } else {
            apply_zero_extend_to_virtual_reg(machine_block, prepared_reg, source_type,
                                             prepared_reg.get_kind() ==
                                                     AArch64VirtualRegKind::General64
                                                 ? create_fake_pointer_type()
                                                 : source_type);
        }
        return true;
    }

    bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                                   const CoreIrCastInst &cast,
                                   const AArch64VirtualReg &operand_reg,
                                   const AArch64VirtualReg &dst_reg,
                                   AArch64MachineFunction &function) {
        FloatHelperLoweringContext float_context(*this);
        return sysycc::emit_float128_cast_helper(machine_block, float_context, cast,
                                                 operand_reg, dst_reg, function);
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary, const FunctionState &state) {
        AArch64VirtualReg lhs_reg;
        AArch64VirtualReg rhs_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, binary.get_lhs(), state, lhs_reg) ||
            !ensure_value_in_vreg(machine_block, binary.get_rhs(), state, rhs_reg) ||
            !require_canonical_vreg(state, &binary, dst_reg)) {
            return false;
        }
        std::string opcode;
        if (is_float_type(binary.get_type())) {
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_binary_helper(machine_block,
                                                   binary.get_binary_opcode(), lhs_reg,
                                                   rhs_reg, dst_reg);
            }
        }
        return sysycc::emit_non_float128_binary(machine_block, binary, lhs_reg,
                                                rhs_reg, dst_reg,
                                                *state.machine_function,
                                                diagnostic_engine_);
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        AArch64VirtualReg operand_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, unary.get_operand(), state, operand_reg) ||
            !require_canonical_vreg(state, &unary, dst_reg)) {
            return false;
        }
        switch (unary.get_unary_opcode()) {
        case CoreIrUnaryOpcode::Negate:
            if (is_float_type(unary.get_type()) &&
                dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                static CoreIrFloatType float128_type(CoreIrFloatKind::Float128);
                const CoreIrConstantFloat zero_constant(&float128_type, "0.0");
                const AArch64VirtualReg zero_reg =
                    state.machine_function->create_virtual_reg(
                        AArch64VirtualRegKind::Float128);
                if (!sysycc::materialize_float_constant(
                        machine_block, *this, zero_constant, zero_reg,
                        *state.machine_function) ||
                    !emit_float128_binary_helper(machine_block,
                                                 CoreIrBinaryOpcode::Sub, zero_reg,
                                                 operand_reg, dst_reg)) {
                    return false;
                }
                break;
            }
            return sysycc::emit_non_float128_unary(machine_block, unary, operand_reg,
                                                   dst_reg,
                                                   *state.machine_function,
                                                   diagnostic_engine_);
        case CoreIrUnaryOpcode::BitwiseNot:
        case CoreIrUnaryOpcode::LogicalNot:
            if (unary.get_unary_opcode() == CoreIrUnaryOpcode::LogicalNot &&
                is_float_type(unary.get_operand()->get_type()) &&
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                static CoreIrFloatType float128_type(CoreIrFloatKind::Float128);
                const CoreIrConstantFloat zero_constant(&float128_type, "0.0");
                const AArch64VirtualReg zero_reg =
                    state.machine_function->create_virtual_reg(
                        AArch64VirtualRegKind::Float128);
                if (!sysycc::materialize_float_constant(
                        machine_block, *this, zero_constant, zero_reg,
                        *state.machine_function) ||
                    !emit_float128_compare_helper(
                        machine_block, CoreIrComparePredicate::Equal, operand_reg,
                        zero_reg, dst_reg, *state.machine_function)) {
                    return false;
                }
                break;
            }
            return sysycc::emit_non_float128_unary(machine_block, unary, operand_reg,
                                                   dst_reg,
                                                   *state.machine_function,
                                                   diagnostic_engine_);
        }
        return true;
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        AArch64VirtualReg lhs_reg;
        AArch64VirtualReg rhs_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, compare.get_lhs(), state, lhs_reg) ||
            !ensure_value_in_vreg(machine_block, compare.get_rhs(), state, rhs_reg) ||
            !require_canonical_vreg(state, &compare, dst_reg)) {
            return false;
        }
        if (lhs_reg.get_kind() == AArch64VirtualRegKind::Float128) {
            return emit_float128_compare_helper(machine_block,
                                                compare.get_predicate(), lhs_reg,
                                                rhs_reg, dst_reg,
                                                *state.machine_function);
        }
        return sysycc::emit_non_float128_compare(machine_block, compare, lhs_reg,
                                                 rhs_reg, dst_reg,
                                                 *state.machine_function);
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        AArch64VirtualReg operand_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, cast.get_operand(), state, operand_reg) ||
            !require_canonical_vreg(state, &cast, dst_reg)) {
            return false;
        }
        if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
            operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
            return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                             dst_reg, *state.machine_function);
        }
        return sysycc::emit_non_float128_cast(machine_block, cast, operand_reg,
                                              dst_reg, *state.machine_function);
    }

    bool emit_call(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                   const FunctionState &state) {
        AbiEmissionContext abi_context(*this, state);
        const AArch64FunctionAbiInfo abi_info = abi_lowering_pass_.classify_call(call);
        const auto call_copy_it =
            state.indirect_call_argument_copy_offsets.find(&call);
        const std::vector<std::size_t> *indirect_copy_offsets =
            call_copy_it == state.indirect_call_argument_copy_offsets.end()
                ? nullptr
                : &call_copy_it->second;
        return sysycc::emit_call_with_abi(
            machine_block, call, abi_info, *state.machine_function, abi_context,
            indirect_copy_offsets);
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
        AbiEmissionContext abi_context(*this, state);
        return sysycc::emit_function_return(
            machine_function, machine_block, return_inst, state.abi_info,
            state.indirect_result_address, abi_context);
    }
};

} // namespace

bool AArch64MachineLoweringPass::run(AArch64CodegenContext &codegen_context) const {
    AArch64LoweringSession session(codegen_context);
    return session.Lower();
}

} // namespace sysycc
