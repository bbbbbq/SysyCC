#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"

#include <sstream>
#include <string>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

std::string format_byte_string(const std::vector<std::uint8_t> &bytes) {
    std::ostringstream stream;
    stream << "c\"";
    for (std::uint8_t byte : bytes) {
        if (byte >= 32 && byte <= 126 && byte != '\\' && byte != '"') {
            stream << static_cast<char>(byte);
            continue;
        }
        stream << '\\';
        constexpr char k_hex_digits[] = "0123456789ABCDEF";
        stream << k_hex_digits[(byte >> 4) & 0xF];
        stream << k_hex_digits[byte & 0xF];
    }
    stream << "\"";
    return stream.str();
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
    return "binary";
}

std::string format_unary_opcode(CoreIrUnaryOpcode opcode) {
    switch (opcode) {
    case CoreIrUnaryOpcode::Negate:
        return "neg";
    case CoreIrUnaryOpcode::BitwiseNot:
        return "not";
    case CoreIrUnaryOpcode::LogicalNot:
        return "lnot";
    }
    return "unary";
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
    return "cmp";
}

std::string format_cast_kind(CoreIrCastKind cast_kind) {
    switch (cast_kind) {
    case CoreIrCastKind::SignExtend:
        return "sext";
    case CoreIrCastKind::ZeroExtend:
        return "zext";
    case CoreIrCastKind::Truncate:
        return "trunc";
    case CoreIrCastKind::SignedIntToFloat:
        return "sitofp";
    case CoreIrCastKind::UnsignedIntToFloat:
        return "uitofp";
    case CoreIrCastKind::FloatToSignedInt:
        return "fptosi";
    case CoreIrCastKind::FloatToUnsignedInt:
        return "fptoui";
    case CoreIrCastKind::FloatExtend:
        return "fpext";
    case CoreIrCastKind::FloatTruncate:
        return "fptrunc";
    case CoreIrCastKind::PtrToInt:
        return "ptrtoint";
    case CoreIrCastKind::IntToPtr:
        return "inttoptr";
    }
    return "cast";
}

} // namespace

std::string CoreIrRawPrinter::format_type(const CoreIrType *type) const {
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
            return "f16";
        case CoreIrFloatKind::Float32:
            return "f32";
        case CoreIrFloatKind::Float64:
            return "f64";
        case CoreIrFloatKind::Float128:
            return "f128";
        }
        return "float";
    }
    case CoreIrTypeKind::Vector: {
        const auto *vector_type = static_cast<const CoreIrVectorType *>(type);
        return "<" + std::to_string(vector_type->get_element_count()) + " x " +
               format_type(vector_type->get_element_type()) + ">";
    }
    case CoreIrTypeKind::Pointer:
        return format_type(static_cast<const CoreIrPointerType *>(type)
                               ->get_pointee_type()) +
               "*";
    case CoreIrTypeKind::Array: {
        const auto *array_type = static_cast<const CoreIrArrayType *>(type);
        return "[" + std::to_string(array_type->get_element_count()) + " x " +
               format_type(array_type->get_element_type()) + "]";
    }
    case CoreIrTypeKind::Struct: {
        const auto *struct_type = static_cast<const CoreIrStructType *>(type);
        std::string result = "{ ";
        const auto &element_types = struct_type->get_element_types();
        for (std::size_t index = 0; index < element_types.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += format_type(element_types[index]);
        }
        result += " }";
        return result;
    }
    case CoreIrTypeKind::Function: {
        const auto *function_type =
            static_cast<const CoreIrFunctionType *>(type);
        std::string result =
            format_type(function_type->get_return_type()) + " (";
        const auto &parameter_types = function_type->get_parameter_types();
        for (std::size_t index = 0; index < parameter_types.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += format_type(parameter_types[index]);
        }
        if (function_type->get_is_variadic()) {
            if (!parameter_types.empty()) {
                result += ", ";
            }
            result += "...";
        }
        result += ")";
        return result;
    }
    }

    return "<unknown-type>";
}

std::string
CoreIrRawPrinter::format_constant(const CoreIrConstant *constant) const {
    if (constant == nullptr) {
        return "<null-constant>";
    }
    if (const auto *int_constant =
            dynamic_cast<const CoreIrConstantInt *>(constant);
        int_constant != nullptr) {
        return std::to_string(int_constant->get_value());
    }
    if (const auto *float_constant =
            dynamic_cast<const CoreIrConstantFloat *>(constant);
        float_constant != nullptr) {
        return float_constant->get_literal_text();
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
        return format_byte_string(byte_string->get_bytes());
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
        std::string result = is_array ? "[ " : is_vector ? "< " : "{ ";
        const auto &elements = aggregate->get_elements();
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += format_constant(elements[index]);
        }
        result += is_array ? " ]" : is_vector ? " >" : " }";
        return result;
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
        return "<global-address>";
    }
    if (const auto *gep_constant =
            dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
        gep_constant != nullptr) {
        std::string result =
            "getelementptr(" + format_constant(gep_constant->get_base());
        for (const CoreIrConstant *index : gep_constant->get_indices()) {
            result += ", " + format_constant(index);
        }
        result += ")";
        return result;
    }
    return "<constant>";
}

std::string CoreIrRawPrinter::format_value(const CoreIrValue *value) const {
    if (value == nullptr) {
        return "<null>";
    }
    if (const auto *constant = dynamic_cast<const CoreIrConstant *>(value);
        constant != nullptr) {
        return format_constant(constant);
    }
    if (!value->get_name().empty()) {
        return "%" + value->get_name();
    }
    return "<unnamed>";
}

std::string CoreIrRawPrinter::format_instruction(
    const CoreIrInstruction &instruction) const {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi: {
        const auto &phi_instruction =
            static_cast<const CoreIrPhiInst &>(instruction);
        std::string text = format_value(&phi_instruction) + " = phi " +
                           format_type(phi_instruction.get_type());
        for (std::size_t index = 0;
             index < phi_instruction.get_incoming_count(); ++index) {
            CoreIrBasicBlock *incoming_block =
                phi_instruction.get_incoming_block(index);
            CoreIrValue *incoming_value =
                phi_instruction.get_incoming_value(index);
            if (incoming_block == nullptr || incoming_value == nullptr) {
                continue;
            }
            text += index == 0 ? " " : ", ";
            text += "[ ";
            text += format_value(incoming_value);
            text += ", %";
            text += incoming_block->get_name();
            text += " ]";
        }
        return text;
    }
    case CoreIrOpcode::Binary: {
        const auto &binary_instruction =
            static_cast<const CoreIrBinaryInst &>(instruction);
        return format_value(&binary_instruction) + " = " +
               format_binary_opcode(binary_instruction.get_binary_opcode()) +
               " " + format_type(binary_instruction.get_type()) + " " +
               format_value(binary_instruction.get_lhs()) + ", " +
               format_value(binary_instruction.get_rhs());
    }
    case CoreIrOpcode::Unary: {
        const auto &unary_instruction =
            static_cast<const CoreIrUnaryInst &>(instruction);
        return format_value(&unary_instruction) + " = " +
               format_unary_opcode(unary_instruction.get_unary_opcode()) + " " +
               format_type(unary_instruction.get_type()) + " " +
               format_value(unary_instruction.get_operand());
    }
    case CoreIrOpcode::Compare: {
        const auto &compare_instruction =
            static_cast<const CoreIrCompareInst &>(instruction);
        return format_value(&compare_instruction) + " = icmp " +
               format_compare_predicate(compare_instruction.get_predicate()) +
               " " + format_type(compare_instruction.get_lhs()->get_type()) +
               " " + format_value(compare_instruction.get_lhs()) + ", " +
               format_value(compare_instruction.get_rhs());
    }
    case CoreIrOpcode::Select: {
        const auto &select_instruction =
            static_cast<const CoreIrSelectInst &>(instruction);
        return format_value(&select_instruction) + " = select " +
               format_type(select_instruction.get_condition()->get_type()) +
               " " + format_value(select_instruction.get_condition()) + ", " +
               format_type(select_instruction.get_true_value()->get_type()) +
               " " + format_value(select_instruction.get_true_value()) + ", " +
               format_value(select_instruction.get_false_value());
    }
    case CoreIrOpcode::ExtractElement: {
        const auto &extract_instruction =
            static_cast<const CoreIrExtractElementInst &>(instruction);
        return format_value(&extract_instruction) + " = extractelement " +
               format_type(extract_instruction.get_vector_value()->get_type()) +
               " " + format_value(extract_instruction.get_vector_value()) + ", " +
               format_type(extract_instruction.get_index()->get_type()) + " " +
               format_value(extract_instruction.get_index());
    }
    case CoreIrOpcode::InsertElement: {
        const auto &insert_instruction =
            static_cast<const CoreIrInsertElementInst &>(instruction);
        return format_value(&insert_instruction) + " = insertelement " +
               format_type(insert_instruction.get_vector_value()->get_type()) +
               " " + format_value(insert_instruction.get_vector_value()) + ", " +
               format_type(insert_instruction.get_element_value()->get_type()) +
               " " + format_value(insert_instruction.get_element_value()) + ", " +
               format_type(insert_instruction.get_index()->get_type()) + " " +
               format_value(insert_instruction.get_index());
    }
    case CoreIrOpcode::ShuffleVector: {
        const auto &shuffle_instruction =
            static_cast<const CoreIrShuffleVectorInst &>(instruction);
        std::string text =
            format_value(&shuffle_instruction) + " = shufflevector " +
            format_type(shuffle_instruction.get_lhs()->get_type()) + " " +
            format_value(shuffle_instruction.get_lhs()) + ", " +
            format_type(shuffle_instruction.get_rhs()->get_type()) + " " +
            format_value(shuffle_instruction.get_rhs());
        for (std::size_t index = 0; index < shuffle_instruction.get_mask_count();
             ++index) {
            text += ", ";
            text += format_value(shuffle_instruction.get_mask_value(index));
        }
        return text;
    }
    case CoreIrOpcode::VectorReduceAdd: {
        const auto &reduce_instruction =
            static_cast<const CoreIrVectorReduceAddInst &>(instruction);
        std::string text =
            format_value(&reduce_instruction) + " = vector_reduce_add ";
        if (reduce_instruction.get_start_value() != nullptr) {
            text += format_type(reduce_instruction.get_start_value()->get_type()) + " " +
                    format_value(reduce_instruction.get_start_value()) + ", ";
        }
        text += format_type(reduce_instruction.get_vector_value()->get_type()) + " " +
                format_value(reduce_instruction.get_vector_value());
        return text;
    }
    case CoreIrOpcode::Cast: {
        const auto &cast_instruction =
            static_cast<const CoreIrCastInst &>(instruction);
        return format_value(&cast_instruction) + " = " +
               format_cast_kind(cast_instruction.get_cast_kind()) + " " +
               format_type(cast_instruction.get_type()) + " " +
               format_value(cast_instruction.get_operand());
    }
    case CoreIrOpcode::AddressOfFunction: {
        const auto &address_of_function_instruction =
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction);
        return format_value(&address_of_function_instruction) +
               " = addr_of_function " +
               format_type(address_of_function_instruction.get_type()) + " @" +
               address_of_function_instruction.get_function()->get_name();
    }
    case CoreIrOpcode::AddressOfGlobal: {
        const auto &address_of_global_instruction =
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction);
        return format_value(&address_of_global_instruction) +
               " = addr_of_global " +
               format_type(address_of_global_instruction.get_type()) + " @" +
               address_of_global_instruction.get_global()->get_name();
    }
    case CoreIrOpcode::AddressOfStackSlot: {
        const auto &address_of_stack_slot_instruction =
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction);
        return format_value(&address_of_stack_slot_instruction) +
               " = addr_of_stackslot " +
               format_type(address_of_stack_slot_instruction.get_type()) +
               " %" +
               address_of_stack_slot_instruction.get_stack_slot()->get_name();
    }
    case CoreIrOpcode::GetElementPtr: {
        const auto &gep_instruction =
            static_cast<const CoreIrGetElementPtrInst &>(instruction);
        std::string text = format_value(&gep_instruction) + " = gep " +
                           format_type(gep_instruction.get_type()) + " " +
                           format_value(gep_instruction.get_base());
        for (std::size_t index = 0; index < gep_instruction.get_index_count();
             ++index) {
            text += ", ";
            text += format_value(gep_instruction.get_index(index));
        }
        return text;
    }
    case CoreIrOpcode::Load: {
        const auto &load_instruction =
            static_cast<const CoreIrLoadInst &>(instruction);
        std::string text = format_value(&load_instruction) + " = load " +
                           format_type(load_instruction.get_type()) + ", ";
        if (load_instruction.get_stack_slot() != nullptr) {
            text += "%";
            text += load_instruction.get_stack_slot()->get_name();
            if (load_instruction.get_alignment() > 0) {
                text += ", align ";
                text += std::to_string(load_instruction.get_alignment());
            }
            return text;
        }
        text += format_value(load_instruction.get_address());
        if (load_instruction.get_alignment() > 0) {
            text += ", align ";
            text += std::to_string(load_instruction.get_alignment());
        }
        return text;
    }
    case CoreIrOpcode::Store: {
        const auto &store_instruction =
            static_cast<const CoreIrStoreInst &>(instruction);
        std::string text =
            "store " + format_type(store_instruction.get_value()->get_type()) +
            " " + format_value(store_instruction.get_value()) + ", ";
        if (store_instruction.get_stack_slot() != nullptr) {
            text += "%";
            text += store_instruction.get_stack_slot()->get_name();
            if (store_instruction.get_alignment() > 0) {
                text += ", align ";
                text += std::to_string(store_instruction.get_alignment());
            }
            return text;
        }
        text += format_value(store_instruction.get_address());
        if (store_instruction.get_alignment() > 0) {
            text += ", align ";
            text += std::to_string(store_instruction.get_alignment());
        }
        return text;
    }
    case CoreIrOpcode::Call: {
        const auto &call_instruction =
            static_cast<const CoreIrCallInst &>(instruction);
        std::string call_text;
        if (!call_instruction.get_name().empty()) {
            call_text += format_value(&call_instruction);
            call_text += " = ";
        }
        call_text += "call ";
        call_text += format_type(call_instruction.get_type());
        call_text += " ";
        if (call_instruction.get_is_direct_call()) {
            call_text += "@";
            call_text += call_instruction.get_callee_name();
        } else {
            call_text += format_value(call_instruction.get_callee_value());
        }
        call_text += "(";
        const auto &arguments = call_instruction.get_operands();
        for (std::size_t index = call_instruction.get_argument_begin_index();
             index < arguments.size(); ++index) {
            if (index > call_instruction.get_argument_begin_index()) {
                call_text += ", ";
            }
            call_text += format_type(arguments[index]->get_type());
            call_text += " ";
            call_text += format_value(arguments[index]);
        }
        call_text += ")";
        return call_text;
    }
    case CoreIrOpcode::Jump: {
        const auto &jump_instruction =
            static_cast<const CoreIrJumpInst &>(instruction);
        return "jmp %" + jump_instruction.get_target_block()->get_name();
    }
    case CoreIrOpcode::CondJump: {
        const auto &cond_jump_instruction =
            static_cast<const CoreIrCondJumpInst &>(instruction);
        return "br " +
               format_type(cond_jump_instruction.get_condition()->get_type()) +
               " " + format_value(cond_jump_instruction.get_condition()) +
               ", label %" +
               cond_jump_instruction.get_true_block()->get_name() +
               ", label %" +
               cond_jump_instruction.get_false_block()->get_name();
    }
    case CoreIrOpcode::Return: {
        const auto &return_instruction =
            static_cast<const CoreIrReturnInst &>(instruction);
        if (return_instruction.get_return_value() == nullptr) {
            return "ret void";
        }
        return "ret " +
               format_type(return_instruction.get_return_value()->get_type()) +
               " " + format_value(return_instruction.get_return_value());
    }
    }
    return "<instruction>";
}

void CoreIrRawPrinter::append_global(std::string &output,
                                     const CoreIrGlobal &global) const {
    output += global.get_is_constant() ? "const " : "global ";
    output += "@";
    output += global.get_name();
    output += " : ";
    output += format_type(global.get_type());
    if (global.get_initializer() != nullptr) {
        output += " = ";
        output += format_constant(global.get_initializer());
    }
    if (global.get_is_internal_linkage()) {
        output += " internal";
    }
    output += "\n";
}

void CoreIrRawPrinter::append_block(std::string &output,
                                    const CoreIrBasicBlock &basic_block) const {
    output += basic_block.get_name();
    output += ":\n";
    for (const auto &instruction : basic_block.get_instructions()) {
        output += "  ";
        output += format_instruction(*instruction);
        output += "\n";
    }
}

void CoreIrRawPrinter::append_function(std::string &output,
                                       const CoreIrFunction &function) const {
    output += "func @";
    output += function.get_name();
    output += "(";
    const auto &parameters = function.get_parameters();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) {
            output += ", ";
        }
        output += format_type(parameters[index]->get_type());
        output += " ";
        output += format_value(parameters[index].get());
    }
    if (function.get_is_variadic()) {
        if (!parameters.empty()) {
            output += ", ";
        }
        output += "...";
    }
    output += ") -> ";
    output += format_type(function.get_function_type()->get_return_type());
    if (function.get_is_internal_linkage()) {
        output += " internal";
    }
    output += " {\n";
    for (const auto &stack_slot : function.get_stack_slots()) {
        output += "  stackslot %";
        output += stack_slot->get_name();
        output += " : ";
        output += format_type(stack_slot->get_allocated_type());
        output += ", align ";
        output += std::to_string(stack_slot->get_alignment());
        output += "\n";
    }
    if (!function.get_stack_slots().empty() &&
        !function.get_basic_blocks().empty()) {
        output += "\n";
    }
    for (const auto &basic_block : function.get_basic_blocks()) {
        append_block(output, *basic_block);
    }
    output += "}\n";
}

std::string CoreIrRawPrinter::print_module(const CoreIrModule &module) const {
    std::string output = "module ";
    output += module.get_name();
    output += "\n";

    if (!module.get_globals().empty()) {
        output += "\n";
        for (const auto &global : module.get_globals()) {
            append_global(output, *global);
        }
    }

    if (!module.get_functions().empty()) {
        output += "\n";
        for (std::size_t index = 0; index < module.get_functions().size();
             ++index) {
            append_function(output, *module.get_functions()[index]);
            if (index + 1 < module.get_functions().size()) {
                output += "\n";
            }
        }
    }

    return output;
}

} // namespace sysycc
