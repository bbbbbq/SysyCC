#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

bool parameter_can_emit_readonly_attr(const CoreIrType *type) {
    const auto *pointer_type = dynamic_cast<const CoreIrPointerType *>(type);
    if (pointer_type == nullptr || pointer_type->get_pointee_type() == nullptr) {
        return false;
    }
    return pointer_type->get_pointee_type()->get_kind() != CoreIrTypeKind::Function;
}

std::string get_default_target_triple() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple-macosx15.0.0";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-macosx10.15.0";
#elif defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

std::string get_default_target_datalayout() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "e-m:o-i64:64-i128:128-n32:64-S128-Fn32";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "e-m:o-i64:64-f80:128-n8:16:32:64-S128";
#elif defined(__aarch64__)
    return "e-m:e-i64:64-i128:128-n32:64-S128";
#elif defined(__x86_64__)
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:"
           "16:32:64-S128";
#else
    return "";
#endif
}

std::string encode_llvm_string_bytes(const std::vector<std::uint8_t> &bytes) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex << std::setfill('0');
    for (const std::uint8_t byte : bytes) {
        if (std::isprint(byte) != 0 && byte != '\\' && byte != '"') {
            encoded << static_cast<char>(byte);
            continue;
        }
        encoded << '\\' << std::setw(2) << static_cast<int>(byte);
    }
    return encoded.str();
}

std::size_t get_integer_bit_width(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    return integer_type == nullptr ? 0 : integer_type->get_bit_width();
}

std::uint64_t get_integer_mask(std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    if (bit_width >= 64) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << bit_width) - 1;
}

std::uint64_t truncate_to_bit_width(std::uint64_t value,
                                    std::size_t bit_width) {
    return value & get_integer_mask(bit_width);
}

std::int64_t sign_extend_to_i64(std::uint64_t value, std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    value = truncate_to_bit_width(value, bit_width);
    if (bit_width >= 64) {
        return static_cast<std::int64_t>(value);
    }
    const std::uint64_t sign_bit = std::uint64_t{1} << (bit_width - 1);
    if ((value & sign_bit) != 0) {
        value |= ~get_integer_mask(bit_width);
    }
    return static_cast<std::int64_t>(value);
}

std::string format_signed_integer_literal(std::uint64_t value,
                                          std::size_t bit_width) {
    if (bit_width == 1) {
        return (value & 1U) == 0 ? "0" : "1";
    }
    return std::to_string(sign_extend_to_i64(value, bit_width));
}

std::string format_intrinsic_type_suffix(const CoreIrType *type) {
    if (type == nullptr) {
        return "";
    }
    if (const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
        integer_type != nullptr) {
        return "i" + std::to_string(integer_type->get_bit_width());
    }
    if (const auto *float_type = dynamic_cast<const CoreIrFloatType *>(type);
        float_type != nullptr) {
        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return "f16";
        case CoreIrFloatKind::Float32:
            return "f32";
        case CoreIrFloatKind::Float64:
            return "f64";
        case CoreIrFloatKind::Float128:
            return "f128";
        }
    }
    return "";
}

std::optional<std::string>
try_format_folded_integer_cast_literal(const CoreIrCastInst &cast_instruction) {
    const auto *operand_constant =
        dynamic_cast<const CoreIrConstantInt *>(cast_instruction.get_operand());
    if (operand_constant == nullptr) {
        return std::nullopt;
    }
    const std::size_t source_width =
        get_integer_bit_width(cast_instruction.get_operand()->get_type());
    const std::size_t target_width =
        get_integer_bit_width(cast_instruction.get_type());
    if (source_width == 0 || target_width == 0) {
        return std::nullopt;
    }

    const std::uint64_t source_bits =
        truncate_to_bit_width(operand_constant->get_value(), source_width);
    switch (cast_instruction.get_cast_kind()) {
    case CoreIrCastKind::SignExtend:
        return format_signed_integer_literal(
            truncate_to_bit_width(static_cast<std::uint64_t>(sign_extend_to_i64(
                                      source_bits, source_width)),
                                  target_width),
            target_width);
    case CoreIrCastKind::ZeroExtend:
        return std::to_string(source_bits);
    case CoreIrCastKind::Truncate:
        return format_signed_integer_literal(
            truncate_to_bit_width(source_bits, target_width), target_width);
    case CoreIrCastKind::SignedIntToFloat:
    case CoreIrCastKind::UnsignedIntToFloat:
    case CoreIrCastKind::FloatToSignedInt:
    case CoreIrCastKind::FloatToUnsignedInt:
    case CoreIrCastKind::FloatExtend:
    case CoreIrCastKind::FloatTruncate:
    case CoreIrCastKind::PtrToInt:
    case CoreIrCastKind::IntToPtr:
        return std::nullopt;
    }
    return std::nullopt;
}

std::string format_binary_opcode(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
        return "add";
    case CoreIrBinaryOpcode::Sub:
        return "sub";
    case CoreIrBinaryOpcode::Mul:
        return "mul";
    case CoreIrBinaryOpcode::SDiv:
        return "sdiv";
    case CoreIrBinaryOpcode::UDiv:
        return "udiv";
    case CoreIrBinaryOpcode::SRem:
        return "srem";
    case CoreIrBinaryOpcode::URem:
        return "urem";
    case CoreIrBinaryOpcode::And:
        return "and";
    case CoreIrBinaryOpcode::Or:
        return "or";
    case CoreIrBinaryOpcode::Xor:
        return "xor";
    case CoreIrBinaryOpcode::Shl:
        return "shl";
    case CoreIrBinaryOpcode::LShr:
        return "lshr";
    case CoreIrBinaryOpcode::AShr:
        return "ashr";
    }
    return "";
}

std::string format_float_binary_opcode(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
        return "fadd";
    case CoreIrBinaryOpcode::Sub:
        return "fsub";
    case CoreIrBinaryOpcode::Mul:
        return "fmul";
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
        return "fdiv";
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
        return "frem";
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
    case CoreIrBinaryOpcode::Shl:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        return "";
    }
    return "";
}

bool uses_float_binary_opcode(const CoreIrType *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == CoreIrTypeKind::Float) {
        return true;
    }
    const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
    return vector_type != nullptr &&
           vector_type->get_element_type() != nullptr &&
           vector_type->get_element_type()->get_kind() == CoreIrTypeKind::Float;
}

std::string format_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
        return "slt";
    case CoreIrComparePredicate::SignedLessEqual:
        return "sle";
    case CoreIrComparePredicate::SignedGreater:
        return "sgt";
    case CoreIrComparePredicate::SignedGreaterEqual:
        return "sge";
    case CoreIrComparePredicate::UnsignedLess:
        return "ult";
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ule";
    case CoreIrComparePredicate::UnsignedGreater:
        return "ugt";
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "uge";
    }
    return "";
}

std::string format_float_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "oeq";
    case CoreIrComparePredicate::NotEqual:
        return "une";
    case CoreIrComparePredicate::SignedLess:
    case CoreIrComparePredicate::UnsignedLess:
        return "olt";
    case CoreIrComparePredicate::SignedLessEqual:
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ole";
    case CoreIrComparePredicate::SignedGreater:
    case CoreIrComparePredicate::UnsignedGreater:
        return "ogt";
    case CoreIrComparePredicate::SignedGreaterEqual:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "oge";
    }
    return "";
}

bool is_integer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Integer;
}

bool is_float_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Float;
}

std::string strip_floating_literal_suffix(std::string value_text) {
    while (!value_text.empty()) {
        const char last = value_text.back();
        if (last == 'f' || last == 'F' || last == 'l' || last == 'L') {
            value_text.pop_back();
            continue;
        }
        break;
    }
    return value_text;
}

std::string normalize_decimal_floating_literal(std::string value_text) {
    if (value_text.empty()) {
        return "0.0";
    }
    const bool has_decimal_point = value_text.find('.') != std::string::npos;
    const bool has_exponent = value_text.find('e') != std::string::npos ||
                              value_text.find('E') != std::string::npos;
    if (!value_text.empty() && value_text.front() == '.') {
        value_text.insert(value_text.begin(), '0');
    }
    if (!has_decimal_point && !has_exponent) {
        value_text += ".0";
    }
    return value_text;
}

bool is_hex_float_literal(const std::string &value_text) {
    return value_text.find("0x") != std::string::npos ||
           value_text.find("0X") != std::string::npos ||
           value_text.find('p') != std::string::npos ||
           value_text.find('P') != std::string::npos;
}

std::string format_floating_value(double value, int precision) {
    std::ostringstream stream;
    stream << std::scientific << std::setprecision(precision) << value;
    return stream.str();
}

std::string normalize_float_literal_text(const std::string &value_text,
                                         CoreIrFloatKind float_kind) {
    const std::string stripped = strip_floating_literal_suffix(value_text);
    if (stripped.empty()) {
        return "0.0";
    }

    try {
        const long double parsed = std::stold(stripped);
        switch (float_kind) {
        case CoreIrFloatKind::Float16:
        case CoreIrFloatKind::Float32:
            return format_floating_value(
                static_cast<double>(static_cast<float>(parsed)), 17);
        case CoreIrFloatKind::Float64:
        case CoreIrFloatKind::Float128:
            return format_floating_value(static_cast<double>(parsed), 17);
        }
    } catch (...) {
        return !is_hex_float_literal(stripped)
                   ? normalize_decimal_floating_literal(stripped)
                   : "0.0";
    }
    return !is_hex_float_literal(stripped)
               ? normalize_decimal_floating_literal(stripped)
               : "0.0";
}

} // namespace

std::string
CoreIrLlvmTargetBackend::next_helper_name(const std::string &prefix) {
    if (prefix == "t") {
        return next_value_name();
    }
    return prefix + std::to_string(helper_id_++);
}

std::string CoreIrLlvmTargetBackend::next_value_name() {
    return "t" + std::to_string(next_value_id_++);
}

std::string
CoreIrLlvmTargetBackend::get_emitted_value_name(const CoreIrValue *value) {
    if (value == nullptr) {
        return "<null>";
    }
    const auto it = emitted_value_names_.find(value);
    if (it != emitted_value_names_.end()) {
        return it->second;
    }
    std::string name = next_value_name();
    emitted_value_names_.emplace(value, name);
    return name;
}

std::string
CoreIrLlvmTargetBackend::get_emitted_block_name(const CoreIrBasicBlock *block) {
    if (block == nullptr) {
        return "<null-block>";
    }
    const auto it = emitted_block_names_.find(block);
    if (it != emitted_block_names_.end()) {
        return it->second;
    }
    std::string name = block->get_name();
    if (name.empty() || name.size() > 255 ||
        used_block_names_.find(name) != used_block_names_.end()) {
        name = next_helper_name("bb");
    }
    emitted_block_names_.emplace(block, name);
    used_block_names_.insert(name);
    return name;
}

std::string CoreIrLlvmTargetBackend::get_emitted_stack_slot_name(
    const CoreIrStackSlot *stack_slot) {
    if (stack_slot == nullptr) {
        return "<null-stack-slot>";
    }
    const auto it = emitted_stack_slot_names_.find(stack_slot);
    if (it != emitted_stack_slot_names_.end()) {
        return it->second;
    }
    std::string name = stack_slot->get_name();
    if (name.empty() || name.size() > 255 ||
        used_stack_slot_names_.find(name) != used_stack_slot_names_.end()) {
        name = next_value_name();
    }
    emitted_stack_slot_names_.emplace(stack_slot, name);
    used_stack_slot_names_.insert(name);
    return name;
}

std::string CoreIrLlvmTargetBackend::format_type(const CoreIrType *type) const {
    if (type == nullptr) {
        return "<null-type>";
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Void:
        return "void";
    case CoreIrTypeKind::Integer:
        return "i" + std::to_string(static_cast<const CoreIrIntegerType *>(type)
                                        ->get_bit_width());
    case CoreIrTypeKind::Float: {
        switch (static_cast<const CoreIrFloatType *>(type)->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return "half";
        case CoreIrFloatKind::Float32:
            return "float";
        case CoreIrFloatKind::Float64:
            return "double";
        case CoreIrFloatKind::Float128:
            return "fp128";
        }
        return "float";
    }
    case CoreIrTypeKind::Vector: {
        const auto *vector_type = static_cast<const CoreIrVectorType *>(type);
        return "<" + std::to_string(vector_type->get_element_count()) + " x " +
               format_type(vector_type->get_element_type()) + ">";
    }
    case CoreIrTypeKind::Pointer:
        return "ptr";
    case CoreIrTypeKind::Array: {
        const auto *array_type = static_cast<const CoreIrArrayType *>(type);
        return "[" + std::to_string(array_type->get_element_count()) + " x " +
               format_type(array_type->get_element_type()) + "]";
    }
    case CoreIrTypeKind::Struct: {
        const auto *struct_type = static_cast<const CoreIrStructType *>(type);
        std::string text = "{ ";
        const auto &element_types = struct_type->get_element_types();
        for (std::size_t index = 0; index < element_types.size(); ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += format_type(element_types[index]);
        }
        text += " }";
        return text;
    }
    case CoreIrTypeKind::Function: {
        const auto *function_type =
            static_cast<const CoreIrFunctionType *>(type);
        std::string text = format_type(function_type->get_return_type()) + " (";
        const auto &parameter_types = function_type->get_parameter_types();
        for (std::size_t index = 0; index < parameter_types.size(); ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += format_type(parameter_types[index]);
        }
        if (function_type->get_is_variadic()) {
            if (!parameter_types.empty()) {
                text += ", ";
            }
            text += "...";
        }
        text += ")";
        return text;
    }
    }
    return "<type>";
}

std::string
CoreIrLlvmTargetBackend::format_constant(const CoreIrConstant *constant) const {
    if (constant == nullptr) {
        return "zeroinitializer";
    }
    if (const auto *int_constant =
            dynamic_cast<const CoreIrConstantInt *>(constant);
        int_constant != nullptr) {
        return std::to_string(int_constant->get_value());
    }
    if (const auto *float_constant =
            dynamic_cast<const CoreIrConstantFloat *>(constant);
        float_constant != nullptr) {
        const auto *float_type =
            dynamic_cast<const CoreIrFloatType *>(constant->get_type());
        if (float_type == nullptr) {
            return "0.0";
        }
        return normalize_float_literal_text(float_constant->get_literal_text(),
                                            float_type->get_float_kind());
    }
    if (dynamic_cast<const CoreIrConstantNull *>(constant) != nullptr) {
        return "null";
    }
    if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) !=
        nullptr) {
        return "zeroinitializer";
    }
    if (const auto *byte_string =
            dynamic_cast<const CoreIrConstantByteString *>(constant);
        byte_string != nullptr) {
        return "c\"" + encode_llvm_string_bytes(byte_string->get_bytes()) +
               "\"";
    }
    if (const auto *aggregate =
            dynamic_cast<const CoreIrConstantAggregate *>(constant);
        aggregate != nullptr) {
        const bool is_array =
            constant->get_type() != nullptr &&
            constant->get_type()->get_kind() == CoreIrTypeKind::Array;
        const bool is_vector =
            constant->get_type() != nullptr &&
            constant->get_type()->get_kind() == CoreIrTypeKind::Vector;
        std::string text = is_array ? "[ " : is_vector ? "< " : "{ ";
        const auto &elements = aggregate->get_elements();
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += format_type(elements[index]->get_type());
            text += " ";
            text += format_constant(elements[index]);
        }
        text += is_array ? " ]" : is_vector ? " >" : " }";
        return text;
    }
    if (const auto *global_address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
        global_address != nullptr) {
        if (global_address->get_global() != nullptr) {
            return "@" + global_address->get_global()->get_name();
        }
        if (global_address->get_function() != nullptr) {
            return "@" + global_address->get_function()->get_name();
        }
        return "<constant-global-address>";
    }
    if (const auto *gep_constant =
            dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
        gep_constant != nullptr) {
        const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
            gep_constant->get_base()->get_type());
        if (base_pointer_type == nullptr) {
            return "<constant-gep>";
        }
        std::string text = "getelementptr inbounds (";
        text += format_type(base_pointer_type->get_pointee_type());
        text += ", ptr ";
        text += format_constant(gep_constant->get_base());
        for (const CoreIrConstant *index : gep_constant->get_indices()) {
            text += ", ";
            text += format_type(index->get_type());
            text += " ";
            text += format_constant(index);
        }
        text += ")";
        return text;
    }
    return "<constant>";
}

std::string
CoreIrLlvmTargetBackend::format_value_ref(const CoreIrValue *value) {
    if (value == nullptr) {
        return "<null>";
    }
    if (const auto *constant = dynamic_cast<const CoreIrConstant *>(value);
        constant != nullptr) {
        return format_constant(constant);
    }
    if (const auto *address_of_global =
            dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
        address_of_global != nullptr) {
        return "@" + address_of_global->get_global()->get_name();
    }
    if (const auto *address_of_function =
            dynamic_cast<const CoreIrAddressOfFunctionInst *>(value);
        address_of_function != nullptr) {
        return "@" + address_of_function->get_function()->get_name();
    }
    if (const auto *address_of_stack_slot =
            dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
        address_of_stack_slot != nullptr) {
        return "%" + get_emitted_stack_slot_name(
                         address_of_stack_slot->get_stack_slot());
    }
    if (dynamic_cast<const CoreIrInstruction *>(value) != nullptr) {
        if (const auto *compare_instruction =
                dynamic_cast<const CoreIrCompareInst *>(value);
            compare_instruction != nullptr &&
            compare_instruction->get_type() != nullptr &&
            ((compare_instruction->get_type()->get_kind() ==
                  CoreIrTypeKind::Integer &&
              static_cast<const CoreIrIntegerType *>(
                  compare_instruction->get_type())
                      ->get_bit_width() == 1) ||
             (compare_instruction->get_type()->get_kind() ==
                  CoreIrTypeKind::Vector &&
              static_cast<const CoreIrVectorType *>(
                  compare_instruction->get_type())
                      ->get_element_type()
                      ->get_kind() == CoreIrTypeKind::Integer &&
              static_cast<const CoreIrIntegerType *>(
                  static_cast<const CoreIrVectorType *>(
                      compare_instruction->get_type())
                      ->get_element_type())
                      ->get_bit_width() == 1))) {
            return "%" + get_emitted_value_name(value) + ".raw";
        }
        return "%" + get_emitted_value_name(value);
    }
    return "%" + value->get_name();
}

std::string
CoreIrLlvmTargetBackend::format_pointer_ref(const CoreIrValue *value) {
    return format_value_ref(value);
}

bool CoreIrLlvmTargetBackend::append_instruction(
    std::string &text, const CoreIrInstruction &instruction,
    DiagnosticEngine &diagnostic_engine) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi: {
        const auto &phi_instruction =
            static_cast<const CoreIrPhiInst &>(instruction);
        text += "  %" + get_emitted_value_name(&phi_instruction) + " = phi ";
        text += format_type(phi_instruction.get_type());
        bool first_incoming = true;
        for (std::size_t index = 0;
             index < phi_instruction.get_incoming_count(); ++index) {
            CoreIrBasicBlock *incoming_block =
                phi_instruction.get_incoming_block(index);
            CoreIrValue *incoming_value =
                phi_instruction.get_incoming_value(index);
            if (incoming_block == nullptr || incoming_value == nullptr) {
                continue;
            }
            if (!first_incoming) {
                text += ", ";
            } else {
                text += " ";
                first_incoming = false;
            }
            text += "[ ";
            text += format_value_ref(incoming_value);
            text += ", %";
            text += get_emitted_block_name(incoming_block);
            text += " ]";
        }
        text += "\n";
        return true;
    }
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
        return true;
    case CoreIrOpcode::GetElementPtr: {
        const auto &gep_instruction =
            static_cast<const CoreIrGetElementPtrInst &>(instruction);
        const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
            gep_instruction.get_base()->get_type());
        if (base_pointer_type == nullptr) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "core ir llvm lowering expected gep base to be a pointer",
                instruction.get_source_span());
            return false;
        }
        text += "  %" + get_emitted_value_name(&gep_instruction) +
                " = getelementptr inbounds ";
        text += format_type(base_pointer_type->get_pointee_type());
        text += ", ptr ";
        text += format_pointer_ref(gep_instruction.get_base());
        for (std::size_t index = 0; index < gep_instruction.get_index_count();
             ++index) {
            CoreIrValue *index_value = gep_instruction.get_index(index);
            text += ", ";
            text += format_type(index_value->get_type());
            text += " ";
            text += format_value_ref(index_value);
        }
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Load: {
        const auto &load_instruction =
            static_cast<const CoreIrLoadInst &>(instruction);
        text += "  %" + get_emitted_value_name(&load_instruction) + " = load ";
        text += format_type(load_instruction.get_type());
        text += ", ptr ";
        if (load_instruction.get_stack_slot() != nullptr) {
            text += "%" + get_emitted_stack_slot_name(
                              load_instruction.get_stack_slot());
        } else {
            text += format_pointer_ref(load_instruction.get_address());
        }
        if (load_instruction.get_alignment() > 0) {
            text += ", align ";
            text += std::to_string(load_instruction.get_alignment());
        }
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Store: {
        const auto &store_instruction =
            static_cast<const CoreIrStoreInst &>(instruction);
        text += "  store ";
        text += format_type(store_instruction.get_value()->get_type());
        text += " ";
        text += format_value_ref(store_instruction.get_value());
        text += ", ptr ";
        if (store_instruction.get_stack_slot() != nullptr) {
            text += "%" + get_emitted_stack_slot_name(
                              store_instruction.get_stack_slot());
        } else {
            text += format_pointer_ref(store_instruction.get_address());
        }
        if (store_instruction.get_alignment() > 0) {
            text += ", align ";
            text += std::to_string(store_instruction.get_alignment());
        }
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Binary: {
        const auto &binary_instruction =
            static_cast<const CoreIrBinaryInst &>(instruction);
        text += "  %" + get_emitted_value_name(&binary_instruction) + " = ";
        text += uses_float_binary_opcode(binary_instruction.get_type())
                    ? format_float_binary_opcode(binary_instruction.get_binary_opcode())
                    : format_binary_opcode(binary_instruction.get_binary_opcode());
        text += " ";
        text += format_type(binary_instruction.get_type());
        text += " ";
        text += format_value_ref(binary_instruction.get_lhs());
        text += ", ";
        text += format_value_ref(binary_instruction.get_rhs());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Unary: {
        const auto &unary_instruction =
            static_cast<const CoreIrUnaryInst &>(instruction);
        switch (unary_instruction.get_unary_opcode()) {
        case CoreIrUnaryOpcode::Negate:
            text += "  %" + get_emitted_value_name(&unary_instruction) + " = ";
            text += unary_instruction.get_type() != nullptr &&
                            unary_instruction.get_type()->get_kind() ==
                                CoreIrTypeKind::Float
                        ? "fsub "
                        : "sub ";
            text += format_type(unary_instruction.get_type());
            text += unary_instruction.get_type() != nullptr &&
                            unary_instruction.get_type()->get_kind() ==
                                CoreIrTypeKind::Float
                        ? " 0.0, "
                        : " 0, ";
            text += format_value_ref(unary_instruction.get_operand());
            text += "\n";
            return true;
        case CoreIrUnaryOpcode::BitwiseNot:
            text +=
                "  %" + get_emitted_value_name(&unary_instruction) + " = xor ";
            text += format_type(unary_instruction.get_type());
            text += " ";
            text += format_value_ref(unary_instruction.get_operand());
            text += ", -1\n";
            return true;
        case CoreIrUnaryOpcode::LogicalNot: {
            const std::string raw_name =
                "%" + get_emitted_value_name(&unary_instruction) + ".raw";
            if (is_float_type(unary_instruction.get_operand()->get_type())) {
                text += "  " + raw_name + " = fcmp oeq ";
                text +=
                    format_type(unary_instruction.get_operand()->get_type());
                text += " ";
                text += format_value_ref(unary_instruction.get_operand());
                text += ", 0.0\n";
            } else if (unary_instruction.get_operand()->get_type() != nullptr &&
                       unary_instruction.get_operand()
                               ->get_type()
                               ->get_kind() == CoreIrTypeKind::Pointer) {
                text += "  " + raw_name + " = icmp eq ptr ";
                text += format_value_ref(unary_instruction.get_operand());
                text += ", null\n";
            } else {
                text += "  " + raw_name + " = icmp eq ";
                text +=
                    format_type(unary_instruction.get_operand()->get_type());
                text += " ";
                text += format_value_ref(unary_instruction.get_operand());
                text += ", 0\n";
            }
            text += "  %" + get_emitted_value_name(&unary_instruction) +
                    " = zext i1 ";
            text += raw_name;
            text += " to ";
            text += format_type(unary_instruction.get_type());
            text += "\n";
            return true;
        }
        }
        return false;
    }
    case CoreIrOpcode::Compare: {
        const auto &compare_instruction =
            static_cast<const CoreIrCompareInst &>(instruction);
        const std::string raw_name =
            "%" + get_emitted_value_name(&compare_instruction) + ".raw";
        const CoreIrType *lhs_type = compare_instruction.get_lhs() == nullptr
                                         ? nullptr
                                         : compare_instruction.get_lhs()->get_type();
        const bool is_float_compare =
            lhs_type != nullptr &&
            (lhs_type->get_kind() == CoreIrTypeKind::Float ||
             (lhs_type->get_kind() == CoreIrTypeKind::Vector &&
              static_cast<const CoreIrVectorType *>(lhs_type)
                      ->get_element_type()
                      ->get_kind() == CoreIrTypeKind::Float));
        text += "  " + raw_name + " = ";
        text += is_float_compare ? "fcmp " : "icmp ";
        text +=
            is_float_compare
                ? format_float_compare_predicate(
                      compare_instruction.get_predicate())
                : format_compare_predicate(compare_instruction.get_predicate());
        text += " ";
        text += format_type(compare_instruction.get_lhs()->get_type());
        text += " ";
        text += format_value_ref(compare_instruction.get_lhs());
        text += ", ";
        text += format_value_ref(compare_instruction.get_rhs());
        text += "\n";
        if ((compare_instruction.get_type()->get_kind() ==
                 CoreIrTypeKind::Integer &&
             static_cast<const CoreIrIntegerType *>(
                 compare_instruction.get_type())
                     ->get_bit_width() == 1) ||
            (compare_instruction.get_type()->get_kind() ==
                 CoreIrTypeKind::Vector &&
             static_cast<const CoreIrVectorType *>(
                 compare_instruction.get_type())
                     ->get_element_type()
                     ->get_kind() == CoreIrTypeKind::Integer &&
             static_cast<const CoreIrIntegerType *>(
                 static_cast<const CoreIrVectorType *>(
                     compare_instruction.get_type())
                     ->get_element_type())
                     ->get_bit_width() == 1)) {
            return true;
        }
        text += "  %" + get_emitted_value_name(&compare_instruction) +
                " = zext i1 ";
        text += raw_name;
        text += " to ";
        text += format_type(compare_instruction.get_type());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Select: {
        const auto &select_instruction =
            static_cast<const CoreIrSelectInst &>(instruction);
        text +=
            "  %" + get_emitted_value_name(&select_instruction) + " = select ";
        text += format_type(select_instruction.get_condition()->get_type());
        text += " ";
        text += format_value_ref(select_instruction.get_condition());
        text += ", ";
        text += format_type(select_instruction.get_type());
        text += " ";
        text += format_value_ref(select_instruction.get_true_value());
        text += ", ";
        text += format_type(select_instruction.get_type());
        text += " ";
        text += format_value_ref(select_instruction.get_false_value());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::ExtractElement: {
        const auto &extract_instruction =
            static_cast<const CoreIrExtractElementInst &>(instruction);
        text += "  %" + get_emitted_value_name(&extract_instruction) +
                " = extractelement ";
        text += format_type(extract_instruction.get_vector_value()->get_type());
        text += " ";
        text += format_value_ref(extract_instruction.get_vector_value());
        text += ", ";
        text += format_type(extract_instruction.get_index()->get_type());
        text += " ";
        text += format_value_ref(extract_instruction.get_index());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::InsertElement: {
        const auto &insert_instruction =
            static_cast<const CoreIrInsertElementInst &>(instruction);
        text += "  %" + get_emitted_value_name(&insert_instruction) +
                " = insertelement ";
        text += format_type(insert_instruction.get_vector_value()->get_type());
        text += " ";
        text += format_value_ref(insert_instruction.get_vector_value());
        text += ", ";
        text += format_type(insert_instruction.get_element_value()->get_type());
        text += " ";
        text += format_value_ref(insert_instruction.get_element_value());
        text += ", ";
        text += format_type(insert_instruction.get_index()->get_type());
        text += " ";
        text += format_value_ref(insert_instruction.get_index());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::ShuffleVector: {
        const auto &shuffle_instruction =
            static_cast<const CoreIrShuffleVectorInst &>(instruction);
        text += "  %" + get_emitted_value_name(&shuffle_instruction) +
                " = shufflevector ";
        text += format_type(shuffle_instruction.get_lhs()->get_type());
        text += " ";
        text += format_value_ref(shuffle_instruction.get_lhs());
        text += ", ";
        text += format_type(shuffle_instruction.get_rhs()->get_type());
        text += " ";
        text += format_value_ref(shuffle_instruction.get_rhs());
        text += ", ";
        text += "<" + std::to_string(shuffle_instruction.get_mask_count()) + " x i32> <";
        for (std::size_t index = 0; index < shuffle_instruction.get_mask_count();
             ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += "i32 ";
            text += format_value_ref(shuffle_instruction.get_mask_value(index));
        }
        text += ">\n";
        return true;
    }
    case CoreIrOpcode::VectorReduceAdd: {
        const auto &reduce_instruction =
            static_cast<const CoreIrVectorReduceAddInst &>(instruction);
        const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(
            reduce_instruction.get_vector_value()->get_type());
        if (vector_type == nullptr) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "vector_reduce_add expected a vector operand",
                instruction.get_source_span());
            return false;
        }
        const std::string vector_suffix =
            "v" + std::to_string(vector_type->get_element_count()) +
            format_intrinsic_type_suffix(vector_type->get_element_type());
        if (vector_suffix == "v") {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "vector_reduce_add expected an intrinsic-lowerable element type",
                instruction.get_source_span());
            return false;
        }
        text += "  %" + get_emitted_value_name(&reduce_instruction) + " = call ";
        text += format_type(reduce_instruction.get_type());
        const auto *float_type =
            dynamic_cast<const CoreIrFloatType *>(vector_type->get_element_type());
        if (float_type != nullptr) {
            text += " @llvm.vector.reduce.fadd.";
            text += vector_suffix;
            text += "(";
            text += format_type(reduce_instruction.get_type());
            text += " ";
            text += reduce_instruction.get_start_value() == nullptr
                        ? "zeroinitializer"
                        : format_value_ref(reduce_instruction.get_start_value());
            text += ", ";
            text += format_type(reduce_instruction.get_vector_value()->get_type());
            text += " ";
            text += format_value_ref(reduce_instruction.get_vector_value());
            text += ")\n";
        } else {
            text += " @llvm.vector.reduce.add.";
            text += vector_suffix;
            text += "(";
            text += format_type(reduce_instruction.get_vector_value()->get_type());
            text += " ";
            text += format_value_ref(reduce_instruction.get_vector_value());
            text += ")\n";
        }
        return true;
    }
    case CoreIrOpcode::Cast: {
        const auto &cast_instruction =
            static_cast<const CoreIrCastInst &>(instruction);
        const CoreIrType *source_type =
            cast_instruction.get_operand() == nullptr
                ? nullptr
                : cast_instruction.get_operand()->get_type();
        const CoreIrType *target_type = cast_instruction.get_type();
        if (source_type == nullptr || target_type == nullptr) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "core ir llvm lowering expected cast source and target types",
                instruction.get_source_span());
            return false;
        }
        const auto *source_integer_type =
            dynamic_cast<const CoreIrIntegerType *>(source_type);
        const auto *target_integer_type =
            dynamic_cast<const CoreIrIntegerType *>(target_type);
        if (source_integer_type != nullptr && target_integer_type != nullptr &&
            source_integer_type->get_bit_width() ==
                target_integer_type->get_bit_width() &&
            (cast_instruction.get_cast_kind() == CoreIrCastKind::SignExtend ||
             cast_instruction.get_cast_kind() == CoreIrCastKind::ZeroExtend ||
             cast_instruction.get_cast_kind() == CoreIrCastKind::Truncate)) {
            text +=
                "  %" + get_emitted_value_name(&cast_instruction) + " = or ";
            text += format_type(target_type);
            text += " ";
            text += format_value_ref(cast_instruction.get_operand());
            text += ", 0\n";
            return true;
        }
        std::string cast_opcode;
        switch (cast_instruction.get_cast_kind()) {
        case CoreIrCastKind::SignExtend:
            cast_opcode = "sext";
            break;
        case CoreIrCastKind::ZeroExtend:
            cast_opcode = "zext";
            break;
        case CoreIrCastKind::Truncate:
            cast_opcode = "trunc";
            break;
        case CoreIrCastKind::SignedIntToFloat:
            cast_opcode = "sitofp";
            break;
        case CoreIrCastKind::UnsignedIntToFloat:
            cast_opcode = "uitofp";
            break;
        case CoreIrCastKind::FloatToSignedInt:
            cast_opcode = "fptosi";
            break;
        case CoreIrCastKind::FloatToUnsignedInt:
            cast_opcode = "fptoui";
            break;
        case CoreIrCastKind::FloatExtend:
            cast_opcode = "fpext";
            break;
        case CoreIrCastKind::FloatTruncate:
            cast_opcode = "fptrunc";
            break;
        case CoreIrCastKind::PtrToInt:
            cast_opcode = "ptrtoint";
            break;
        case CoreIrCastKind::IntToPtr:
            cast_opcode = "inttoptr";
            break;
        }

        if (const std::optional<std::string> folded_literal =
                try_format_folded_integer_cast_literal(cast_instruction);
            folded_literal.has_value()) {
            // Materialize folded narrow-int cast literals through a trivial
            // instruction so LLVM sees the signed spelling we intend.
            text +=
                "  %" + get_emitted_value_name(&cast_instruction) + " = add ";
            text += format_type(target_type);
            text += " 0, ";
            text += *folded_literal;
            text += "\n";
            return true;
        }

        text += "  %" + get_emitted_value_name(&cast_instruction) + " = ";
        text += cast_opcode;
        text += " ";
        text += format_type(source_type);
        text += " ";
        text += format_value_ref(cast_instruction.get_operand());
        text += " to ";
        text += format_type(target_type);
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Call: {
        const auto &call_instruction =
            static_cast<const CoreIrCallInst &>(instruction);
        text += "  ";
        if (!call_instruction.get_name().empty()) {
            text += "%" + get_emitted_value_name(&call_instruction);
            text += " = ";
        }
        text += "call ";
        if (call_instruction.get_is_direct_call() &&
            call_instruction.get_callee_type() != nullptr &&
            call_instruction.get_callee_type()->get_is_variadic()) {
            text += format_type(call_instruction.get_callee_type());
            text += " ";
        } else {
            text += format_type(call_instruction.get_type());
            text += " ";
        }
        if (call_instruction.get_is_direct_call()) {
            text += "@";
            text += call_instruction.get_callee_name();
        } else {
            text += format_value_ref(call_instruction.get_callee_value());
        }
        text += "(";
        const auto &arguments = call_instruction.get_operands();
        for (std::size_t index = call_instruction.get_argument_begin_index();
             index < arguments.size(); ++index) {
            if (index > call_instruction.get_argument_begin_index()) {
                text += ", ";
            }
            text += format_type(arguments[index]->get_type());
            text += " ";
            text += format_value_ref(arguments[index]);
        }
        text += ")\n";
        return true;
    }
    case CoreIrOpcode::Jump: {
        const auto &jump_instruction =
            static_cast<const CoreIrJumpInst &>(instruction);
        text += "  br label %";
        text += get_emitted_block_name(jump_instruction.get_target_block());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::CondJump: {
        const auto &cond_jump_instruction =
            static_cast<const CoreIrCondJumpInst &>(instruction);
        CoreIrValue *condition = cond_jump_instruction.get_condition();
        std::string condition_ref;
        if (condition != nullptr && condition->get_type() != nullptr &&
            condition->get_type()->get_kind() == CoreIrTypeKind::Integer &&
            static_cast<const CoreIrIntegerType *>(condition->get_type())
                    ->get_bit_width() == 1) {
            condition_ref = format_value_ref(condition);
        } else {
            const std::string helper_name = "%" + next_helper_name("t");
            if (is_float_type(condition->get_type())) {
                text += "  " + helper_name + " = fcmp une ";
                text += format_type(condition->get_type());
                text += " ";
                text += format_value_ref(condition);
                text += ", 0.0\n";
            } else if (condition->get_type() != nullptr &&
                       condition->get_type()->get_kind() ==
                           CoreIrTypeKind::Pointer) {
                text += "  " + helper_name + " = icmp ne ptr ";
                text += format_value_ref(condition);
                text += ", null\n";
            } else {
                text += "  " + helper_name + " = icmp ne ";
                text += format_type(condition->get_type());
                text += " ";
                text += format_value_ref(condition);
                text += ", 0\n";
            }
            condition_ref = helper_name;
        }
        text += "  br i1 ";
        text += condition_ref;
        text += ", label %";
        text += get_emitted_block_name(cond_jump_instruction.get_true_block());
        text += ", label %";
        text += get_emitted_block_name(cond_jump_instruction.get_false_block());
        text += "\n";
        return true;
    }
    case CoreIrOpcode::Return: {
        const auto &return_instruction =
            static_cast<const CoreIrReturnInst &>(instruction);
        if (return_instruction.get_return_value() == nullptr) {
            text += "  ret void\n";
            return true;
        }
        text += "  ret ";
        text += format_type(return_instruction.get_return_value()->get_type());
        text += " ";
        text += format_value_ref(return_instruction.get_return_value());
        text += "\n";
        return true;
    }
    }
    diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                "core ir llvm lowering encountered an unknown "
                                "instruction kind",
                                instruction.get_source_span());
    return false;
}

bool CoreIrLlvmTargetBackend::append_function(
    std::string &text, const CoreIrFunction &function,
    DiagnosticEngine &diagnostic_engine) {
    if (function.get_basic_blocks().empty()) {
        text += "declare ";
        if (function.get_is_internal_linkage()) {
            text += "internal ";
        }
        text += format_type(function.get_function_type()->get_return_type());
        text += " @";
        text += function.get_name();
        text += "(";
        const auto &parameter_types =
            function.get_function_type()->get_parameter_types();
        const auto &parameter_nocapture = function.get_parameter_nocapture();
        const auto &parameter_readonly = function.get_parameter_readonly();
        for (std::size_t index = 0; index < parameter_types.size(); ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += format_type(parameter_types[index]);
            if (index < parameter_nocapture.size() && parameter_nocapture[index]) {
                text += " nocapture";
            }
            if (index < parameter_readonly.size() && parameter_readonly[index] &&
                parameter_can_emit_readonly_attr(parameter_types[index])) {
                text += " readonly";
            }
        }
        if (function.get_is_variadic()) {
            if (!parameter_types.empty()) {
                text += ", ";
            }
            text += "...";
        }
        text += ")\n";
        return true;
    }

    text += "define ";
    if (function.get_is_internal_linkage()) {
        text += "internal ";
    }
    text += format_type(function.get_function_type()->get_return_type());
    text += " @";
    text += function.get_name();
    text += "(";
    const auto &parameters = function.get_parameters();
    const auto &parameter_nocapture = function.get_parameter_nocapture();
    const auto &parameter_readonly = function.get_parameter_readonly();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) {
            text += ", ";
        }
        text += format_type(parameters[index]->get_type());
        if (index < parameter_nocapture.size() && parameter_nocapture[index]) {
            text += " nocapture";
        }
        if (index < parameter_readonly.size() && parameter_readonly[index] &&
            parameter_can_emit_readonly_attr(parameters[index]->get_type())) {
            text += " readonly";
        }
        text += " %";
        text += parameters[index]->get_name();
    }
    if (function.get_is_variadic()) {
        if (!parameters.empty()) {
            text += ", ";
        }
        text += "...";
    }
    text += ")";
    if (function.get_is_always_inline()) {
        text += " alwaysinline";
    }
    if (function.get_is_readnone()) {
        text += " readnone";
    } else if (function.get_is_readonly()) {
        text += " readonly";
    } else if (function.get_is_writeonly()) {
        text += " writeonly";
    }
    text += " {\n";
    emitted_value_names_.clear();
    emitted_block_names_.clear();
    emitted_stack_slot_names_.clear();
    used_block_names_.clear();
    used_stack_slot_names_.clear();
    next_value_id_ = 0;
    helper_id_ = 0;

    const auto *entry_block = function.get_basic_blocks().empty()
                                  ? nullptr
                                  : function.get_basic_blocks().front().get();
    for (const auto &basic_block : function.get_basic_blocks()) {
        text += get_emitted_block_name(basic_block.get());
        text += ":\n";
        if (basic_block.get() == entry_block) {
            for (const auto &stack_slot : function.get_stack_slots()) {
                text += "  %";
                text += get_emitted_stack_slot_name(stack_slot.get());
                text += " = alloca ";
                text += format_type(stack_slot->get_allocated_type());
                text += "\n";
            }
        }
        for (const auto &instruction : basic_block->get_instructions()) {
            if (instruction->get_opcode() != CoreIrOpcode::Phi) {
                continue;
            }
            if (!append_instruction(text, *instruction, diagnostic_engine)) {
                return false;
            }
        }
        for (const auto &instruction : basic_block->get_instructions()) {
            if (instruction->get_opcode() == CoreIrOpcode::Phi) {
                continue;
            }
            if (!append_instruction(text, *instruction, diagnostic_engine)) {
                return false;
            }
        }
    }
    text += "}\n";
    return true;
}

void CoreIrLlvmTargetBackend::append_global(std::string &text,
                                            const CoreIrGlobal &global) const {
    text += "@";
    text += global.get_name();
    text += " = ";
    if (global.get_initializer() == nullptr) {
        text += global.get_is_internal_linkage() ? "internal " : "external ";
        text += "global ";
        text += format_type(global.get_type());
        text += "\n";
        return;
    }
    if (global.get_is_constant()) {
        if (global.get_is_internal_linkage()) {
            text += "private unnamed_addr constant ";
        } else {
            text += "constant ";
        }
    } else {
        if (global.get_is_internal_linkage()) {
            text += "internal ";
        }
        text += "global ";
    }
    text += format_type(global.get_type());
    text += " ";
    text += format_constant(global.get_initializer());
    text += "\n";
}

IrKind CoreIrLlvmTargetBackend::get_kind() const noexcept {
    return IrKind::LLVM;
}

std::unique_ptr<IRResult>
CoreIrLlvmTargetBackend::Lower(const CoreIrModule &module,
                               DiagnosticEngine &diagnostic_engine) {
    helper_id_ = 0;
    std::string text;
    const std::string target_datalayout = get_default_target_datalayout();
    const std::string target_triple = get_default_target_triple();
    if (!target_datalayout.empty()) {
        text += "target datalayout = \"";
        text += target_datalayout;
        text += "\"\n";
    }
    if (!target_triple.empty()) {
        text += "target triple = \"";
        text += target_triple;
        text += "\"\n";
    }
    if (!target_datalayout.empty() || !target_triple.empty()) {
        text += "\n";
    }

    for (const auto &global : module.get_globals()) {
        append_global(text, *global);
    }
    if (!module.get_globals().empty() && !module.get_functions().empty()) {
        text += "\n";
    }
    for (std::size_t index = 0; index < module.get_functions().size();
         ++index) {
        if (!append_function(text, *module.get_functions()[index],
                             diagnostic_engine)) {
            return nullptr;
        }
        if (index + 1 < module.get_functions().size()) {
            text += "\n";
        }
    }
    return std::make_unique<IRResult>(get_kind(), std::move(text));
}

} // namespace sysycc
