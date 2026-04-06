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
#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
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

std::string float_condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
    case CoreIrComparePredicate::UnsignedLess:
        return "lt";
    case CoreIrComparePredicate::SignedLessEqual:
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "le";
    case CoreIrComparePredicate::SignedGreater:
    case CoreIrComparePredicate::UnsignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "ge";
    }
    return "eq";
}

std::string condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
        return "lt";
    case CoreIrComparePredicate::SignedLessEqual:
        return "le";
    case CoreIrComparePredicate::SignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
        return "ge";
    case CoreIrComparePredicate::UnsignedLess:
        return "lo";
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::UnsignedGreater:
        return "hi";
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "hs";
    }
    return "eq";
}

std::string fp_move_mnemonic(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::Float128 ? "mov" : "fmov";
}

void append_register_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &dst_reg,
                          const AArch64VirtualReg &src_reg) {
    if (dst_reg.get_id() == src_reg.get_id() &&
        dst_reg.get_kind() == src_reg.get_kind()) {
        return;
    }
    if (dst_reg.is_floating_point() && src_reg.is_floating_point()) {
        machine_block.append_instruction(fp_move_mnemonic(dst_reg.get_kind()) + " " +
                                         def_vreg(dst_reg) + ", " +
                                         use_vreg_as_kind(src_reg, dst_reg.get_kind()));
        return;
    }
    machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                     use_vreg_as_kind(src_reg, dst_reg.get_kind()));
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

enum class AArch64ValueLocationKind : unsigned char {
    VirtualReg,
    MemoryAddress,
    ConstantInt,
    ConstantNull,
    SymbolAddress,
    StackSlotAddress,
    None,
};

struct AArch64ValueLocation {
    AArch64ValueLocationKind kind = AArch64ValueLocationKind::None;
    AArch64VirtualReg virtual_reg;
};

struct AArch64PhiEdgeKey {
    const CoreIrBasicBlock *predecessor = nullptr;
    const CoreIrBasicBlock *successor = nullptr;

    bool operator==(const AArch64PhiEdgeKey &other) const noexcept {
        return predecessor == other.predecessor && successor == other.successor;
    }
};

struct AArch64PhiEdgeKeyHash {
    std::size_t operator()(const AArch64PhiEdgeKey &key) const noexcept {
        const std::size_t lhs =
            std::hash<const CoreIrBasicBlock *>{}(key.predecessor);
        const std::size_t rhs =
            std::hash<const CoreIrBasicBlock *>{}(key.successor);
        return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
    }
};

struct AArch64PhiCopyOp {
    const CoreIrPhiInst *phi = nullptr;
    const CoreIrValue *source_value = nullptr;
};

struct AArch64PhiEdgePlan {
    AArch64PhiEdgeKey edge;
    std::string edge_label;
    std::vector<AArch64PhiCopyOp> copies;
};

bool is_supported_native_value_type(const CoreIrType *type) {
    return is_void_type(type) || is_supported_scalar_storage_type(type) ||
           is_supported_object_type(type);
}

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

        if (!append_globals(*machine_module_) || !append_functions(*machine_module_)) {
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

    static bool instruction_has_canonical_vreg(const CoreIrInstruction &instruction) {
        switch (instruction.get_opcode()) {
        case CoreIrOpcode::Phi:
        case CoreIrOpcode::Load:
        case CoreIrOpcode::Binary:
        case CoreIrOpcode::Unary:
        case CoreIrOpcode::Compare:
        case CoreIrOpcode::Cast:
        case CoreIrOpcode::Call:
            return !is_void_type(instruction.get_type());
        case CoreIrOpcode::AddressOfStackSlot:
        case CoreIrOpcode::AddressOfGlobal:
        case CoreIrOpcode::AddressOfFunction:
        case CoreIrOpcode::GetElementPtr:
            return true;
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Jump:
        case CoreIrOpcode::CondJump:
        case CoreIrOpcode::Return:
            return false;
        }
        return false;
    }

    void assign_virtual_value_location(FunctionState &state,
                                       const CoreIrValue *value,
                                       AArch64VirtualReg vreg) const {
        state.value_locations[value] =
            AArch64ValueLocation{AArch64ValueLocationKind::VirtualReg, vreg};
    }

    void assign_memory_value_location(FunctionState &state,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg address_vreg,
                                      std::size_t offset) const {
        state.value_locations[value] =
            AArch64ValueLocation{AArch64ValueLocationKind::MemoryAddress,
                                 address_vreg};
        state.aggregate_value_offsets[value] = offset;
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

    std::size_t allocate_aggregate_value_slot(std::size_t &current_offset,
                                              const CoreIrType *type) const {
        current_offset = align_to(current_offset, get_type_alignment(type));
        current_offset += get_type_size(type);
        return current_offset;
    }

    bool seed_function_value_locations(const CoreIrFunction &function,
                                       FunctionState &state,
                                       std::size_t &current_offset) {
        for (const auto &parameter : function.get_parameters()) {
            if (!is_supported_native_value_type(parameter->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported parameter type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
            if (is_aggregate_type(parameter->get_type())) {
                assign_memory_value_location(
                    state, parameter.get(),
                    create_pointer_virtual_reg(*state.machine_function),
                    allocate_aggregate_value_slot(current_offset,
                                                  parameter->get_type()));
            } else {
                assign_virtual_value_location(
                    state, parameter.get(),
                    create_virtual_reg(*state.machine_function, parameter->get_type()));
            }
        }

        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!instruction_has_canonical_vreg(*instruction)) {
                    continue;
                }
                if (!is_supported_native_value_type(instruction->get_type())) {
                    add_backend_error(
                        diagnostic_engine_,
                        "unsupported Core IR value type in AArch64 native backend "
                        "for function '" +
                            function.get_name() + "'");
                    return false;
                }
                if (is_aggregate_type(instruction->get_type())) {
                    assign_memory_value_location(
                        state, instruction.get(),
                        create_pointer_virtual_reg(*state.machine_function),
                        allocate_aggregate_value_slot(current_offset,
                                                      instruction->get_type()));
                } else {
                    assign_virtual_value_location(
                        state, instruction.get(),
                        create_virtual_reg(*state.machine_function,
                                           instruction->get_type()));
                }
            }
        }
        return true;
    }

    std::vector<const CoreIrPhiInst *>
    collect_block_phis(const CoreIrBasicBlock &basic_block) const {
        std::vector<const CoreIrPhiInst *> phis;
        for (const auto &instruction : basic_block.get_instructions()) {
            if (instruction == nullptr ||
                instruction->get_opcode() != CoreIrOpcode::Phi) {
                break;
            }
            phis.push_back(static_cast<const CoreIrPhiInst *>(instruction.get()));
        }
        return phis;
    }

    bool append_predecessor_once(
        std::unordered_map<const CoreIrBasicBlock *,
                           std::vector<const CoreIrBasicBlock *>> &predecessors,
        const CoreIrBasicBlock *successor,
        const CoreIrBasicBlock *predecessor) const {
        if (successor == nullptr || predecessor == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null CFG edge while planning AArch64 phi lowering");
            return false;
        }
        auto &incoming = predecessors[successor];
        if (std::find(incoming.begin(), incoming.end(), predecessor) == incoming.end()) {
            incoming.push_back(predecessor);
        }
        return true;
    }

    bool collect_block_predecessors(
        const CoreIrFunction &function,
        std::unordered_map<const CoreIrBasicBlock *,
                           std::vector<const CoreIrBasicBlock *>> &predecessors) {
        for (const auto &basic_block : function.get_basic_blocks()) {
            if (basic_block == nullptr || basic_block->get_instructions().empty()) {
                continue;
            }
            const CoreIrInstruction *terminator =
                basic_block->get_instructions().back().get();
            if (terminator == nullptr || !terminator->get_is_terminator()) {
                add_backend_error(diagnostic_engine_,
                                  "encountered basic block without terminator while "
                                  "planning AArch64 phi lowering");
                return false;
            }
            switch (terminator->get_opcode()) {
            case CoreIrOpcode::Jump:
                if (!append_predecessor_once(
                        predecessors,
                        static_cast<const CoreIrJumpInst *>(terminator)->get_target_block(),
                        basic_block.get())) {
                    return false;
                }
                break;
            case CoreIrOpcode::CondJump: {
                const auto *cond_jump =
                    static_cast<const CoreIrCondJumpInst *>(terminator);
                if (!append_predecessor_once(predecessors, cond_jump->get_true_block(),
                                             basic_block.get()) ||
                    !append_predecessor_once(predecessors, cond_jump->get_false_block(),
                                             basic_block.get())) {
                    return false;
                }
                break;
            }
            case CoreIrOpcode::Return:
                break;
            default:
                break;
            }
        }
        return true;
    }

    const CoreIrValue *find_phi_incoming_value(const CoreIrPhiInst &phi,
                                               const CoreIrBasicBlock *predecessor) const {
        for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
            if (phi.get_incoming_block(index) == predecessor) {
                return phi.get_incoming_value(index);
            }
        }
        return nullptr;
    }

    bool build_phi_edge_plans(const CoreIrFunction &function, FunctionState &state) {
        std::unordered_map<const CoreIrBasicBlock *, std::vector<const CoreIrBasicBlock *>>
            predecessors;
        if (!collect_block_predecessors(function, predecessors)) {
            return false;
        }

        for (const auto &basic_block : function.get_basic_blocks()) {
            if (basic_block == nullptr) {
                continue;
            }
            const std::vector<const CoreIrPhiInst *> phis =
                collect_block_phis(*basic_block);
            if (phis.empty()) {
                continue;
            }

            const auto predecessor_it = predecessors.find(basic_block.get());
            if (predecessor_it == predecessors.end()) {
                add_backend_error(diagnostic_engine_,
                                  "encountered phi block without predecessors in the "
                                  "AArch64 native backend");
                return false;
            }

            for (const CoreIrBasicBlock *predecessor : predecessor_it->second) {
                AArch64PhiEdgePlan plan;
                plan.edge = AArch64PhiEdgeKey{predecessor, basic_block.get()};
                plan.edge_label =
                    block_labels_.at(predecessor) + "_to_" +
                    sanitize_label_fragment(basic_block->get_name()) + "_phi";
                for (const CoreIrPhiInst *phi : phis) {
                    if (phi == nullptr) {
                        continue;
                    }
                    if (is_aggregate_type(phi->get_type())) {
                        add_backend_error(
                            diagnostic_engine_,
                            "aggregate phi lowering is not supported by the current "
                            "AArch64 native backend");
                        return false;
                    }
                    const CoreIrValue *incoming =
                        find_phi_incoming_value(*phi, predecessor);
                    if (incoming == nullptr) {
                        add_backend_error(
                            diagnostic_engine_,
                            "phi block is missing an incoming value for one of its "
                            "predecessors in the AArch64 native backend");
                        return false;
                    }
                    plan.copies.push_back(AArch64PhiCopyOp{phi, incoming});
                }
                state.phi_edge_labels.emplace(plan.edge, plan.edge_label);
                state.phi_edge_plans.push_back(std::move(plan));
            }
        }
        return true;
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

    void seed_call_argument_copy_slots(const CoreIrFunction &function,
                                       FunctionState &state,
                                       std::size_t &current_offset) {
        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                const auto *call =
                    dynamic_cast<const CoreIrCallInst *>(instruction.get());
                if (call == nullptr) {
                    continue;
                }
                const AArch64FunctionAbiInfo abi_info =
                    abi_lowering_pass_.classify_call(*call);
                std::vector<std::size_t> offsets;
                offsets.resize(abi_info.parameters.size(), 0);
                for (std::size_t index = 0; index < abi_info.parameters.size(); ++index) {
                    if (!abi_info.parameters[index].is_indirect) {
                        continue;
                    }
                    offsets[index] = allocate_aggregate_value_slot(
                        current_offset,
                        call->get_operands()[call->get_argument_begin_index() + index]
                            ->get_type());
                }
                if (!offsets.empty()) {
                    state.indirect_call_argument_copy_offsets.emplace(call,
                                                                      std::move(offsets));
                }
            }
        }
    }

    void seed_promoted_stack_slots(const CoreIrFunction &function,
                                   FunctionState &state) {
        const CoreIrBasicBlock *entry_block =
            function.get_basic_blocks().empty() ? nullptr :
                                                  function.get_basic_blocks().front().get();
        std::unordered_set<const CoreIrStackSlot *> address_taken;
        std::unordered_map<const CoreIrStackSlot *, std::size_t> direct_store_count;
        std::unordered_map<const CoreIrStackSlot *, const CoreIrStoreInst *> entry_store;

        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (const auto *address_of_stack_slot =
                        dynamic_cast<const CoreIrAddressOfStackSlotInst *>(instruction.get());
                    address_of_stack_slot != nullptr) {
                    address_taken.insert(address_of_stack_slot->get_stack_slot());
                    continue;
                }
                const auto *store =
                    dynamic_cast<const CoreIrStoreInst *>(instruction.get());
                if (store == nullptr || store->get_stack_slot() == nullptr) {
                    continue;
                }
                ++direct_store_count[store->get_stack_slot()];
                if (basic_block.get() == entry_block) {
                    entry_store.emplace(store->get_stack_slot(), store);
                }
            }
        }

        for (const auto &stack_slot : function.get_stack_slots()) {
            if (!is_supported_value_type(stack_slot->get_allocated_type())) {
                continue;
            }
            if (address_taken.find(stack_slot.get()) != address_taken.end()) {
                continue;
            }
            const auto count_it = direct_store_count.find(stack_slot.get());
            if (count_it == direct_store_count.end() || count_it->second != 1) {
                continue;
            }
            const auto entry_store_it = entry_store.find(stack_slot.get());
            if (entry_store_it == entry_store.end()) {
                continue;
            }
            const CoreIrValue *stored_value = entry_store_it->second->get_value();
            const AArch64ValueLocation *location =
                lookup_value_location(state, stored_value);
            if (location == nullptr ||
                location->kind != AArch64ValueLocationKind::VirtualReg) {
                continue;
            }
            state.promoted_stack_slots.insert(stack_slot.get());
        }
    }

    bool is_promoted_stack_slot(const FunctionState &state,
                                const CoreIrStackSlot *stack_slot) const {
        return stack_slot != nullptr &&
               state.promoted_stack_slots.find(stack_slot) !=
                   state.promoted_stack_slots.end();
    }

    bool split_constant_address(const CoreIrConstant *constant, std::string &symbol_name,
                                long long &offset) const {
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
            global_address != nullptr) {
            symbol_name = global_address->get_global()->get_name();
            offset = 0;
            return true;
        }
        if (const auto *gep_constant =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
            gep_constant != nullptr) {
            if (!split_constant_address(gep_constant->get_base(), symbol_name, offset)) {
                return false;
            }
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_constant->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                return false;
            }
            const std::optional<long long> gep_offset =
                compute_constant_gep_offset(base_pointer_type->get_pointee_type(),
                                            gep_constant->get_indices());
            if (!gep_offset.has_value()) {
                return false;
            }
            offset += *gep_offset;
            return true;
        }
        return false;
    }

    std::optional<long long>
    compute_constant_gep_offset(const CoreIrType *base_type,
                                const std::vector<const CoreIrConstant *> &indices) const {
        if (base_type == nullptr) {
            return std::nullopt;
        }

        const CoreIrType *current_type = base_type;
        long long offset = 0;
        for (std::size_t index_position = 0; index_position < indices.size();
             ++index_position) {
            const std::optional<long long> maybe_index =
                get_signed_integer_constant(indices[index_position]);
            if (!maybe_index.has_value()) {
                return std::nullopt;
            }

            if (index_position == 0) {
                offset += *maybe_index * static_cast<long long>(get_type_size(current_type));
                continue;
            }

            if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
                array_type != nullptr) {
                offset += *maybe_index *
                          static_cast<long long>(get_type_size(array_type->get_element_type()));
                current_type = array_type->get_element_type();
                continue;
            }

            if (const auto *struct_type =
                    dynamic_cast<const CoreIrStructType *>(current_type);
                struct_type != nullptr) {
                if (*maybe_index < 0 ||
                    static_cast<std::size_t>(*maybe_index) >=
                        struct_type->get_element_types().size()) {
                    return std::nullopt;
                }
                offset += static_cast<long long>(
                    get_struct_member_offset(struct_type,
                                             static_cast<std::size_t>(*maybe_index)));
                current_type =
                    struct_type->get_element_types()[static_cast<std::size_t>(*maybe_index)];
                continue;
            }

            if (const auto *pointer_type =
                    dynamic_cast<const CoreIrPointerType *>(current_type);
                pointer_type != nullptr) {
                offset += *maybe_index * static_cast<long long>(
                                             get_type_size(pointer_type->get_pointee_type()));
                current_type = pointer_type->get_pointee_type();
                continue;
            }

            return std::nullopt;
        }

        return offset;
    }

    std::size_t alignment_to_log2(std::size_t alignment) const {
        if (alignment <= 1) {
            return 0;
        }
        std::size_t log2 = 0;
        while ((static_cast<std::size_t>(1) << log2) < alignment) {
            ++log2;
        }
        return log2;
    }

    void append_data_fragment(AArch64DataObject &data_object, std::string text,
                              std::vector<AArch64RelocationRecord> relocations = {}) {
        data_object.append_fragment(
            AArch64DataFragment(std::move(text), std::move(relocations)));
    }

    bool append_global_constant_fragments(AArch64DataObject &data_object,
                                          const CoreIrConstant *constant,
                                          const CoreIrType *type) {
        if (constant == nullptr) {
            return false;
        }

        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
            append_data_fragment(data_object,
                                 "  .zero " + std::to_string(get_type_size(type)));
            return true;
        }
        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(constant);
            int_constant != nullptr) {
            append_data_fragment(
                data_object,
                "  " + scalar_directive(type) + " " +
                std::to_string(int_constant->get_value()));
            return true;
        }
        if (const auto *float_constant =
                dynamic_cast<const CoreIrConstantFloat *>(constant);
            float_constant != nullptr) {
            const auto *float_type = as_float_type(type);
            if (float_type == nullptr) {
                return false;
            }
            const std::string literal_text =
                strip_floating_literal_suffix(float_constant->get_literal_text());
            try {
                switch (float_type->get_float_kind()) {
                case CoreIrFloatKind::Float16: {
                    const float parsed = std::stof(literal_text);
                    append_data_fragment(
                        data_object,
                        "  .hword " +
                        format_bits_literal(float32_to_float16_bits(parsed), 4));
                    return true;
                }
                case CoreIrFloatKind::Float32: {
                    const float parsed = std::stof(literal_text);
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &parsed, sizeof(bits));
                    append_data_fragment(data_object,
                                         "  .word " +
                                             format_bits_literal(bits, 8));
                    return true;
                }
                case CoreIrFloatKind::Float64: {
                    const double parsed = std::stod(literal_text);
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &parsed, sizeof(bits));
                    append_data_fragment(data_object,
                                         "  .xword " +
                                             format_bits_literal(bits, 16));
                    return true;
                }
                case CoreIrFloatKind::Float128:
                    if (floating_literal_is_zero(literal_text)) {
                        append_data_fragment(data_object, "  .zero 16");
                        return true;
                    }
                    add_backend_error(
                        diagnostic_engine_,
                        "non-zero float128 global initializers are not yet "
                        "supported by the AArch64 native backend");
                    return false;
                }
            } catch (...) {
                add_backend_error(
                    diagnostic_engine_,
                    "failed to parse floating literal for AArch64 global "
                    "constant emission");
                return false;
            }
        }
        if (dynamic_cast<const CoreIrConstantNull *>(constant) != nullptr) {
            append_data_fragment(
                data_object,
                "  " + scalar_directive(type) + " 0");
            return true;
        }
        if (const auto *byte_string =
                dynamic_cast<const CoreIrConstantByteString *>(constant);
            byte_string != nullptr) {
            std::ostringstream bytes_line;
            bytes_line << "  .byte ";
            for (std::size_t index = 0; index < byte_string->get_bytes().size();
                 ++index) {
                if (index > 0) {
                    bytes_line << ", ";
                }
                bytes_line << static_cast<unsigned>(byte_string->get_bytes()[index]);
            }
            append_data_fragment(data_object, bytes_line.str());
            return true;
        }
        if (const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            aggregate != nullptr) {
            const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
            const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
            std::size_t emitted_size = 0;
            for (std::size_t index = 0; index < aggregate->get_elements().size(); ++index) {
                const CoreIrType *element_type = nullptr;
                std::size_t element_offset = emitted_size;
                if (struct_type != nullptr &&
                    index < struct_type->get_element_types().size()) {
                    element_type = struct_type->get_element_types()[index];
                    element_offset = get_struct_member_offset(struct_type, index);
                } else if (array_type != nullptr &&
                           index < array_type->get_element_count()) {
                    element_type = array_type->get_element_type();
                    element_offset = index * get_type_size(element_type);
                }
                if (element_type == nullptr) {
                    return false;
                }
                if (element_offset > emitted_size) {
                    append_data_fragment(data_object,
                                         "  .zero " +
                                             std::to_string(element_offset -
                                                            emitted_size));
                    emitted_size = element_offset;
                }
                if (!append_global_constant_fragments(
                        data_object, aggregate->get_elements()[index], element_type)) {
                    return false;
                }
                emitted_size =
                    std::max(emitted_size, element_offset + get_type_size(element_type));
            }
            if (get_type_size(type) > emitted_size) {
                append_data_fragment(
                    data_object,
                    "  .zero " +
                    std::to_string(get_type_size(type) - emitted_size));
            }
            return true;
        }
        std::string symbol_name;
        long long offset = 0;
        if (split_constant_address(constant, symbol_name, offset)) {
            record_symbol_reference(symbol_name, AArch64SymbolKind::Object);
            std::string line = "  " + scalar_directive(type) + " " + symbol_name;
            if (offset > 0) {
                line += " + " + std::to_string(offset);
            } else if (offset < 0) {
                line += " - " + std::to_string(-offset);
            }
            append_data_fragment(
                data_object, line,
                {AArch64RelocationRecord{
                    get_storage_size(type) <= 4 ? AArch64RelocationKind::Absolute32
                                                : AArch64RelocationKind::Absolute64,
                    symbol_name,
                    offset}});
            return true;
        }

        return false;
    }

    bool append_globals(AArch64MachineModule &machine_module) {
        for (const auto &global : module_.get_globals()) {
            if (!append_global(machine_module, *global)) {
                return false;
            }
        }
        return true;
    }

    bool append_global(AArch64MachineModule &machine_module,
                       const CoreIrGlobal &global) {
        if (global.get_initializer() == nullptr) {
            if (global.get_is_internal_linkage()) {
                add_backend_error(
                    diagnostic_engine_,
                    "AArch64 native backend requires an initializer for internal global '" +
                        global.get_name() + "'");
                return false;
            }
            record_symbol_reference(global.get_name(), AArch64SymbolKind::Object);
            return true;
        }
        if (is_byte_string_global(global)) {
            record_symbol_definition(global.get_name(), AArch64SymbolKind::Object,
                                     AArch64SectionKind::ReadOnlyData,
                                     !global.get_is_internal_linkage());
            AArch64DataObject &data_object = machine_module.append_data_object(
                AArch64SectionKind::ReadOnlyData, global.get_name(),
                !global.get_is_internal_linkage(), 0);
            const auto *byte_string =
                static_cast<const CoreIrConstantByteString *>(global.get_initializer());
            std::ostringstream bytes_line;
            bytes_line << "  .byte ";
            for (std::size_t index = 0; index < byte_string->get_bytes().size();
                 ++index) {
                if (index > 0) {
                    bytes_line << ", ";
                }
                bytes_line << static_cast<unsigned>(byte_string->get_bytes()[index]);
            }
            append_data_fragment(data_object, bytes_line.str());
            return true;
        }

        if (!is_supported_object_type(global.get_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global type in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }

        const AArch64SectionKind section_kind =
            global.get_is_constant() ? AArch64SectionKind::ReadOnlyData
                                     : AArch64SectionKind::Data;
        record_symbol_definition(global.get_name(), AArch64SymbolKind::Object,
                                 section_kind, !global.get_is_internal_linkage());
        AArch64DataObject &data_object = machine_module.append_data_object(
            section_kind, global.get_name(), !global.get_is_internal_linkage(),
            alignment_to_log2(get_type_alignment(global.get_type())));
        if (!append_global_constant_fragments(data_object, global.get_initializer(),
                                              global.get_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global initializer in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }
        return true;
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
        if (!is_supported_native_value_type(
                function.get_function_type()->get_return_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported return type in AArch64 native backend for function '" +
                    function.get_name() + "'");
            return false;
        }
        for (const auto &parameter : function.get_parameters()) {
            if (!is_supported_native_value_type(parameter->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported parameter type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            if (!is_supported_object_type(stack_slot->get_allocated_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported stack slot type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
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
        for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
            const AArch64AbiAssignment &assignment = state.abi_info.parameters[index];
            if (!assignment.locations.empty() &&
                assignment.locations.front().kind == AArch64AbiLocationKind::Stack) {
                state.incoming_stack_argument_offsets.emplace(
                    function.get_parameters()[index].get(),
                    assignment.locations.front().stack_offset);
            }
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            current_offset = align_to(current_offset,
                                      get_type_alignment(stack_slot->get_allocated_type()));
            current_offset += get_type_size(stack_slot->get_allocated_type());
            machine_function.get_frame_info().set_stack_slot_offset(stack_slot.get(),
                                                                    current_offset);
        }

        if (!seed_function_value_locations(function, state, current_offset)) {
            return false;
        }
        seed_call_argument_copy_slots(function, state, current_offset);
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
        seed_promoted_stack_slots(function, state);

        block_labels_.clear();
        for (const auto &basic_block : function.get_basic_blocks()) {
            block_labels_.emplace(
                basic_block.get(),
                ".L" + sanitize_label_fragment(function_name) + "_" +
                    sanitize_label_fragment(basic_block->get_name()));
        }
        state.phi_edge_labels.clear();
        state.phi_edge_plans.clear();
        if (!build_phi_edge_plans(function, state)) {
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
        AddressMaterializationContext address_context(*this, state);
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 lowering");
            return false;
        }

        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(value);
            int_constant != nullptr) {
            return sysycc::materialize_integer_constant(machine_block, *this,
                                                        value->get_type(),
                                                        int_constant->get_value(),
                                                        target_reg);
        }
        if (const auto *float_constant =
                dynamic_cast<const CoreIrConstantFloat *>(value);
            float_constant != nullptr) {
            return sysycc::materialize_float_constant(machine_block, *this,
                                                      *float_constant, target_reg,
                                                      *state.machine_function);
        }
        if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr ||
            dynamic_cast<const CoreIrConstantZeroInitializer *>(value) != nullptr) {
            machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                             zero_register_name(target_reg.get_use_64bit()));
            return true;
        }
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
            global_address != nullptr) {
            return sysycc::materialize_global_address(
                machine_block, address_context,
                global_address->get_global()->get_name(), target_reg,
                AArch64SymbolKind::Object);
        }
        if (const auto *gep_constant =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(value);
            gep_constant != nullptr) {
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_constant->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported constant gep base in AArch64 native backend");
                return false;
            }
            return sysycc::materialize_gep_value(
                machine_block, address_context, gep_constant->get_base(),
                base_pointer_type->get_pointee_type(),
                gep_constant->get_indices().size(),
                [&gep_constant](std::size_t index) -> CoreIrValue * {
                    return const_cast<CoreIrConstant *>(gep_constant->get_indices()[index]);
                },
                target_reg, *state.machine_function);
        }
        if (const auto *address_of_stack_slot =
                dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
            address_of_stack_slot != nullptr) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    address_of_stack_slot->get_stack_slot());
            append_frame_address(machine_block, target_reg, offset,
                                 *state.machine_function);
            return true;
        }
        if (const auto *address_of_global =
                dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
            address_of_global != nullptr) {
            return sysycc::materialize_global_address(
                machine_block, address_context,
                address_of_global->get_global()->get_name(), target_reg,
                AArch64SymbolKind::Object);
        }
        if (const auto *address_of_function =
                dynamic_cast<const CoreIrAddressOfFunctionInst *>(value);
            address_of_function != nullptr) {
            return sysycc::materialize_global_address(
                machine_block, address_context,
                address_of_function->get_function()->get_name(), target_reg,
                AArch64SymbolKind::Function);
        }
        if (const auto *gep_instruction =
                dynamic_cast<const CoreIrGetElementPtrInst *>(value);
            gep_instruction != nullptr) {
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_instruction->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported gep base in AArch64 native backend");
                return false;
            }
            return sysycc::materialize_gep_value(
                machine_block, address_context, gep_instruction->get_base(),
                base_pointer_type->get_pointee_type(),
                gep_instruction->get_index_count(),
                [&gep_instruction](std::size_t index) -> CoreIrValue * {
                    return gep_instruction->get_index(index);
                },
                target_reg, *state.machine_function);
        }

        add_backend_error(diagnostic_engine_,
                          "unsupported Core IR value in AArch64 native backend");
        return false;
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

    bool emit_parallel_phi_copies(AArch64MachineBlock &machine_block,
                                  const AArch64PhiEdgePlan &plan,
                                  const FunctionState &state) {
        struct PendingPhiCopy {
            AArch64VirtualReg destination;
            std::optional<AArch64VirtualReg> source_reg;
            const CoreIrValue *source_value = nullptr;
        };

        std::vector<PendingPhiCopy> pending;
        for (const AArch64PhiCopyOp &copy : plan.copies) {
            if (copy.phi == nullptr || copy.source_value == nullptr) {
                add_backend_error(
                    diagnostic_engine_,
                    "encountered malformed phi copy while lowering the AArch64 "
                    "native backend");
                return false;
            }
            AArch64VirtualReg destination;
            if (!require_canonical_vreg(state, copy.phi, destination)) {
                return false;
            }
            PendingPhiCopy pending_copy;
            pending_copy.destination = destination;
            pending_copy.source_value = copy.source_value;
            if (const AArch64ValueLocation *location =
                    lookup_value_location(state, copy.source_value);
                location != nullptr &&
                location->kind == AArch64ValueLocationKind::VirtualReg &&
                location->virtual_reg.is_valid()) {
                pending_copy.source_reg = location->virtual_reg;
            }
            if (pending_copy.source_reg.has_value() &&
                pending_copy.source_reg->get_id() == pending_copy.destination.get_id() &&
                pending_copy.source_reg->get_kind() ==
                    pending_copy.destination.get_kind()) {
                continue;
            }
            pending.push_back(std::move(pending_copy));
        }

        while (!pending.empty()) {
            bool progressed = false;
            std::unordered_set<std::size_t> pending_destinations;
            for (const PendingPhiCopy &copy : pending) {
                pending_destinations.insert(copy.destination.get_id());
            }

            for (auto it = pending.begin(); it != pending.end(); ++it) {
                const bool source_is_blocked =
                    it->source_reg.has_value() &&
                    pending_destinations.find(it->source_reg->get_id()) !=
                        pending_destinations.end();
                if (source_is_blocked) {
                    continue;
                }
                if (it->source_reg.has_value()) {
                    append_register_copy(machine_block, it->destination,
                                         *it->source_reg);
                } else if (!materialize_value(machine_block, it->source_value,
                                              it->destination, state)) {
                    return false;
                }
                pending.erase(it);
                progressed = true;
                break;
            }

            if (progressed) {
                continue;
            }

            PendingPhiCopy &cycle_head = pending.front();
            if (!cycle_head.source_reg.has_value()) {
                add_backend_error(
                    diagnostic_engine_,
                    "failed to schedule phi copies in the AArch64 native backend");
                return false;
            }
            const AArch64VirtualReg saved_destination =
                state.machine_function->create_virtual_reg(
                    cycle_head.destination.get_kind());
            append_register_copy(machine_block, saved_destination,
                                 cycle_head.destination);
            for (PendingPhiCopy &copy : pending) {
                if (copy.source_reg.has_value() &&
                    copy.source_reg->get_id() == cycle_head.destination.get_id()) {
                    copy.source_reg = saved_destination;
                }
            }
        }

        return true;
    }

    bool emit_phi_edge_block(AArch64MachineFunction &machine_function,
                             const AArch64PhiEdgePlan &plan,
                             const FunctionState &state) {
        AArch64MachineBlock &edge_block =
            machine_function.append_block(plan.edge_label);
        if (!emit_parallel_phi_copies(edge_block, plan, state)) {
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
        const auto *integer_type = as_integer_type(type);
        if (integer_type == nullptr) {
            return;
        }
        switch (integer_type->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false) + ", #1");
            break;
        case 8:
            machine_block.append_instruction("uxtb " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false));
            break;
        case 16:
            machine_block.append_instruction("uxth " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false));
            break;
        default:
            break;
        }
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        const auto *source_integer = as_integer_type(source_type);
        if (source_integer == nullptr) {
            return;
        }
        switch (source_integer->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false) + ", #1");
            break;
        case 8:
            machine_block.append_instruction("uxtb " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false));
            break;
        case 16:
            machine_block.append_instruction("uxth " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false));
            break;
        case 32:
            break;
        default:
            break;
        }
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        const auto *source_integer = as_integer_type(source_type);
        const bool target_uses_64bit = uses_64bit_register(target_type);
        if (source_integer == nullptr) {
            return;
        }
        switch (source_integer->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false) + ", #1");
            machine_block.append_instruction(
                "neg " + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, target_uses_64bit));
            break;
        case 8:
            machine_block.append_instruction(
                std::string("sxtb ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, false));
            break;
        case 16:
            machine_block.append_instruction(
                std::string("sxth ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, false));
            break;
        case 32:
            if (target_uses_64bit) {
                machine_block.append_instruction("sxtw " + def_vreg_as(dst_reg, true) +
                                                 ", " + use_vreg_as(dst_reg, false));
            }
            break;
        default:
            break;
        }
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
        if (is_aggregate_type(load.get_type())) {
            AArch64VirtualReg destination_address;
            if (!materialize_canonical_memory_address(machine_block, state, &load,
                                                      destination_address)) {
                return false;
            }
            AArch64VirtualReg source_address = create_pointer_virtual_reg(*state.machine_function);
            if (load.get_stack_slot() != nullptr) {
                append_frame_address(
                    machine_block, source_address,
                    state.machine_function->get_frame_info().get_stack_slot_offset(
                        load.get_stack_slot()),
                    *state.machine_function);
            } else {
                if (!ensure_value_in_vreg(machine_block, load.get_address(), state,
                                          source_address)) {
                    return false;
                }
            }
            return emit_memory_copy(machine_block, destination_address, source_address,
                                    load.get_type(), *state.machine_function);
        }
        AArch64VirtualReg value_reg;
        if (!require_canonical_vreg(state, &load, value_reg)) {
            return false;
        }
        if (load.get_stack_slot() != nullptr) {
            if (is_promoted_stack_slot(state, load.get_stack_slot())) {
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
            append_load_from_frame(
                machine_block, load.get_type(), value_reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    load.get_stack_slot()),
                *state.machine_function);
        } else {
            AArch64VirtualReg address_reg;
            if (!ensure_value_in_vreg(machine_block, load.get_address(), state,
                                      address_reg)) {
                return false;
            }
            machine_block.append_instruction(load_mnemonic_for_type(load.get_type()) +
                                             " " + def_vreg(value_reg) + ", [" +
                                             use_vreg(address_reg) + "]");
        }
        return true;
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

        if (const auto *zero_initializer =
                dynamic_cast<const CoreIrConstantZeroInitializer *>(store.get_value());
            zero_initializer != nullptr) {
            AArch64VirtualReg address_reg;
            if (store.get_stack_slot() != nullptr) {
                address_reg = create_pointer_virtual_reg(*state.machine_function);
                append_frame_address(
                    machine_block, address_reg,
                    state.machine_function->get_frame_info().get_stack_slot_offset(
                        store.get_stack_slot()),
                    *state.machine_function);
            } else {
                if (!ensure_value_in_vreg(machine_block, store.get_address(), state,
                                          address_reg)) {
                    return false;
                }
            }
            return emit_zero_fill(machine_block, address_reg,
                                  zero_initializer->get_type(),
                                  *state.machine_function);
        }

        if (is_aggregate_type(store.get_value()->get_type())) {
            AArch64VirtualReg source_address;
            if (!ensure_value_in_memory_address(machine_block, store.get_value(), state,
                                                source_address)) {
                return false;
            }
            AArch64VirtualReg destination_address =
                create_pointer_virtual_reg(*state.machine_function);
            if (store.get_stack_slot() != nullptr) {
                append_frame_address(
                    machine_block, destination_address,
                    state.machine_function->get_frame_info().get_stack_slot_offset(
                        store.get_stack_slot()),
                    *state.machine_function);
            } else {
                if (!ensure_value_in_vreg(machine_block, store.get_address(), state,
                                          destination_address)) {
                    return false;
                }
            }
            return emit_memory_copy(machine_block, destination_address, source_address,
                                    store.get_value()->get_type(),
                                    *state.machine_function);
        }

        AArch64VirtualReg value_reg;
        if (!ensure_value_in_vreg(machine_block, store.get_value(), state, value_reg)) {
            return false;
        }
        if (store.get_stack_slot() != nullptr) {
            append_store_to_frame(
                machine_block, store.get_value()->get_type(), value_reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    store.get_stack_slot()),
                *state.machine_function);
            return true;
        }
        AArch64VirtualReg address_reg;
        if (!ensure_value_in_vreg(machine_block, store.get_address(), state,
                                  address_reg)) {
            return false;
        }
        machine_block.append_instruction(store_mnemonic_for_type(store.get_value()->get_type()) +
                                         " " + use_vreg(value_reg) + ", [" +
                                         use_vreg(address_reg) + "]");
        return true;
    }

    bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                     CoreIrBinaryOpcode opcode,
                                     const AArch64VirtualReg &lhs_reg,
                                     const AArch64VirtualReg &rhs_reg,
                                     const AArch64VirtualReg &dst_reg) {
        std::string helper_name;
        switch (opcode) {
        case CoreIrBinaryOpcode::Add:
            helper_name = "__addtf3";
            break;
        case CoreIrBinaryOpcode::Sub:
            helper_name = "__subtf3";
            break;
        case CoreIrBinaryOpcode::Mul:
            helper_name = "__multf3";
            break;
        case CoreIrBinaryOpcode::SDiv:
        case CoreIrBinaryOpcode::UDiv:
            helper_name = "__divtf3";
            break;
        default:
            add_backend_error(
                diagnostic_engine_,
                "unsupported float128 binary opcode in the AArch64 native backend");
            return false;
        }
        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V0),
                                    AArch64VirtualRegKind::Float128, lhs_reg);
        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V1),
                                    AArch64VirtualRegKind::Float128, rhs_reg);
        append_helper_call(machine_block, helper_name);
        append_copy_from_physical_reg(machine_block, dst_reg,
                                      static_cast<unsigned>(AArch64PhysicalReg::V0),
                                      AArch64VirtualRegKind::Float128);
        return true;
    }

    bool emit_float128_compare_helper(AArch64MachineBlock &machine_block,
                                      CoreIrComparePredicate predicate,
                                      const AArch64VirtualReg &lhs_reg,
                                      const AArch64VirtualReg &rhs_reg,
                                      const AArch64VirtualReg &dst_reg,
                                      AArch64MachineFunction &function) {
        const AArch64VirtualReg unordered_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const AArch64VirtualReg compare_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const AArch64VirtualReg unordered_value_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        const bool unordered_result =
            predicate == CoreIrComparePredicate::NotEqual;
        std::string helper_name;
        std::string condition;
        switch (predicate) {
        case CoreIrComparePredicate::Equal:
            helper_name = "__eqtf2";
            condition = "eq";
            break;
        case CoreIrComparePredicate::NotEqual:
            helper_name = "__eqtf2";
            condition = "ne";
            break;
        case CoreIrComparePredicate::SignedLess:
        case CoreIrComparePredicate::UnsignedLess:
            helper_name = "__lttf2";
            condition = "lt";
            break;
        case CoreIrComparePredicate::SignedLessEqual:
        case CoreIrComparePredicate::UnsignedLessEqual:
            helper_name = "__getf2";
            condition = "le";
            break;
        case CoreIrComparePredicate::SignedGreater:
        case CoreIrComparePredicate::UnsignedGreater:
            helper_name = "__getf2";
            condition = "gt";
            break;
        case CoreIrComparePredicate::SignedGreaterEqual:
        case CoreIrComparePredicate::UnsignedGreaterEqual:
            helper_name = "__getf2";
            condition = "ge";
            break;
        }

        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V0),
                                    AArch64VirtualRegKind::Float128, lhs_reg);
        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V1),
                                    AArch64VirtualRegKind::Float128, rhs_reg);
        append_helper_call(machine_block, "__unordtf2");
        machine_block.append_instruction("mov " + def_vreg(unordered_reg) + ", w0");

        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V0),
                                    AArch64VirtualRegKind::Float128, lhs_reg);
        append_copy_to_physical_reg(machine_block,
                                    static_cast<unsigned>(AArch64PhysicalReg::V1),
                                    AArch64VirtualRegKind::Float128, rhs_reg);
        append_helper_call(machine_block, helper_name);
        machine_block.append_instruction("mov " + def_vreg(compare_reg) + ", w0");

        machine_block.append_instruction("cmp " + use_vreg(compare_reg) + ", #0");
        machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", " + condition);
        machine_block.append_instruction("mov " + def_vreg(unordered_value_reg) + ", #" +
                                         std::string(unordered_result ? "1" : "0"));
        machine_block.append_instruction("cmp " + use_vreg(unordered_reg) + ", #0");
        machine_block.append_instruction("csel " + def_vreg(dst_reg) + ", " +
                                         use_vreg(unordered_value_reg) + ", " +
                                         use_vreg(dst_reg) + ", ne");
        return true;
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
        switch (cast.get_cast_kind()) {
        case CoreIrCastKind::SignedIntToFloat:
        case CoreIrCastKind::UnsignedIntToFloat: {
            const bool operand_is_64bit =
                uses_64bit_register(cast.get_operand()->get_type());
            const AArch64VirtualReg prepared_reg = function.create_virtual_reg(
                operand_is_64bit ? AArch64VirtualRegKind::General64
                                 : AArch64VirtualRegKind::General32);
            prepare_integer_value_for_runtime_helper(machine_block,
                                                     cast.get_operand()->get_type(),
                                                     cast.get_cast_kind(), operand_reg,
                                                     prepared_reg);
            const std::string helper_name =
                operand_is_64bit
                    ? (cast.get_cast_kind() == CoreIrCastKind::SignedIntToFloat
                           ? "__floatditf"
                           : "__floatunditf")
                    : (cast.get_cast_kind() == CoreIrCastKind::SignedIntToFloat
                           ? "__floatsitf"
                           : "__floatunsitf");
            append_copy_to_physical_reg(machine_block,
                                        static_cast<unsigned>(AArch64PhysicalReg::X0),
                                        operand_is_64bit
                                            ? AArch64VirtualRegKind::General64
                                            : AArch64VirtualRegKind::General32,
                                        prepared_reg);
            append_helper_call(machine_block, helper_name);
            append_copy_from_physical_reg(machine_block, dst_reg,
                                          static_cast<unsigned>(AArch64PhysicalReg::V0),
                                          AArch64VirtualRegKind::Float128);
            return true;
        }
        case CoreIrCastKind::FloatToSignedInt:
        case CoreIrCastKind::FloatToUnsignedInt: {
            const bool target_is_64bit = uses_64bit_register(cast.get_type());
            const std::string helper_name =
                target_is_64bit
                    ? (cast.get_cast_kind() == CoreIrCastKind::FloatToSignedInt
                           ? "__fixtfdi"
                           : "__fixunstfdi")
                    : (cast.get_cast_kind() == CoreIrCastKind::FloatToSignedInt
                           ? "__fixtfsi"
                           : "__fixunstfsi");
            append_copy_to_physical_reg(machine_block,
                                        static_cast<unsigned>(AArch64PhysicalReg::V0),
                                        AArch64VirtualRegKind::Float128, operand_reg);
            append_helper_call(machine_block, helper_name);
            append_copy_from_physical_reg(machine_block, dst_reg,
                                          static_cast<unsigned>(AArch64PhysicalReg::X0),
                                          target_is_64bit
                                              ? AArch64VirtualRegKind::General64
                                              : AArch64VirtualRegKind::General32);
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            return true;
        }
        case CoreIrCastKind::FloatExtend: {
            if (dst_reg.get_kind() != AArch64VirtualRegKind::Float128) {
                return false;
            }
            if (const auto *float_constant =
                    dynamic_cast<const CoreIrConstantFloat *>(cast.get_operand());
                float_constant != nullptr) {
                const std::string literal_text = float_constant->get_literal_text();
                const bool had_long_double_suffix =
                    !literal_text.empty() &&
                    (literal_text.back() == 'l' || literal_text.back() == 'L');
                if (had_long_double_suffix &&
                    !float128_literal_is_supported_by_helper_path(
                        strip_floating_literal_suffix(literal_text))) {
                    add_backend_error(
                        diagnostic_engine_,
                        "float128 literal is not exactly representable by the current "
                        "AArch64 helper-based materialization path");
                    return false;
                }
            }
            AArch64VirtualReg helper_operand = operand_reg;
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                helper_operand =
                    promote_float16_to_float32(machine_block, operand_reg, function);
            }
            const std::string helper_name =
                helper_operand.get_kind() == AArch64VirtualRegKind::Float32
                    ? "__extendsftf2"
                    : "__extenddftf2";
            append_copy_to_physical_reg(machine_block,
                                        static_cast<unsigned>(AArch64PhysicalReg::V0),
                                        helper_operand.get_kind(), helper_operand);
            append_helper_call(machine_block, helper_name);
            append_copy_from_physical_reg(machine_block, dst_reg,
                                          static_cast<unsigned>(AArch64PhysicalReg::V0),
                                          AArch64VirtualRegKind::Float128);
            return true;
        }
        case CoreIrCastKind::FloatTruncate: {
            if (operand_reg.get_kind() != AArch64VirtualRegKind::Float128) {
                return false;
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg narrowed =
                    function.create_virtual_reg(AArch64VirtualRegKind::Float32);
                append_copy_to_physical_reg(machine_block,
                                            static_cast<unsigned>(AArch64PhysicalReg::V0),
                                            AArch64VirtualRegKind::Float128, operand_reg);
                append_helper_call(machine_block, "__trunctfsf2");
                append_copy_from_physical_reg(machine_block, narrowed,
                                              static_cast<unsigned>(AArch64PhysicalReg::V0),
                                              AArch64VirtualRegKind::Float32);
                demote_float32_to_float16(machine_block, narrowed, dst_reg);
                return true;
            }
            const std::string helper_name =
                dst_reg.get_kind() == AArch64VirtualRegKind::Float32
                    ? "__trunctfsf2"
                    : "__trunctfdf2";
            append_copy_to_physical_reg(machine_block,
                                        static_cast<unsigned>(AArch64PhysicalReg::V0),
                                        AArch64VirtualRegKind::Float128, operand_reg);
            append_helper_call(machine_block, helper_name);
            append_copy_from_physical_reg(machine_block, dst_reg,
                                          static_cast<unsigned>(AArch64PhysicalReg::V0),
                                          dst_reg.get_kind());
            return true;
        }
        default:
            return false;
        }
    }

    AArch64VirtualReg promote_float16_to_float32(AArch64MachineBlock &machine_block,
                                                 const AArch64VirtualReg &source_reg,
                                                 AArch64MachineFunction &function) {
        const AArch64VirtualReg promoted =
            function.create_virtual_reg(AArch64VirtualRegKind::Float32);
        machine_block.append_instruction("fcvt " + def_vreg(promoted) + ", " +
                                         use_vreg(source_reg));
        return promoted;
    }

    void demote_float32_to_float16(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &source_reg,
                                   const AArch64VirtualReg &target_reg) {
        machine_block.append_instruction("fcvt " + def_vreg(target_reg) + ", " +
                                         use_vreg(source_reg));
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
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg lhs32 =
                    promote_float16_to_float32(machine_block, lhs_reg,
                                               *state.machine_function);
                const AArch64VirtualReg rhs32 =
                    promote_float16_to_float32(machine_block, rhs_reg,
                                               *state.machine_function);
                const AArch64VirtualReg result32 =
                    state.machine_function->create_virtual_reg(
                        AArch64VirtualRegKind::Float32);
                switch (binary.get_binary_opcode()) {
                case CoreIrBinaryOpcode::Add:
                    opcode = "fadd";
                    break;
                case CoreIrBinaryOpcode::Sub:
                    opcode = "fsub";
                    break;
                case CoreIrBinaryOpcode::Mul:
                    opcode = "fmul";
                    break;
                case CoreIrBinaryOpcode::SDiv:
                case CoreIrBinaryOpcode::UDiv:
                    opcode = "fdiv";
                    break;
                default:
                    add_backend_error(
                        diagnostic_engine_,
                        "unsupported _Float16 binary opcode in the AArch64 native backend");
                    return false;
                }
                machine_block.append_instruction(opcode + " " + def_vreg(result32) + ", " +
                                                 use_vreg(lhs32) + ", " +
                                                 use_vreg(rhs32));
                demote_float32_to_float16(machine_block, result32, dst_reg);
                return true;
            }
            switch (binary.get_binary_opcode()) {
            case CoreIrBinaryOpcode::Add:
                opcode = "fadd";
                break;
            case CoreIrBinaryOpcode::Sub:
                opcode = "fsub";
                break;
            case CoreIrBinaryOpcode::Mul:
                opcode = "fmul";
                break;
            case CoreIrBinaryOpcode::SDiv:
            case CoreIrBinaryOpcode::UDiv:
                opcode = "fdiv";
                break;
            case CoreIrBinaryOpcode::And:
            case CoreIrBinaryOpcode::Or:
            case CoreIrBinaryOpcode::Xor:
            case CoreIrBinaryOpcode::Shl:
            case CoreIrBinaryOpcode::LShr:
            case CoreIrBinaryOpcode::AShr:
            case CoreIrBinaryOpcode::SRem:
            case CoreIrBinaryOpcode::URem:
                add_backend_error(
                    diagnostic_engine_,
                    "unsupported floating-point binary opcode in the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction(opcode + " " + def_vreg(dst_reg) + ", " +
                                             use_vreg(lhs_reg) + ", " +
                                             use_vreg(rhs_reg));
            return true;
        }
        switch (binary.get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            opcode = "add";
            break;
        case CoreIrBinaryOpcode::Sub:
            opcode = "sub";
            break;
        case CoreIrBinaryOpcode::Mul:
            opcode = "mul";
            break;
        case CoreIrBinaryOpcode::SDiv:
            opcode = "sdiv";
            break;
        case CoreIrBinaryOpcode::UDiv:
            opcode = "udiv";
            break;
        case CoreIrBinaryOpcode::And:
            opcode = "and";
            break;
        case CoreIrBinaryOpcode::Or:
            opcode = "orr";
            break;
        case CoreIrBinaryOpcode::Xor:
            opcode = "eor";
            break;
        case CoreIrBinaryOpcode::Shl:
            opcode = "lsl";
            break;
        case CoreIrBinaryOpcode::LShr:
            opcode = "lsr";
            break;
        case CoreIrBinaryOpcode::AShr:
            opcode = "asr";
            break;
        case CoreIrBinaryOpcode::SRem:
            {
            const AArch64VirtualReg quotient_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            const AArch64VirtualReg product_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("mov " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("sdiv " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("mul " + def_vreg(product_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("sub " + def_vreg(dst_reg) + ", " +
                                             use_vreg(dst_reg) + ", " +
                                             use_vreg(product_reg));
            return true;
            }
        case CoreIrBinaryOpcode::URem:
            {
            const AArch64VirtualReg quotient_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            const AArch64VirtualReg product_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("mov " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("udiv " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("mul " + def_vreg(product_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("sub " + def_vreg(dst_reg) + ", " +
                                             use_vreg(dst_reg) + ", " +
                                             use_vreg(product_reg));
            return true;
            }
        }
        machine_block.append_instruction(opcode + " " + def_vreg(dst_reg) + ", " +
                                         use_vreg(lhs_reg) + ", " +
                                         use_vreg(rhs_reg));
        return true;
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
            if (is_float_type(unary.get_type())) {
                if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
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
                if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                    const AArch64VirtualReg promoted =
                        promote_float16_to_float32(machine_block, operand_reg,
                                                   *state.machine_function);
                    const AArch64VirtualReg negated =
                        state.machine_function->create_virtual_reg(
                            AArch64VirtualRegKind::Float32);
                    machine_block.append_instruction("fneg " + def_vreg(negated) + ", " +
                                                     use_vreg(promoted));
                    demote_float32_to_float16(machine_block, negated, dst_reg);
                } else {
                    machine_block.append_instruction("fneg " + def_vreg(dst_reg) + ", " +
                                                     use_vreg(operand_reg));
                }
            } else {
                machine_block.append_instruction("neg " + def_vreg(dst_reg) + ", " +
                                                 use_vreg(operand_reg));
            }
            break;
        case CoreIrUnaryOpcode::BitwiseNot:
            if (is_float_type(unary.get_type())) {
                add_backend_error(
                    diagnostic_engine_,
                    "bitwise-not on floating-point values is not supported by the "
                    "AArch64 native backend");
                return false;
            }
            machine_block.append_instruction("mvn " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
            break;
        case CoreIrUnaryOpcode::LogicalNot:
            if (is_float_type(unary.get_operand()->get_type())) {
                if (operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
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
                if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                    const AArch64VirtualReg promoted =
                        promote_float16_to_float32(machine_block, operand_reg,
                                                   *state.machine_function);
                    machine_block.append_instruction("fcmp " + use_vreg(promoted) +
                                                     ", #0.0");
                } else {
                    machine_block.append_instruction("fcmp " + use_vreg(operand_reg) +
                                                     ", #0.0");
                }
            } else {
                machine_block.append_instruction("cmp " + use_vreg(operand_reg) + ", #0");
            }
            machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", eq");
            break;
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
        if (is_float_type(compare.get_lhs()->get_type())) {
            if (lhs_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg lhs32 =
                    promote_float16_to_float32(machine_block, lhs_reg,
                                               *state.machine_function);
                const AArch64VirtualReg rhs32 =
                    promote_float16_to_float32(machine_block, rhs_reg,
                                               *state.machine_function);
                machine_block.append_instruction("fcmp " + use_vreg(lhs32) + ", " +
                                                 use_vreg(rhs32));
            } else {
                machine_block.append_instruction("fcmp " + use_vreg(lhs_reg) + ", " +
                                                 use_vreg(rhs_reg));
            }
        } else {
            machine_block.append_instruction("cmp " + use_vreg(lhs_reg) + ", " +
                                             use_vreg(rhs_reg));
        }
        machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", " +
                                         (is_float_type(compare.get_lhs()->get_type())
                                              ? float_condition_code(
                                                    compare.get_predicate())
                                              : condition_code(
                                                    compare.get_predicate())));
        return true;
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        AArch64VirtualReg operand_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, cast.get_operand(), state, operand_reg) ||
            !require_canonical_vreg(state, &cast, dst_reg)) {
            return false;
        }
        switch (cast.get_cast_kind()) {
        case CoreIrCastKind::Truncate:
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(operand_reg,
                                                         uses_64bit_register(cast.get_type())));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::ZeroExtend:
            machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(operand_reg, false));
            apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                             cast.get_operand()->get_type(),
                                             cast.get_type());
            break;
        case CoreIrCastKind::SignExtend:
            if (uses_64bit_register(cast.get_type())) {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
            } else {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg(operand_reg));
            }
            apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                             cast.get_operand()->get_type(),
                                             cast.get_type());
            break;
        case CoreIrCastKind::PtrToInt:
            if (uses_64bit_register(cast.get_operand()->get_type()) &&
                !uses_64bit_register(cast.get_type())) {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
            } else {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg_as(operand_reg,
                                                             uses_64bit_register(cast.get_type())));
            }
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::IntToPtr:
            if (uses_64bit_register(cast.get_operand()->get_type())) {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg_as(operand_reg, true));
            } else {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
                apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                 cast.get_operand()->get_type(),
                                                 cast.get_type());
            }
            break;
        case CoreIrCastKind::SignedIntToFloat:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg widened =
                    state.machine_function->create_virtual_reg(
                        AArch64VirtualRegKind::Float32);
                machine_block.append_instruction("scvtf " + def_vreg(widened) + ", " +
                                                 use_vreg_as(
                                                     operand_reg,
                                                     uses_64bit_register(
                                                         cast.get_operand()->get_type())));
                demote_float32_to_float16(machine_block, widened, dst_reg);
                break;
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                add_backend_error(diagnostic_engine_,
                                  "float128 integer-to-float casts are not yet "
                                  "supported by the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction("scvtf " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(
                                                 operand_reg,
                                                 uses_64bit_register(
                                                     cast.get_operand()->get_type())));
            break;
        case CoreIrCastKind::UnsignedIntToFloat:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg widened =
                    state.machine_function->create_virtual_reg(
                        AArch64VirtualRegKind::Float32);
                machine_block.append_instruction("ucvtf " + def_vreg(widened) + ", " +
                                                 use_vreg_as(
                                                     operand_reg,
                                                     uses_64bit_register(
                                                         cast.get_operand()->get_type())));
                demote_float32_to_float16(machine_block, widened, dst_reg);
                break;
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                add_backend_error(diagnostic_engine_,
                                  "float128 integer-to-float casts are not yet "
                                  "supported by the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction("ucvtf " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(
                                                 operand_reg,
                                                 uses_64bit_register(
                                                     cast.get_operand()->get_type())));
            break;
        case CoreIrCastKind::FloatToSignedInt:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg widened =
                    promote_float16_to_float32(machine_block, operand_reg,
                                               *state.machine_function);
                machine_block.append_instruction("fcvtzs " +
                                                 def_vreg_as(dst_reg,
                                                             uses_64bit_register(cast.get_type())) +
                                                 ", " + use_vreg(widened));
                apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
                break;
            }
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                add_backend_error(diagnostic_engine_,
                                  "float128 float-to-integer casts are not yet "
                                  "supported by the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction("fcvtzs " +
                                             def_vreg_as(dst_reg,
                                                         uses_64bit_register(cast.get_type())) +
                                             ", " + use_vreg(operand_reg));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::FloatToUnsignedInt:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg widened =
                    promote_float16_to_float32(machine_block, operand_reg,
                                               *state.machine_function);
                machine_block.append_instruction("fcvtzu " +
                                                 def_vreg_as(dst_reg,
                                                             uses_64bit_register(cast.get_type())) +
                                                 ", " + use_vreg(widened));
                apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
                break;
            }
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                add_backend_error(diagnostic_engine_,
                                  "float128 float-to-integer casts are not yet "
                                  "supported by the AArch64 native backend");
                return false;
            }
            machine_block.append_instruction("fcvtzu " +
                                             def_vreg_as(dst_reg,
                                                         uses_64bit_register(cast.get_type())) +
                                             ", " + use_vreg(operand_reg));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::FloatExtend:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (operand_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                const AArch64VirtualReg widened =
                    promote_float16_to_float32(machine_block, operand_reg,
                                               *state.machine_function);
                if (dst_reg.get_kind() == AArch64VirtualRegKind::Float32) {
                    append_register_copy(machine_block, dst_reg, widened);
                } else {
                    machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                                     use_vreg(widened));
                }
                break;
            }
            machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
            break;
        case CoreIrCastKind::FloatTruncate:
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float128 ||
                operand_reg.get_kind() == AArch64VirtualRegKind::Float128) {
                return emit_float128_cast_helper(machine_block, cast, operand_reg,
                                                 dst_reg, *state.machine_function);
            }
            if (dst_reg.get_kind() == AArch64VirtualRegKind::Float16) {
                if (operand_reg.get_kind() == AArch64VirtualRegKind::Float32) {
                    demote_float32_to_float16(machine_block, operand_reg, dst_reg);
                } else {
                    const AArch64VirtualReg narrowed =
                        state.machine_function->create_virtual_reg(
                            AArch64VirtualRegKind::Float32);
                    machine_block.append_instruction("fcvt " + def_vreg(narrowed) + ", " +
                                                     use_vreg(operand_reg));
                    demote_float32_to_float16(machine_block, narrowed, dst_reg);
                }
                break;
            }
            machine_block.append_instruction("fcvt " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
            break;
        }
        return true;
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
