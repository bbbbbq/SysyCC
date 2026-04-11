#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"

#include <limits>
#include <optional>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

struct AArch64IntegerConstantValue {
    std::uint64_t unsigned_value = 0;
    std::uint64_t magnitude = 0;
    bool is_negative = false;
};

std::optional<AArch64IntegerConstantValue>
get_integer_constant_value(const CoreIrConstant *constant) {
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

    const std::uint64_t mask =
        bit_width == 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
    const std::uint64_t masked = int_constant->get_value() & mask;
    AArch64IntegerConstantValue result;
    result.unsigned_value = masked;

    if (!integer_type->get_is_signed()) {
        result.magnitude = masked;
        return result;
    }

    const std::uint64_t sign_bit = 1ULL << (bit_width - 1);
    if ((masked & sign_bit) == 0) {
        result.magnitude = masked;
        return result;
    }

    result.is_negative = true;
    result.magnitude = (~masked + 1ULL) & mask;
    return result;
}

std::optional<AArch64IntegerConstantValue>
scale_integer_constant_value(const AArch64IntegerConstantValue &value,
                             std::size_t scale) {
    const unsigned __int128 scaled =
        static_cast<unsigned __int128>(value.magnitude) * scale;
    if (scaled > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return AArch64IntegerConstantValue{
        .unsigned_value = static_cast<std::uint64_t>(scaled),
        .magnitude = static_cast<std::uint64_t>(scaled),
        .is_negative = value.is_negative};
}

AArch64MachineOperand memory_operand(const AArch64VirtualReg &base_reg) {
    return AArch64MachineOperand::memory_address_virtual_reg(base_reg);
}

AArch64MachineOperand memory_operand(const AArch64VirtualReg &base_reg,
                                     AArch64MachineSymbolReference symbolic_offset) {
    return AArch64MachineOperand::memory_address_virtual_reg(
        base_reg, std::move(symbolic_offset));
}

bool materialize_index_as_pointer_offset(
    AArch64MachineBlock &machine_block, AArch64AddressMaterializationContext &context,
    const CoreIrValue *index_value, AArch64VirtualReg &out,
    AArch64MachineFunction &function) {
    AArch64VirtualReg index_reg;
    if (!context.ensure_value_in_vreg(machine_block, index_value, index_reg)) {
        return false;
    }

    out = context.create_pointer_virtual_reg(function);
    if (const auto *index_integer_type = as_integer_type(index_value->get_type());
        index_integer_type != nullptr) {
        const unsigned bit_width = index_integer_type->get_bit_width();
        if (bit_width == 64) {
            machine_block.append_instruction(
                AArch64MachineInstr("mov", {def_vreg_operand(out),
                                            use_vreg_operand_as(index_reg, true)}));
            return true;
        }
        if (bit_width > 64) {
            context.report_error("unsupported integer index width in AArch64 native backend");
            return false;
        }
        machine_block.append_instruction(
            AArch64MachineInstr("mov", {def_vreg_operand_as(out, false),
                                        use_vreg_operand_as(index_reg, false)}));
        if (index_integer_type->get_is_signed()) {
            context.apply_sign_extend_to_virtual_reg(machine_block, out,
                                                     index_value->get_type(),
                                                     context.create_fake_pointer_type());
        } else {
            context.apply_zero_extend_to_virtual_reg(machine_block, out,
                                                     index_value->get_type(),
                                                     context.create_fake_pointer_type());
        }
        return true;
    }

    if (is_pointer_type(index_value->get_type())) {
        machine_block.append_instruction(
            AArch64MachineInstr("mov", {def_vreg_operand(out),
                                        use_vreg_operand_as(index_reg, true)}));
        return true;
    }

    context.report_error("unsupported dynamic index type in AArch64 native backend");
    return false;
}

bool add_scaled_index(AArch64MachineBlock &machine_block,
                      AArch64AddressMaterializationContext &context,
                      const AArch64VirtualReg &base_reg,
                      const CoreIrValue *index_value, std::size_t scale,
                      AArch64MachineFunction &function) {
    AArch64VirtualReg index_reg;
    if (!materialize_index_as_pointer_offset(machine_block, context, index_value,
                                             index_reg, function)) {
        return false;
    }

    if (scale == 1) {
        machine_block.append_instruction(AArch64MachineInstr(
            "add", {def_vreg_operand(base_reg), use_vreg_operand(base_reg),
                    use_vreg_operand(index_reg)}));
        return true;
    }

    const bool power_of_two = scale != 0 && (scale & (scale - 1)) == 0;
    if (power_of_two) {
        std::size_t shift = 0;
        std::size_t remaining = scale;
        while (remaining > 1) {
            remaining >>= 1U;
            ++shift;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "add", {def_vreg_operand(base_reg), use_vreg_operand(base_reg),
                    use_vreg_operand(index_reg), shift_operand("lsl", shift)}));
        return true;
    }

    const AArch64VirtualReg scale_reg = context.create_pointer_virtual_reg(function);
    if (!context.materialize_integer_constant(machine_block,
                                              context.create_fake_pointer_type(),
                                              static_cast<std::uint64_t>(scale),
                                              scale_reg, function)) {
        return false;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "mul", {def_vreg_operand(index_reg), use_vreg_operand(index_reg),
                use_vreg_operand(scale_reg)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand(base_reg), use_vreg_operand(base_reg),
                use_vreg_operand(index_reg)}));
    return true;
}

bool apply_constant_gep_index(AArch64MachineBlock &machine_block,
                              AArch64AddressMaterializationContext &context,
                              const AArch64VirtualReg &target_reg,
                              const AArch64IntegerConstantValue &index,
                              const CoreIrType *current_type,
                              std::size_t index_position,
                              const CoreIrType *&next_type,
                              AArch64MachineFunction &function) {
    if (index_position == 0) {
        const auto scaled =
            scale_integer_constant_value(index, get_type_size(current_type));
        if (!scaled.has_value()) {
            return false;
        }
        return context.add_constant_offset(machine_block, target_reg,
                                           scaled->is_negative
                                               ? -static_cast<long long>(scaled->magnitude)
                                               : static_cast<long long>(scaled->magnitude),
                                           function);
    }

    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
        array_type != nullptr) {
        const auto scaled = scale_integer_constant_value(
            index, get_type_size(array_type->get_element_type()));
        if (!scaled.has_value() ||
            !context.add_constant_offset(machine_block, target_reg,
                                         scaled->is_negative
                                             ? -static_cast<long long>(scaled->magnitude)
                                             : static_cast<long long>(scaled->magnitude),
                                         function)) {
            return false;
        }
        next_type = array_type->get_element_type();
        return true;
    }

    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(current_type);
        struct_type != nullptr) {
        if (index.is_negative ||
            static_cast<std::size_t>(index.unsigned_value) >=
                struct_type->get_element_types().size()) {
            context.report_error("unsupported gep struct index in AArch64 native backend");
            return false;
        }
        if (!context.add_constant_offset(
                machine_block, target_reg,
                static_cast<long long>(get_struct_member_offset(
                    struct_type, static_cast<std::size_t>(index.unsigned_value))),
                function)) {
            return false;
        }
        next_type =
            struct_type->get_element_types()[static_cast<std::size_t>(index.unsigned_value)];
        return true;
    }

    if (const auto *pointer_type = dynamic_cast<const CoreIrPointerType *>(current_type);
        pointer_type != nullptr) {
        const auto scaled = scale_integer_constant_value(
            index, get_type_size(pointer_type->get_pointee_type()));
        if (!scaled.has_value() ||
            !context.add_constant_offset(machine_block, target_reg,
                                         scaled->is_negative
                                             ? -static_cast<long long>(scaled->magnitude)
                                             : static_cast<long long>(scaled->magnitude),
                                         function)) {
            return false;
        }
        next_type = pointer_type->get_pointee_type();
        return true;
    }

    context.report_error(
        "invalid constant gep index into non-aggregate type in the AArch64 "
        "native backend");
    return false;
}

bool apply_dynamic_gep_index(AArch64MachineBlock &machine_block,
                             AArch64AddressMaterializationContext &context,
                             const AArch64VirtualReg &target_reg,
                             const CoreIrValue *index_value,
                             const CoreIrType *current_type,
                             std::size_t index_position,
                             const CoreIrType *&next_type,
                             AArch64MachineFunction &function) {
    if (index_position == 0) {
        return add_scaled_index(machine_block, context, target_reg, index_value,
                                get_type_size(current_type), function);
    }

    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
        array_type != nullptr) {
        if (!add_scaled_index(machine_block, context, target_reg, index_value,
                              get_type_size(array_type->get_element_type()),
                              function)) {
            return false;
        }
        next_type = array_type->get_element_type();
        return true;
    }

    if (const auto *pointer_type = dynamic_cast<const CoreIrPointerType *>(current_type);
        pointer_type != nullptr) {
        if (!add_scaled_index(machine_block, context, target_reg, index_value,
                              get_type_size(pointer_type->get_pointee_type()),
                              function)) {
            return false;
        }
        next_type = pointer_type->get_pointee_type();
        return true;
    }

    if (dynamic_cast<const CoreIrStructType *>(current_type) != nullptr) {
        context.report_error("dynamic struct index is not supported in the "
                             "AArch64 native backend");
        return false;
    }

    context.report_error(
        "invalid dynamic gep index into non-aggregate type in the AArch64 "
        "native backend");
    return false;
}

} // namespace

bool materialize_global_address(AArch64MachineBlock &machine_block,
                                AArch64AddressMaterializationContext &context,
                                const std::string &symbol_name,
                                const AArch64VirtualReg &target_reg,
                                AArch64SymbolKind symbol_kind) {
    context.record_symbol_reference(symbol_name, symbol_kind);
    const bool is_nonpreemptible =
        symbol_kind == AArch64SymbolKind::Function
            ? context.is_nonpreemptible_function_symbol(symbol_name)
            : context.is_nonpreemptible_global_symbol(symbol_name);
    const AArch64SymbolReference symbol_reference = context.make_symbol_reference(
        symbol_name, symbol_kind,
        is_nonpreemptible ? AArch64SymbolBinding::Local
                          : AArch64SymbolBinding::Global);
    if (context.is_position_independent() && !is_nonpreemptible) {
        machine_block.append_instruction(AArch64MachineInstr(
            "adrp", {def_vreg_operand(target_reg),
                     AArch64MachineOperand::symbol(
                         AArch64MachineSymbolReference::got(symbol_reference))}));
        machine_block.append_instruction(AArch64MachineInstr(
            "ldr", {def_vreg_operand(target_reg),
                    memory_operand(target_reg,
                                   AArch64MachineSymbolReference::got_lo12(
                                       symbol_reference))}));
        return true;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "adrp", {def_vreg_operand(target_reg),
                 AArch64MachineOperand::symbol(symbol_reference)}));
    machine_block.append_instruction(AArch64MachineInstr(
        "add", {def_vreg_operand(target_reg), use_vreg_operand(target_reg),
                AArch64MachineOperand::symbol(
                    AArch64MachineSymbolReference::lo12(symbol_reference))}));
    return true;
}

bool materialize_gep_value(
    AArch64MachineBlock &machine_block, AArch64AddressMaterializationContext &context,
    const CoreIrValue *base_value, const CoreIrType *base_type,
    std::size_t index_count,
    const std::function<CoreIrValue *(std::size_t)> &get_index_value,
    const AArch64VirtualReg &target_reg, AArch64MachineFunction &function) {
    AArch64VirtualReg base_reg;
    if (!context.ensure_value_in_vreg(machine_block, base_value, base_reg)) {
        return false;
    }
    if (base_reg.get_id() != target_reg.get_id()) {
        append_register_copy(machine_block, target_reg, base_reg);
    }

    const CoreIrType *current_type = base_type;
    for (std::size_t index_position = 0; index_position < index_count;
         ++index_position) {
        CoreIrValue *index_value = get_index_value(index_position);
        if (index_value == nullptr) {
            context.report_error("encountered null gep index in AArch64 native backend");
            return false;
        }

        if (const auto *index_constant = dynamic_cast<const CoreIrConstantInt *>(index_value);
            index_constant != nullptr) {
            const std::optional<AArch64IntegerConstantValue> index =
                get_integer_constant_value(index_constant);
            if (!index.has_value()) {
                return false;
            }
            const CoreIrType *next_type = current_type;
            if (!apply_constant_gep_index(machine_block, context, target_reg, *index,
                                          current_type, index_position, next_type,
                                          function)) {
                return false;
            }
            current_type = next_type;
            continue;
        }

        const CoreIrType *next_type = current_type;
        if (!apply_dynamic_gep_index(machine_block, context, target_reg, index_value,
                                     current_type, index_position, next_type,
                                     function)) {
            return false;
        }
        current_type = next_type;
    }

    return true;
}

} // namespace sysycc
