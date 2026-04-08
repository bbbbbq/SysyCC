#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc::detail {

const CoreIrIntegerType *as_integer_type(const CoreIrType *type) {
    return dynamic_cast<const CoreIrIntegerType *>(type);
}

bool are_equivalent_types(const CoreIrType *lhs, const CoreIrType *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr || lhs->get_kind() != rhs->get_kind()) {
        return false;
    }
    switch (lhs->get_kind()) {
    case CoreIrTypeKind::Void:
        return true;
    case CoreIrTypeKind::Integer:
        return static_cast<const CoreIrIntegerType *>(lhs)->get_bit_width() ==
               static_cast<const CoreIrIntegerType *>(rhs)->get_bit_width();
    case CoreIrTypeKind::Float:
        return static_cast<const CoreIrFloatType *>(lhs)->get_float_kind() ==
               static_cast<const CoreIrFloatType *>(rhs)->get_float_kind();
    case CoreIrTypeKind::Pointer:
        return are_equivalent_types(
            static_cast<const CoreIrPointerType *>(lhs)->get_pointee_type(),
            static_cast<const CoreIrPointerType *>(rhs)->get_pointee_type());
    case CoreIrTypeKind::Array:
        return static_cast<const CoreIrArrayType *>(lhs)->get_element_count() ==
                   static_cast<const CoreIrArrayType *>(rhs)->get_element_count() &&
               are_equivalent_types(
                   static_cast<const CoreIrArrayType *>(lhs)->get_element_type(),
                   static_cast<const CoreIrArrayType *>(rhs)->get_element_type());
    case CoreIrTypeKind::Struct: {
        const auto &lhs_elements =
            static_cast<const CoreIrStructType *>(lhs)->get_element_types();
        const auto &rhs_elements =
            static_cast<const CoreIrStructType *>(rhs)->get_element_types();
        if (lhs_elements.size() != rhs_elements.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lhs_elements.size(); ++index) {
            if (!are_equivalent_types(lhs_elements[index], rhs_elements[index])) {
                return false;
            }
        }
        return true;
    }
    case CoreIrTypeKind::Function: {
        const auto *lhs_function = static_cast<const CoreIrFunctionType *>(lhs);
        const auto *rhs_function = static_cast<const CoreIrFunctionType *>(rhs);
        if (lhs_function->get_is_variadic() != rhs_function->get_is_variadic() ||
            !are_equivalent_types(lhs_function->get_return_type(),
                                  rhs_function->get_return_type())) {
            return false;
        }
        const auto &lhs_parameters = lhs_function->get_parameter_types();
        const auto &rhs_parameters = rhs_function->get_parameter_types();
        if (lhs_parameters.size() != rhs_parameters.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lhs_parameters.size(); ++index) {
            if (!are_equivalent_types(lhs_parameters[index], rhs_parameters[index])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

void append_type_key(std::string &key, const CoreIrType *type) {
    if (type == nullptr) {
        key += "null;";
        return;
    }

    key += std::to_string(static_cast<int>(type->get_kind()));
    key.push_back(':');
    switch (type->get_kind()) {
    case CoreIrTypeKind::Void:
        key += "void;";
        return;
    case CoreIrTypeKind::Integer:
        key += std::to_string(
            static_cast<const CoreIrIntegerType *>(type)->get_bit_width());
        key.push_back(';');
        return;
    case CoreIrTypeKind::Float:
        key += std::to_string(static_cast<int>(
            static_cast<const CoreIrFloatType *>(type)->get_float_kind()));
        key.push_back(';');
        return;
    case CoreIrTypeKind::Pointer:
        append_type_key(
            key, static_cast<const CoreIrPointerType *>(type)->get_pointee_type());
        key.push_back(';');
        return;
    case CoreIrTypeKind::Array: {
        const auto *array_type = static_cast<const CoreIrArrayType *>(type);
        key += std::to_string(array_type->get_element_count());
        key.push_back('x');
        append_type_key(key, array_type->get_element_type());
        key.push_back(';');
        return;
    }
    case CoreIrTypeKind::Struct: {
        key.push_back('{');
        for (const CoreIrType *element_type :
             static_cast<const CoreIrStructType *>(type)->get_element_types()) {
            append_type_key(key, element_type);
            key.push_back(',');
        }
        key += "};";
        return;
    }
    case CoreIrTypeKind::Function: {
        const auto *function_type = static_cast<const CoreIrFunctionType *>(type);
        key += function_type->get_is_variadic() ? "var:" : "fixed:";
        append_type_key(key, function_type->get_return_type());
        key.push_back('(');
        for (const CoreIrType *parameter_type :
             function_type->get_parameter_types()) {
            append_type_key(key, parameter_type);
            key.push_back(',');
        }
        key += ");";
        return;
    }
    }
}

void append_value_key(std::string &key, const CoreIrValue *value) {
    if (value == nullptr) {
        key += "null;";
        return;
    }
    if (const auto *constant_int = dynamic_cast<const CoreIrConstantInt *>(value);
        constant_int != nullptr) {
        key += "cint:";
        append_type_key(key, constant_int->get_type());
        key += std::to_string(constant_int->get_value());
        key.push_back(';');
        return;
    }
    if (const auto *constant_float =
            dynamic_cast<const CoreIrConstantFloat *>(value);
        constant_float != nullptr) {
        key += "cfloat:";
        append_type_key(key, constant_float->get_type());
        key += constant_float->get_literal_text();
        key.push_back(';');
        return;
    }
    if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
        key += "cnull:";
        append_type_key(key, value->get_type());
        key.push_back(';');
        return;
    }
    if (dynamic_cast<const CoreIrConstantZeroInitializer *>(value) != nullptr) {
        key += "czero:";
        append_type_key(key, value->get_type());
        key.push_back(';');
        return;
    }
    if (const auto *byte_string =
            dynamic_cast<const CoreIrConstantByteString *>(value);
        byte_string != nullptr) {
        key += "cbytes:";
        append_type_key(key, byte_string->get_type());
        for (std::uint8_t byte : byte_string->get_bytes()) {
            key += std::to_string(byte);
            key.push_back(',');
        }
        key.push_back(';');
        return;
    }
    if (const auto *aggregate =
            dynamic_cast<const CoreIrConstantAggregate *>(value);
        aggregate != nullptr) {
        key += "cagg:";
        append_type_key(key, aggregate->get_type());
        key.push_back('[');
        for (const CoreIrConstant *element : aggregate->get_elements()) {
            append_value_key(key, element);
            key.push_back(',');
        }
        key += "];";
        return;
    }
    if (const auto *global_address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
        global_address != nullptr) {
        key += "caddr:";
        append_type_key(key, global_address->get_type());
        key += std::to_string(reinterpret_cast<std::uintptr_t>(
            global_address->get_global() != nullptr
                ? static_cast<const void *>(global_address->get_global())
                : static_cast<const void *>(global_address->get_function())));
        key.push_back(';');
        return;
    }
    if (const auto *constant_gep =
            dynamic_cast<const CoreIrConstantGetElementPtr *>(value);
        constant_gep != nullptr) {
        key += "cgep:";
        append_type_key(key, constant_gep->get_type());
        append_value_key(key, constant_gep->get_base());
        key.push_back('[');
        for (const CoreIrConstant *index : constant_gep->get_indices()) {
            append_value_key(key, index);
            key.push_back(',');
        }
        key += "];";
        return;
    }

    key += "v:";
    key += std::to_string(reinterpret_cast<std::uintptr_t>(value));
    key.push_back(';');
}

namespace {

bool are_equivalent_values(const CoreIrValue *lhs, const CoreIrValue *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    std::string lhs_key;
    std::string rhs_key;
    append_value_key(lhs_key, lhs);
    append_value_key(rhs_key, rhs);
    return lhs_key == rhs_key;
}

} // namespace

const CoreIrConstantInt *as_integer_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

bool is_zero_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    return constant != nullptr && constant->get_value() == 0;
}

bool is_one_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    return constant != nullptr && constant->get_value() == 1;
}

bool is_all_ones_integer_constant(const CoreIrValue *value) {
    const auto *constant = as_integer_constant(value);
    const auto *integer_type =
        constant == nullptr ? nullptr : as_integer_type(constant->get_type());
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    const std::uint64_t all_ones =
        bit_width >= 64 ? std::numeric_limits<std::uint64_t>::max()
                        : ((std::uint64_t{1} << bit_width) - 1);
    return constant->get_value() == all_ones;
}

std::optional<std::size_t> get_integer_bit_width(const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return std::nullopt;
    }
    return integer_type->get_bit_width();
}

const CoreIrType *get_pointer_pointee_type(const CoreIrValue *value) {
    if (value == nullptr) {
        return nullptr;
    }
    const auto *pointer_type =
        dynamic_cast<const CoreIrPointerType *>(value->get_type());
    return pointer_type == nullptr ? nullptr : pointer_type->get_pointee_type();
}

const CoreIrType *get_gep_result_pointee_type(const CoreIrGetElementPtrInst &gep) {
    const auto *pointer_type =
        dynamic_cast<const CoreIrPointerType *>(gep.get_type());
    return pointer_type == nullptr ? nullptr : pointer_type->get_pointee_type();
}

bool is_trivial_zero_index_gep(const CoreIrGetElementPtrInst &gep) {
    return gep.get_index_count() == 1 && is_zero_integer_constant(gep.get_index(0)) &&
           gep.get_base() != nullptr && gep.get_base()->get_type() == gep.get_type();
}

CoreIrValue *unwrap_trivial_zero_index_geps(CoreIrValue *value) {
    CoreIrValue *current = value;
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(current);
    while (gep != nullptr && is_trivial_zero_index_gep(*gep)) {
        current = gep->get_base();
        gep = dynamic_cast<CoreIrGetElementPtrInst *>(current);
    }
    return current;
}

const CoreIrType *get_selected_gep_pointee_type(const CoreIrGetElementPtrInst &gep) {
    const CoreIrType *current_type = get_pointer_pointee_type(gep.get_base());
    if (current_type == nullptr) {
        return nullptr;
    }

    std::size_t start_index = 0;
    if (is_trivial_zero_index_gep(gep) ||
        (gep.get_index_count() > 1 && is_zero_integer_constant(gep.get_index(0)))) {
        start_index = 1;
    }

    for (std::size_t index = start_index; index < gep.get_index_count(); ++index) {
        if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
            array_type != nullptr) {
            current_type = array_type->get_element_type();
            continue;
        }
        if (const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(current_type);
            struct_type != nullptr) {
            if (start_index == 0 && index == 0) {
                return nullptr;
            }
            const auto *index_constant = as_integer_constant(gep.get_index(index));
            if (index_constant == nullptr) {
                return nullptr;
            }
            const std::uint64_t field_index = index_constant->get_value();
            if (field_index >= struct_type->get_element_types().size()) {
                return nullptr;
            }
            current_type = struct_type->get_element_types()[field_index];
            continue;
        }
        return nullptr;
    }

    return current_type;
}

bool can_flatten_structural_gep(const CoreIrGetElementPtrInst &gep) {
    return get_selected_gep_pointee_type(gep) == get_gep_result_pointee_type(gep);
}

bool collect_structural_gep_chain(const CoreIrGetElementPtrInst &gep,
                                  CoreIrValue *&root_base,
                                  std::vector<CoreIrValue *> &indices) {
    if (!can_flatten_structural_gep(gep)) {
        return false;
    }

    if (auto *inner_gep = dynamic_cast<CoreIrGetElementPtrInst *>(gep.get_base());
        inner_gep != nullptr &&
        collect_structural_gep_chain(*inner_gep, root_base, indices)) {
        const bool drop_outer_leading_zero = is_zero_integer_constant(gep.get_index(0));
        const std::size_t outer_start_index = drop_outer_leading_zero ? 1 : 0;
        for (std::size_t index = outer_start_index; index < gep.get_index_count();
             ++index) {
            indices.push_back(gep.get_index(index));
        }
        return true;
    }

    root_base = unwrap_trivial_zero_index_geps(gep.get_base());
    for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
        indices.push_back(gep.get_index(index));
    }
    return true;
}

bool are_equivalent_pointer_values(const CoreIrValue *lhs,
                                   const CoreIrValue *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    auto *lhs_root = unwrap_trivial_zero_index_geps(
        const_cast<CoreIrValue *>(lhs));
    auto *rhs_root = unwrap_trivial_zero_index_geps(
        const_cast<CoreIrValue *>(rhs));
    if (lhs_root == rhs_root) {
        return true;
    }

    auto *lhs_gep = dynamic_cast<CoreIrGetElementPtrInst *>(lhs_root);
    auto *rhs_gep = dynamic_cast<CoreIrGetElementPtrInst *>(rhs_root);
    if (lhs_gep == nullptr || rhs_gep == nullptr) {
        return false;
    }

    CoreIrValue *lhs_chain_root = nullptr;
    CoreIrValue *rhs_chain_root = nullptr;
    std::vector<CoreIrValue *> lhs_indices;
    std::vector<CoreIrValue *> rhs_indices;
    if (!collect_structural_gep_chain(*lhs_gep, lhs_chain_root, lhs_indices) ||
        !collect_structural_gep_chain(*rhs_gep, rhs_chain_root, rhs_indices) ||
        lhs_chain_root != rhs_chain_root ||
        lhs_indices.size() != rhs_indices.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs_indices.size(); ++index) {
        if (!are_equivalent_values(lhs_indices[index], rhs_indices[index])) {
            return false;
        }
    }
    return true;
}

bool normalize_constant_stack_slot_path(CoreIrValue *value,
                                        CoreIrStackSlot *&stack_slot,
                                        std::vector<std::uint64_t> &path) {
    if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        stack_slot = address->get_stack_slot();
        return stack_slot != nullptr;
    }

    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return false;
    }

    std::vector<std::uint64_t> base_path;
    if (!normalize_constant_stack_slot_path(gep->get_base(), stack_slot, base_path) ||
        stack_slot == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep->get_index(index));
        if (constant_index == nullptr) {
            return false;
        }
        base_path.push_back(constant_index->get_value());
    }

    if (!base_path.empty() && base_path.front() == 0) {
        base_path.erase(base_path.begin());
    }
    path = std::move(base_path);
    return true;
}

bool trace_stack_slot_prefix(CoreIrValue *value, CoreIrStackSlot *&stack_slot,
                             std::vector<std::uint64_t> &path, bool &exact_path) {
    if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        stack_slot = address->get_stack_slot();
        exact_path = stack_slot != nullptr;
        return stack_slot != nullptr;
    }

    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return false;
    }

    std::vector<std::uint64_t> base_path;
    if (!trace_stack_slot_prefix(gep->get_base(), stack_slot, base_path, exact_path) ||
        stack_slot == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep->get_index(index));
        if (constant_index == nullptr) {
            exact_path = false;
            break;
        }
        base_path.push_back(constant_index->get_value());
    }

    if (!base_path.empty() && base_path.front() == 0) {
        base_path.erase(base_path.begin());
    }
    path = std::move(base_path);
    return true;
}

bool paths_overlap(const std::vector<std::uint64_t> &lhs,
                   const std::vector<std::uint64_t> &rhs) {
    const std::size_t shared = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < shared; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

bool is_supported_integer_cast_kind(CoreIrCastKind cast_kind) {
    return cast_kind == CoreIrCastKind::SignExtend ||
           cast_kind == CoreIrCastKind::ZeroExtend ||
           cast_kind == CoreIrCastKind::Truncate;
}

bool preserves_integer_truthiness(CoreIrCastKind cast_kind) {
    return cast_kind == CoreIrCastKind::SignExtend ||
           cast_kind == CoreIrCastKind::ZeroExtend;
}

bool is_pointer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Pointer;
}

bool is_float_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Float;
}

CoreIrInstruction *insert_instruction_before(
    CoreIrBasicBlock &block, CoreIrInstruction *anchor,
    std::unique_ptr<CoreIrInstruction> instruction) {
    if (instruction == nullptr) {
        return nullptr;
    }

    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [anchor](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == anchor;
        });
    instruction->set_parent(&block);
    CoreIrInstruction *instruction_ptr = instruction.get();
    instructions.insert(it, std::move(instruction));
    return instruction_ptr;
}

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

CoreIrInstruction *replace_instruction(CoreIrBasicBlock &block,
                                       CoreIrInstruction *instruction,
                                       std::unique_ptr<CoreIrInstruction> replacement) {
    if (instruction == nullptr || replacement == nullptr) {
        return nullptr;
    }

    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return nullptr;
    }

    replacement->set_parent(&block);
    CoreIrInstruction *replacement_ptr = replacement.get();
    instruction->replace_all_uses_with(replacement_ptr);
    instruction->detach_operands();
    *it = std::move(replacement);
    return replacement_ptr;
}

} // namespace sysycc::detail
