#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

bool is_integer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Integer;
}

bool is_pointer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Pointer;
}

bool is_void_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Void;
}

const CoreIrIntegerType *as_integer_type(const CoreIrType *type) {
    if (!is_integer_type(type)) {
        return nullptr;
    }
    return static_cast<const CoreIrIntegerType *>(type);
}

bool is_supported_storage_type(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return true;
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    return bit_width == 32 || bit_width == 64;
}

bool is_supported_value_type(const CoreIrType *type) {
    if (is_supported_storage_type(type) || is_void_type(type)) {
        return true;
    }
    const auto *integer_type = as_integer_type(type);
    return integer_type != nullptr && integer_type->get_bit_width() == 1;
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

std::size_t get_storage_size(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return 8;
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return 0;
    }
    if (integer_type->get_bit_width() <= 32) {
        return 4;
    }
    return 8;
}

std::size_t get_storage_alignment(const CoreIrType *type) {
    return std::min<std::size_t>(8, std::max<std::size_t>(4, get_storage_size(type)));
}

bool uses_64bit_register(const CoreIrType *type) {
    return is_pointer_type(type) ||
           (as_integer_type(type) != nullptr &&
            as_integer_type(type)->get_bit_width() > 32);
}

std::string general_register_name(unsigned index, bool use_64bit) {
    return std::string(use_64bit ? "x" : "w") + std::to_string(index);
}

std::string zero_register_name(bool use_64bit) {
    return use_64bit ? "xzr" : "wzr";
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

std::string scalar_directive(const CoreIrType *type) {
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return ".xword";
    }
    return ".word";
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

class AArch64LoweringSession {
  private:
    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64AsmPrinter printer_;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;

    struct FunctionState {
        AArch64MachineFunction *machine_function = nullptr;
        std::unordered_map<const CoreIrParameter *, std::size_t> parameter_offsets;
    };

  public:
    AArch64LoweringSession(const CoreIrModule &module,
                           const BackendOptions &backend_options,
                           DiagnosticEngine &diagnostic_engine)
        : module_(module),
          backend_options_(backend_options),
          diagnostic_engine_(diagnostic_engine) {}

    std::unique_ptr<AsmResult> Generate() {
        if (!backend_options_.get_target_triple().empty() &&
            backend_options_.get_target_triple() != kDefaultTargetTriple) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported AArch64 native target triple: " +
                    backend_options_.get_target_triple());
            return nullptr;
        }

        AArch64MachineModule machine_module;
        machine_module.append_global_line(".arch armv8-a");

        if (!append_globals(machine_module) || !append_functions(machine_module)) {
            return nullptr;
        }

        return std::make_unique<AsmResult>(AsmTargetKind::AArch64,
                                           printer_.print_module(machine_module));
    }

  private:
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
            add_backend_error(
                diagnostic_engine_,
                "AArch64 native backend requires an initializer for global '" +
                    global.get_name() + "'");
            return false;
        }
        if (is_byte_string_global(global)) {
            machine_module.append_global_line(".section .rodata");
            machine_module.append_global_line(
                ".p2align 0");
            if (!global.get_is_internal_linkage()) {
                machine_module.append_global_line(".globl " + global.get_name());
            }
            machine_module.append_global_line(global.get_name() + ":");
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
            machine_module.append_global_line(bytes_line.str());
            machine_module.append_global_line("");
            return true;
        }

        if (!is_supported_storage_type(global.get_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global type in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }

        const auto *int_constant =
            dynamic_cast<const CoreIrConstantInt *>(global.get_initializer());
        const auto *null_constant =
            dynamic_cast<const CoreIrConstantNull *>(global.get_initializer());
        if (int_constant == nullptr && null_constant == nullptr) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global initializer in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }

        machine_module.append_global_line(global.get_is_constant() ? ".section .rodata"
                                                                   : ".data");
        machine_module.append_global_line(
            ".p2align " + std::to_string(get_storage_alignment(global.get_type()) == 8 ? 3 : 2));
        if (!global.get_is_internal_linkage()) {
            machine_module.append_global_line(".globl " + global.get_name());
        }
        machine_module.append_global_line(global.get_name() + ":");
        const std::uint64_t value =
            int_constant != nullptr ? int_constant->get_value() : 0;
        machine_module.append_global_line("  " + scalar_directive(global.get_type()) +
                                          " " + std::to_string(value));
        machine_module.append_global_line("");
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
        if (function.get_is_variadic()) {
            add_backend_error(diagnostic_engine_,
                              "variadic functions are not supported by the "
                              "AArch64 native backend: " +
                                  function.get_name());
            return false;
        }
        if (!is_supported_value_type(function.get_function_type()->get_return_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported return type in AArch64 native backend for function '" +
                    function.get_name() + "'");
            return false;
        }
        if (function.get_parameters().size() > 8) {
            add_backend_error(diagnostic_engine_,
                              "functions with more than 8 parameters are not "
                              "supported by the AArch64 native backend: " +
                                  function.get_name());
            return false;
        }
        for (const auto &parameter : function.get_parameters()) {
            if (!is_supported_storage_type(parameter->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported parameter type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            if (!is_supported_storage_type(stack_slot->get_allocated_type())) {
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
        FunctionState state;
        state.machine_function = &machine_function;

        std::size_t current_offset = 0;
        for (const auto &parameter : function.get_parameters()) {
            current_offset = align_to(current_offset,
                                      get_storage_alignment(parameter->get_type()));
            current_offset += get_storage_size(parameter->get_type());
            machine_function.get_frame_info().set_value_offset(parameter.get(),
                                                               current_offset);
            state.parameter_offsets.emplace(parameter.get(), current_offset);
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            current_offset = align_to(current_offset,
                                      get_storage_alignment(stack_slot->get_allocated_type()));
            current_offset += get_storage_size(stack_slot->get_allocated_type());
            machine_function.get_frame_info().set_stack_slot_offset(stack_slot.get(),
                                                                    current_offset);
        }
        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!instruction_produces_spill_value(*instruction)) {
                    continue;
                }
                if (!is_supported_value_type(instruction->get_type())) {
                    add_backend_error(
                        diagnostic_engine_,
                        "unsupported Core IR value type in AArch64 native backend "
                        "for function '" +
                            function_name + "'");
                    return false;
                }
                current_offset =
                    align_to(current_offset, get_storage_alignment_for_value(*instruction));
                current_offset += get_storage_size_for_value(*instruction);
                machine_function.get_frame_info().set_value_offset(instruction.get(),
                                                                   current_offset);
            }
        }

        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);

        block_labels_.clear();
        for (const auto &basic_block : function.get_basic_blocks()) {
            block_labels_.emplace(
                basic_block.get(),
                ".L" + sanitize_label_fragment(function_name) + "_" +
                    sanitize_label_fragment(basic_block->get_name()));
        }

        if (!emit_function(machine_function, function, state)) {
            return false;
        }
        return true;
    }

    static bool instruction_produces_spill_value(const CoreIrInstruction &instruction) {
        switch (instruction.get_opcode()) {
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
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Jump:
        case CoreIrOpcode::CondJump:
        case CoreIrOpcode::Return:
            return false;
        }
        return false;
    }

    static std::size_t get_storage_size_for_value(const CoreIrValue &value) {
        const auto *integer_type = as_integer_type(value.get_type());
        if (integer_type != nullptr && integer_type->get_bit_width() == 1) {
            return 4;
        }
        return get_storage_size(value.get_type());
    }

    static std::size_t
    get_storage_alignment_for_value(const CoreIrValue &value) {
        const auto *integer_type = as_integer_type(value.get_type());
        if (integer_type != nullptr && integer_type->get_bit_width() == 1) {
            return 4;
        }
        return get_storage_alignment(value.get_type());
    }

    bool emit_function(AArch64MachineFunction &machine_function,
                       const CoreIrFunction &function, FunctionState &state) {
        AArch64MachineBlock &prologue_block =
            machine_function.append_block(function.get_name());
        prologue_block.append_instruction("stp x29, x30, [sp, #-16]!");
        prologue_block.append_instruction("mov x29, sp");
        if (machine_function.get_frame_info().get_frame_size() > 0) {
            prologue_block.append_instruction(
                "sub sp, sp, #" +
                std::to_string(machine_function.get_frame_info().get_frame_size()));
        }

        for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
            const CoreIrParameter *parameter = function.get_parameters()[index].get();
            const std::size_t offset = state.parameter_offsets.at(parameter);
            append_store_to_frame(
                prologue_block, parameter->get_type(),
                general_register_name(static_cast<unsigned>(index),
                                      uses_64bit_register(parameter->get_type())),
                offset);
        }

        for (const auto &basic_block : function.get_basic_blocks()) {
            AArch64MachineBlock &machine_block =
                machine_function.append_block(block_labels_.at(basic_block.get()));
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!emit_instruction(machine_function, machine_block, *instruction,
                                      state)) {
                    return false;
                }
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
                          const CoreIrInstruction &instruction,
                          const FunctionState &state) {
        switch (instruction.get_opcode()) {
        case CoreIrOpcode::Load:
            return emit_load(machine_block,
                             static_cast<const CoreIrLoadInst &>(instruction),
                             state);
        case CoreIrOpcode::Store:
            return emit_store(machine_block,
                              static_cast<const CoreIrStoreInst &>(instruction),
                              state);
        case CoreIrOpcode::Binary:
            return emit_binary(machine_block,
                               static_cast<const CoreIrBinaryInst &>(instruction),
                               state);
        case CoreIrOpcode::Unary:
            return emit_unary(machine_block,
                              static_cast<const CoreIrUnaryInst &>(instruction),
                              state);
        case CoreIrOpcode::Compare:
            return emit_compare(machine_block,
                                static_cast<const CoreIrCompareInst &>(instruction),
                                state);
        case CoreIrOpcode::Cast:
            return emit_cast(machine_block,
                             static_cast<const CoreIrCastInst &>(instruction), state);
        case CoreIrOpcode::Call:
            return emit_call(machine_block,
                             static_cast<const CoreIrCallInst &>(instruction), state);
        case CoreIrOpcode::Jump:
            machine_block.append_instruction(
                "b " +
                block_labels_.at(
                    static_cast<const CoreIrJumpInst &>(instruction).get_target_block()));
            return true;
        case CoreIrOpcode::CondJump:
            return emit_cond_jump(machine_block,
                                  static_cast<const CoreIrCondJumpInst &>(instruction),
                                  state);
        case CoreIrOpcode::Return:
            return emit_return(machine_function, machine_block,
                               static_cast<const CoreIrReturnInst &>(instruction),
                               state);
        case CoreIrOpcode::AddressOfStackSlot:
        case CoreIrOpcode::AddressOfGlobal:
            return true;
        case CoreIrOpcode::AddressOfFunction:
            add_backend_error(diagnostic_engine_,
                              "address-of-function values are not supported by "
                              "the AArch64 native backend");
            return false;
        case CoreIrOpcode::GetElementPtr:
            add_backend_error(diagnostic_engine_,
                              "getelementptr values are not supported by the "
                              "AArch64 native backend");
            return false;
        }
        return false;
    }

    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value, unsigned register_index,
                           const FunctionState &state) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 "
                              "lowering");
            return false;
        }

        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(value);
            int_constant != nullptr) {
            return materialize_integer_constant(machine_block, value->get_type(),
                                                int_constant->get_value(),
                                                register_index);
        }
        if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
            machine_block.append_instruction(
                "mov " +
                general_register_name(register_index, uses_64bit_register(value->get_type())) +
                ", " + zero_register_name(uses_64bit_register(value->get_type())));
            return true;
        }
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
            global_address != nullptr) {
            return materialize_global_address(machine_block,
                                              global_address->get_global()->get_name(),
                                              register_index);
        }
        if (const auto *parameter = dynamic_cast<const CoreIrParameter *>(value);
            parameter != nullptr) {
            const std::size_t offset = state.parameter_offsets.at(parameter);
            append_load_from_frame(machine_block, parameter->get_type(), register_index,
                                   offset);
            return true;
        }
        if (const auto *address_of_stack_slot =
                dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
            address_of_stack_slot != nullptr) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    address_of_stack_slot->get_stack_slot());
            append_frame_address(machine_block, register_index, offset);
            return true;
        }
        if (const auto *address_of_global =
                dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
            address_of_global != nullptr) {
            return materialize_global_address(
                machine_block, address_of_global->get_global()->get_name(),
                register_index);
        }
        if (const auto *instruction = dynamic_cast<const CoreIrInstruction *>(value);
            instruction != nullptr &&
            state.machine_function->get_frame_info().has_value_offset(instruction)) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_value_offset(instruction);
            append_load_from_frame(machine_block, instruction->get_type(),
                                   register_index, offset);
            return true;
        }

        add_backend_error(diagnostic_engine_,
                          "unsupported Core IR value in AArch64 native backend");
        return false;
    }

    bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                      const CoreIrType *type, std::uint64_t value,
                                      unsigned register_index) {
        const bool use_64bit = uses_64bit_register(type);
        const std::string reg = general_register_name(register_index, use_64bit);
        const unsigned pieces = use_64bit ? 4U : 2U;
        bool emitted = false;
        for (unsigned piece = 0; piece < pieces; ++piece) {
            const std::uint16_t imm16 =
                static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
            if (!emitted) {
                machine_block.append_instruction("movz " + reg + ", #" +
                                                 std::to_string(imm16) + ", lsl #" +
                                                 std::to_string(piece * 16U));
                emitted = true;
                continue;
            }
            if (imm16 == 0) {
                continue;
            }
            machine_block.append_instruction("movk " + reg + ", #" +
                                             std::to_string(imm16) + ", lsl #" +
                                             std::to_string(piece * 16U));
        }
        if (!emitted) {
            machine_block.append_instruction("mov " + reg + ", " +
                                             zero_register_name(use_64bit));
        }
        return true;
    }

    bool materialize_global_address(AArch64MachineBlock &machine_block,
                                    const std::string &symbol_name,
                                    unsigned register_index) {
        const std::string reg = general_register_name(register_index, true);
        machine_block.append_instruction("adrp " + reg + ", " + symbol_name);
        machine_block.append_instruction("add " + reg + ", " + reg +
                                         ", :lo12:" + symbol_name);
        return true;
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              unsigned register_index, std::size_t offset) {
        const std::string reg = general_register_name(register_index, true);
        if (offset <= 4095) {
            machine_block.append_instruction("sub " + reg + ", x29, #" +
                                             std::to_string(offset));
            return;
        }
        materialize_integer_constant(machine_block,
                                     create_fake_pointer_type(), offset, register_index);
        machine_block.append_instruction("sub " + reg + ", x29, " + reg);
    }

    const CoreIrType *create_fake_pointer_type() const {
        static CoreIrVoidType void_type;
        static CoreIrPointerType pointer_type(&void_type);
        return &pointer_type;
    }

    void append_load_from_frame(AArch64MachineBlock &machine_block,
                                const CoreIrType *type, unsigned register_index,
                                std::size_t offset) {
        const bool use_64bit = uses_64bit_register(type);
        const std::string reg = general_register_name(register_index, use_64bit);
        const std::string mnemonic = use_64bit ? "ldur" : "ldur";
        if (offset <= 255) {
            machine_block.append_instruction(mnemonic + " " + reg +
                                             ", [x29, #-" +
                                             std::to_string(offset) + "]");
            return;
        }
        append_frame_address(machine_block, 15, offset);
        machine_block.append_instruction("ldr " + reg + ", [x15]");
    }

    void append_store_to_frame(AArch64MachineBlock &machine_block,
                               const CoreIrType *type, const std::string &reg,
                               std::size_t offset) {
        if (offset <= 255) {
            machine_block.append_instruction("stur " + reg + ", [x29, #-" +
                                             std::to_string(offset) + "]");
            return;
        }
        append_frame_address(machine_block, 15, offset);
        machine_block.append_instruction("str " + reg + ", [x15]");
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   const FunctionState &state) {
        const bool use_64bit = uses_64bit_register(load.get_type());
        const std::string reg = general_register_name(9, use_64bit);
        if (load.get_stack_slot() != nullptr) {
            append_load_from_frame(
                machine_block, load.get_type(), 9,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    load.get_stack_slot()));
        } else {
            if (!materialize_value(machine_block, load.get_address(), 10, state)) {
                return false;
            }
            machine_block.append_instruction("ldr " + reg + ", [x10]");
        }
        append_store_to_frame(machine_block, load.get_type(), reg,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &load));
        return true;
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, const FunctionState &state) {
        if (!materialize_value(machine_block, store.get_value(), 9, state)) {
            return false;
        }
        const std::string reg =
            general_register_name(9, uses_64bit_register(store.get_value()->get_type()));
        if (store.get_stack_slot() != nullptr) {
            append_store_to_frame(
                machine_block, store.get_value()->get_type(), reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    store.get_stack_slot()));
            return true;
        }
        if (!materialize_value(machine_block, store.get_address(), 10, state)) {
            return false;
        }
        machine_block.append_instruction("str " + reg + ", [x10]");
        return true;
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary, const FunctionState &state) {
        if (!materialize_value(machine_block, binary.get_lhs(), 9, state) ||
            !materialize_value(machine_block, binary.get_rhs(), 10, state)) {
            return false;
        }
        const bool use_64bit = uses_64bit_register(binary.get_type());
        const std::string dst = general_register_name(9, use_64bit);
        const std::string rhs = general_register_name(10, use_64bit);
        std::string opcode;
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
        case CoreIrBinaryOpcode::URem:
            add_backend_error(
                diagnostic_engine_,
                "remainder operations are not supported by the AArch64 native "
                "backend");
            return false;
        }
        machine_block.append_instruction(opcode + " " + dst + ", " + dst + ", " +
                                         rhs);
        append_store_to_frame(machine_block, binary.get_type(), dst,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &binary));
        return true;
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        if (!materialize_value(machine_block, unary.get_operand(), 9, state)) {
            return false;
        }
        const bool use_64bit = uses_64bit_register(unary.get_type());
        const std::string dst = general_register_name(9, use_64bit);
        switch (unary.get_unary_opcode()) {
        case CoreIrUnaryOpcode::Negate:
            machine_block.append_instruction("neg " + dst + ", " + dst);
            break;
        case CoreIrUnaryOpcode::BitwiseNot:
            machine_block.append_instruction("mvn " + dst + ", " + dst);
            break;
        case CoreIrUnaryOpcode::LogicalNot:
            machine_block.append_instruction("cmp " + dst + ", #0");
            machine_block.append_instruction("cset w9, eq");
            break;
        }
        append_store_to_frame(machine_block, unary.get_type(), dst,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &unary));
        return true;
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        if (!materialize_value(machine_block, compare.get_lhs(), 9, state) ||
            !materialize_value(machine_block, compare.get_rhs(), 10, state)) {
            return false;
        }
        const bool use_64bit = uses_64bit_register(compare.get_lhs()->get_type());
        machine_block.append_instruction(
            "cmp " + general_register_name(9, use_64bit) + ", " +
            general_register_name(10, use_64bit));
        machine_block.append_instruction("cset w9, " +
                                         condition_code(compare.get_predicate()));
        append_store_to_frame(
            machine_block, compare.get_type(), "w9",
            state.machine_function->get_frame_info().get_value_offset(&compare));
        return true;
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        if (!materialize_value(machine_block, cast.get_operand(), 9, state)) {
            return false;
        }
        switch (cast.get_cast_kind()) {
        case CoreIrCastKind::Truncate: {
            const auto *target_integer = as_integer_type(cast.get_type());
            if (target_integer != nullptr && target_integer->get_bit_width() == 1) {
                machine_block.append_instruction("and w9, w9, #1");
            }
            break;
        }
        case CoreIrCastKind::ZeroExtend:
            break;
        case CoreIrCastKind::SignExtend:
            if (uses_64bit_register(cast.get_type()) &&
                !uses_64bit_register(cast.get_operand()->get_type())) {
                machine_block.append_instruction("sxtw x9, w9");
            }
            break;
        case CoreIrCastKind::PtrToInt:
        case CoreIrCastKind::IntToPtr:
            break;
        case CoreIrCastKind::SignedIntToFloat:
        case CoreIrCastKind::UnsignedIntToFloat:
        case CoreIrCastKind::FloatToSignedInt:
        case CoreIrCastKind::FloatToUnsignedInt:
        case CoreIrCastKind::FloatExtend:
        case CoreIrCastKind::FloatTruncate:
            add_backend_error(diagnostic_engine_,
                              "floating-point casts are not supported by the "
                              "AArch64 native backend");
            return false;
        }
        append_store_to_frame(
            machine_block, cast.get_type(),
            general_register_name(9, uses_64bit_register(cast.get_type())),
            state.machine_function->get_frame_info().get_value_offset(&cast));
        return true;
    }

    bool emit_call(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                   const FunctionState &state) {
        if (!call.get_is_direct_call()) {
            add_backend_error(diagnostic_engine_,
                              "indirect calls are not supported by the AArch64 "
                              "native backend");
            return false;
        }
        if (call.get_callee_type() != nullptr && call.get_callee_type()->get_is_variadic()) {
            add_backend_error(diagnostic_engine_,
                              "variadic calls are not supported by the AArch64 "
                              "native backend");
            return false;
        }

        const auto &arguments = call.get_operands();
        const std::size_t argument_count =
            arguments.size() - call.get_argument_begin_index();
        if (argument_count > 8) {
            add_backend_error(diagnostic_engine_,
                              "calls with more than 8 arguments are not "
                              "supported by the AArch64 native backend");
            return false;
        }
        for (std::size_t index = call.get_argument_begin_index();
             index < arguments.size(); ++index) {
            CoreIrValue *argument = arguments[index];
            if (!is_supported_storage_type(argument->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported call argument type in the "
                                  "AArch64 native backend");
                return false;
            }
            const unsigned argument_index =
                static_cast<unsigned>(index - call.get_argument_begin_index());
            if (!materialize_value(machine_block, argument, argument_index, state)) {
                return false;
            }
        }
        machine_block.append_instruction("bl " + call.get_callee_name());
        if (!is_void_type(call.get_type())) {
            append_store_to_frame(
                machine_block, call.get_type(),
                general_register_name(0, uses_64bit_register(call.get_type())),
                state.machine_function->get_frame_info().get_value_offset(&call));
        }
        return true;
    }

    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state) {
        if (!materialize_value(machine_block, cond_jump.get_condition(), 9, state)) {
            return false;
        }
        machine_block.append_instruction(
            "cbnz " +
            general_register_name(9,
                                  uses_64bit_register(cond_jump.get_condition()->get_type())) +
            ", " + block_labels_.at(cond_jump.get_true_block()));
        machine_block.append_instruction(
            "b " + block_labels_.at(cond_jump.get_false_block()));
        return true;
    }

    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state) {
        if (return_inst.get_return_value() != nullptr) {
            if (!materialize_value(machine_block, return_inst.get_return_value(), 9,
                                   state)) {
                return false;
            }
            const bool use_64bit =
                uses_64bit_register(return_inst.get_return_value()->get_type());
            machine_block.append_instruction("mov " + general_register_name(0, use_64bit) +
                                             ", " + general_register_name(9, use_64bit));
        }
        machine_block.append_instruction("b " + machine_function.get_epilogue_label());
        return true;
    }
};

} // namespace

std::size_t
AArch64FunctionFrameInfo::get_stack_slot_offset(
    const CoreIrStackSlot *stack_slot) const {
    return stack_slot_offsets_.at(stack_slot);
}

std::size_t
AArch64FunctionFrameInfo::get_value_offset(const CoreIrValue *value) const {
    return value_offsets_.at(value);
}

bool AArch64FunctionFrameInfo::has_value_offset(const CoreIrValue *value) const {
    return value_offsets_.find(value) != value_offsets_.end();
}

std::string
AArch64AsmPrinter::print_module(const AArch64MachineModule &module) const {
    std::ostringstream output;
    for (const std::string &line : module.get_global_lines()) {
        output << line << "\n";
    }
    for (const AArch64MachineFunction &function : module.get_functions()) {
        if (!module.get_global_lines().empty() || &function != &module.get_functions().front()) {
            output << "\n";
        }
        output << ".text\n";
        if (function.get_is_global_symbol()) {
            output << ".globl " << function.get_name() << "\n";
        }
        output << ".p2align 2\n";
        output << ".type " << function.get_name() << ", %function\n";
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            output << block.get_label() << ":\n";
            for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                output << "  " << instruction.get_text() << "\n";
            }
        }
        output << ".size " << function.get_name() << ", .-" << function.get_name()
               << "\n";
    }
    return output.str();
}

std::unique_ptr<AsmResult>
AArch64AsmBackend::Generate(const CoreIrModule &module,
                            const BackendOptions &backend_options,
                            DiagnosticEngine &diagnostic_engine) const {
    AArch64LoweringSession session(module, backend_options, diagnostic_engine);
    return session.Generate();
}

} // namespace sysycc
