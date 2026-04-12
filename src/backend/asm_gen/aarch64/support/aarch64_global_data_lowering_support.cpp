#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_support.hpp"

#include <cstdint>
#include <cstring>
#include <optional>

#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

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

std::size_t scalar_size(const CoreIrType *type) {
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return 8;
    }
    if (get_storage_size(type) == 2) {
        return 2;
    }
    if (get_storage_size(type) == 1) {
        return 1;
    }
    return 4;
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
    const std::uint64_t sign_bit = static_cast<std::uint64_t>(1) << (bit_width - 1);
    const std::uint64_t mask =
        bit_width == 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
    const std::uint64_t masked = value & mask;
    if ((masked & sign_bit) == 0) {
        return static_cast<long long>(masked);
    }
    return static_cast<long long>(masked | ~mask);
}

std::optional<long long>
compute_constant_gep_offset(const CoreIrType *base_type,
                            const std::vector<const CoreIrConstant *> &indices) {
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
            offset += *maybe_index * static_cast<long long>(
                                         get_type_size(array_type->get_element_type()));
            current_type = array_type->get_element_type();
            continue;
        }

        if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(current_type);
            struct_type != nullptr) {
            if (*maybe_index < 0 ||
                static_cast<std::size_t>(*maybe_index) >=
                    struct_type->get_element_types().size()) {
                return std::nullopt;
            }
            offset += static_cast<long long>(get_struct_member_offset(
                struct_type, static_cast<std::size_t>(*maybe_index)));
            current_type =
                struct_type->get_element_types()[static_cast<std::size_t>(*maybe_index)];
            continue;
        }

        if (const auto *pointer_type = dynamic_cast<const CoreIrPointerType *>(current_type);
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

bool split_constant_address(const CoreIrConstant *constant, std::string &symbol_name,
                            long long &offset) {
    if (const auto *global_address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
        global_address != nullptr) {
        if (global_address->get_global() != nullptr) {
            symbol_name = global_address->get_global()->get_name();
            offset = 0;
            return true;
        }
        if (global_address->get_function() != nullptr) {
            symbol_name = global_address->get_function()->get_name();
            offset = 0;
            return true;
        }
        return false;
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
    if (const auto *cast_constant = dynamic_cast<const CoreIrConstantCast *>(constant);
        cast_constant != nullptr) {
        if (cast_constant->get_cast_kind() == CoreIrCastKind::PtrToInt) {
            return split_constant_address(cast_constant->get_operand(), symbol_name,
                                          offset);
        }
        if (cast_constant->get_cast_kind() == CoreIrCastKind::IntToPtr) {
            const auto *nested_cast =
                dynamic_cast<const CoreIrConstantCast *>(cast_constant->get_operand());
            if (nested_cast != nullptr &&
                nested_cast->get_cast_kind() == CoreIrCastKind::PtrToInt) {
                return split_constant_address(nested_cast->get_operand(), symbol_name,
                                              offset);
            }
        }
    }
    return false;
}

std::size_t alignment_to_log2(std::size_t alignment) {
    if (alignment <= 1) {
        return 0;
    }
    std::size_t log2 = 0;
    while ((static_cast<std::size_t>(1) << log2) < alignment) {
        ++log2;
    }
    return log2;
}

void append_zero_fill_fragment(AArch64DataObject &data_object, std::size_t size) {
    data_object.append_fragment(AArch64DataFragment::zero_fill(size));
}

void append_byte_sequence_fragment(AArch64DataObject &data_object,
                                   std::vector<std::uint8_t> bytes) {
    data_object.append_fragment(
        AArch64DataFragment::byte_sequence(std::move(bytes)));
}

void append_scalar_fragment(AArch64DataObject &data_object, std::size_t size,
                            std::uint64_t value,
                            std::vector<AArch64RelocationRecord> relocations = {}) {
    switch (size) {
    case 1:
        data_object.append_fragment(AArch64DataFragment::scalar_byte(
            value, std::move(relocations)));
        return;
    case 2:
        data_object.append_fragment(AArch64DataFragment::scalar_halfword(
            value, std::move(relocations)));
        return;
    case 4:
        data_object.append_fragment(AArch64DataFragment::scalar_word(
            value, std::move(relocations)));
        return;
    case 8:
        data_object.append_fragment(AArch64DataFragment::scalar_xword(
            value, std::move(relocations)));
        return;
    default:
        append_zero_fill_fragment(data_object, size);
        return;
    }
}

} // namespace

bool append_global_constant_fragments(AArch64DataObject &data_object,
                                      const CoreIrConstant *constant,
                                      const CoreIrType *type,
                                      AArch64GlobalDataLoweringContext &context) {
    if (constant == nullptr) {
        return false;
    }

    if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
        append_zero_fill_fragment(data_object, get_type_size(type));
        return true;
    }
    if (const auto *int_constant = dynamic_cast<const CoreIrConstantInt *>(constant);
        int_constant != nullptr) {
        append_scalar_fragment(data_object, scalar_size(type), int_constant->get_value());
        return true;
    }
    if (const auto *float_constant = dynamic_cast<const CoreIrConstantFloat *>(constant);
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
                append_scalar_fragment(
                    data_object, 2,
                    static_cast<std::uint64_t>(float32_to_float16_bits(parsed)));
                return true;
            }
            case CoreIrFloatKind::Float32: {
                const float parsed = std::stof(literal_text);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &parsed, sizeof(bits));
                append_scalar_fragment(data_object, 4, bits);
                return true;
            }
            case CoreIrFloatKind::Float64: {
                const double parsed = std::stod(literal_text);
                std::uint64_t bits = 0;
                std::memcpy(&bits, &parsed, sizeof(bits));
                append_scalar_fragment(data_object, 8, bits);
                return true;
            }
            case CoreIrFloatKind::Float128:
                if (floating_literal_is_zero(literal_text)) {
                    append_zero_fill_fragment(data_object, 16);
                    return true;
                }
                context.report_error(
                    "non-zero float128 global initializers are not yet supported by the "
                    "AArch64 native backend");
                return false;
            }
        } catch (...) {
            context.report_error(
                "failed to parse floating literal for AArch64 global constant emission");
            return false;
        }
    }
    if (dynamic_cast<const CoreIrConstantNull *>(constant) != nullptr) {
        append_scalar_fragment(data_object, scalar_size(type), 0);
        return true;
    }
    if (const auto *cast_constant = dynamic_cast<const CoreIrConstantCast *>(constant);
        cast_constant != nullptr) {
        if (cast_constant->get_cast_kind() == CoreIrCastKind::IntToPtr) {
            return append_global_constant_fragments(data_object,
                                                   cast_constant->get_operand(), type,
                                                   context);
        }
        if (cast_constant->get_cast_kind() == CoreIrCastKind::PtrToInt &&
            (dynamic_cast<const CoreIrConstantNull *>(cast_constant->get_operand()) !=
                 nullptr ||
             dynamic_cast<const CoreIrConstantZeroInitializer *>(
                 cast_constant->get_operand()) != nullptr)) {
            append_scalar_fragment(data_object, scalar_size(type), 0);
            return true;
        }
    }
    if (const auto *byte_string = dynamic_cast<const CoreIrConstantByteString *>(constant);
        byte_string != nullptr) {
        std::vector<std::uint8_t> bytes(byte_string->get_bytes().begin(),
                                        byte_string->get_bytes().end());
        append_byte_sequence_fragment(data_object, std::move(bytes));
        return true;
    }
    if (const auto *aggregate = dynamic_cast<const CoreIrConstantAggregate *>(constant);
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
                append_zero_fill_fragment(data_object, element_offset - emitted_size);
                emitted_size = element_offset;
            }
            if (!append_global_constant_fragments(data_object,
                                                 aggregate->get_elements()[index],
                                                 element_type, context)) {
                return false;
            }
            emitted_size = std::max(emitted_size,
                                    element_offset + get_type_size(element_type));
        }
        if (get_type_size(type) > emitted_size) {
            append_zero_fill_fragment(data_object,
                                      get_type_size(type) - emitted_size);
        }
        return true;
    }
    std::string symbol_name;
    long long offset = 0;
    if (split_constant_address(constant, symbol_name, offset)) {
        context.record_symbol_reference(symbol_name, AArch64SymbolKind::Object);
        append_scalar_fragment(
            data_object, scalar_size(type), 0,
            {AArch64RelocationRecord{
                get_storage_size(type) <= 4 ? AArch64RelocationKind::Absolute32
                                            : AArch64RelocationKind::Absolute64,
                context.make_symbol_reference(
                    symbol_name, AArch64SymbolKind::Object,
                    AArch64SymbolBinding::Unknown, std::nullopt, offset),
                0,
            }});
        return true;
    }

    return false;
}

bool append_global(AArch64ObjectModule &object_module, const CoreIrGlobal &global,
                   AArch64GlobalDataLoweringContext &context) {
    if (global.get_initializer() == nullptr) {
        if (global.get_is_internal_linkage()) {
            context.report_error(
                "AArch64 native backend requires an initializer for internal global '" +
                global.get_name() + "'");
            return false;
        }
        context.record_symbol_reference(global.get_name(), AArch64SymbolKind::Object);
        return true;
    }
    if (is_byte_string_global(global)) {
        context.record_symbol_definition(global.get_name(), AArch64SymbolKind::Object,
                                         AArch64SectionKind::ReadOnlyData,
                                         !global.get_is_internal_linkage());
        AArch64DataObject &data_object = object_module.append_data_object(
            AArch64SectionKind::ReadOnlyData, global.get_name(),
            !global.get_is_internal_linkage(), 0);
        const auto *byte_string =
            static_cast<const CoreIrConstantByteString *>(global.get_initializer());
        std::vector<std::uint8_t> bytes(byte_string->get_bytes().begin(),
                                        byte_string->get_bytes().end());
        append_byte_sequence_fragment(data_object, std::move(bytes));
        return true;
    }

    if (!is_supported_object_type(global.get_type())) {
        context.report_error("unsupported global type in AArch64 native backend for '" +
                             global.get_name() + "'");
        return false;
    }

    const AArch64SectionKind section_kind =
        global.get_is_constant() ? AArch64SectionKind::ReadOnlyData
                                 : AArch64SectionKind::Data;
    context.record_symbol_definition(global.get_name(), AArch64SymbolKind::Object,
                                     section_kind, !global.get_is_internal_linkage());
    AArch64DataObject &data_object = object_module.append_data_object(
        section_kind, global.get_name(), !global.get_is_internal_linkage(),
        alignment_to_log2(get_type_alignment(global.get_type())));
    if (!append_global_constant_fragments(data_object, global.get_initializer(),
                                          global.get_type(), context)) {
        context.report_error(
            "unsupported global initializer in AArch64 native backend for '" +
            global.get_name() + "'");
        return false;
    }
    return true;
}

bool append_globals(AArch64ObjectModule &object_module, const CoreIrModule &module,
                    AArch64GlobalDataLoweringContext &context) {
    for (const auto &global : module.get_globals()) {
        if (!append_global(object_module, *global, context)) {
            return false;
        }
    }
    return true;
}

} // namespace sysycc
