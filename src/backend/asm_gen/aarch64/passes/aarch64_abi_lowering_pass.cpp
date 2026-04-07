#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"

#include <algorithm>

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

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

std::size_t get_stack_abi_alignment(const CoreIrType *type) {
    return std::max<std::size_t>(8, std::min<std::size_t>(16, get_type_alignment(type)));
}

std::size_t get_stack_abi_size(const CoreIrType *type) {
    return align_to(get_type_size(type), 8);
}

} // namespace

std::optional<AArch64HfaInfo>
AArch64AbiLoweringPass::classify_hfa(const CoreIrType *type,
                                     std::size_t base_offset) const {
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        return AArch64HfaInfo{classify_float_reg_kind(float_type->get_float_kind()),
                              {base_offset}};
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        auto element_hfa = classify_hfa(array_type->get_element_type(), base_offset);
        if (!element_hfa.has_value()) {
            return std::nullopt;
        }
        AArch64HfaInfo result;
        result.element_kind = element_hfa->element_kind;
        const std::size_t stride = get_type_size(array_type->get_element_type());
        for (std::size_t index = 0; index < array_type->get_element_count(); ++index) {
            auto nested = classify_hfa(array_type->get_element_type(),
                                       base_offset + (index * stride));
            if (!nested.has_value() || nested->element_kind != result.element_kind) {
                return std::nullopt;
            }
            result.member_offsets.insert(result.member_offsets.end(),
                                         nested->member_offsets.begin(),
                                         nested->member_offsets.end());
        }
        if (result.member_offsets.empty() || result.member_offsets.size() > 4) {
            return std::nullopt;
        }
        return result;
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        AArch64HfaInfo result;
        bool initialized = false;
        std::size_t running_offset = 0;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            const std::size_t alignment = get_type_alignment(element_type);
            running_offset = align_to(running_offset, alignment);
            auto nested = classify_hfa(element_type, base_offset + running_offset);
            if (!nested.has_value()) {
                return std::nullopt;
            }
            if (!initialized) {
                result.element_kind = nested->element_kind;
                initialized = true;
            } else if (nested->element_kind != result.element_kind) {
                return std::nullopt;
            }
            result.member_offsets.insert(result.member_offsets.end(),
                                         nested->member_offsets.begin(),
                                         nested->member_offsets.end());
            running_offset += get_type_size(element_type);
        }
        if (!initialized || result.member_offsets.empty() ||
            result.member_offsets.size() > 4) {
            return std::nullopt;
        }
        return result;
    }
    return std::nullopt;
}

AArch64AbiAssignment
AArch64AbiLoweringPass::classify_return(const CoreIrType *type) const {
    AArch64AbiAssignment assignment;
    assignment.type = type;
    if (is_void_type(type)) {
        assignment.value_class = AArch64AbiValueClass::Void;
        return assignment;
    }
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        assignment.value_class = AArch64AbiValueClass::FloatingScalar;
        assignment.locations.push_back(AArch64AbiLocation{
            AArch64AbiLocationKind::FloatingRegister,
            static_cast<unsigned>(AArch64PhysicalReg::V0),
            0,
            0,
            get_storage_size(type),
            classify_float_reg_kind(float_type->get_float_kind())});
        return assignment;
    }
    if (is_supported_scalar_storage_type(type) && !is_aggregate_type(type)) {
        assignment.value_class = AArch64AbiValueClass::GeneralScalar;
        assignment.locations.push_back(AArch64AbiLocation{
            AArch64AbiLocationKind::GeneralRegister,
            static_cast<unsigned>(AArch64PhysicalReg::X0),
            0,
            0,
            get_storage_size(type),
            classify_virtual_reg_kind(type)});
        return assignment;
    }
    if (const auto hfa = classify_hfa(type); hfa.has_value()) {
        assignment.value_class = AArch64AbiValueClass::HfaComposite;
        for (std::size_t index = 0; index < hfa->member_offsets.size(); ++index) {
            assignment.locations.push_back(AArch64AbiLocation{
                AArch64AbiLocationKind::FloatingRegister,
                static_cast<unsigned>(AArch64PhysicalReg::V0) +
                    static_cast<unsigned>(index),
                0,
                hfa->member_offsets[index],
                virtual_reg_size(hfa->element_kind),
                hfa->element_kind});
        }
        return assignment;
    }
    if (is_aggregate_type(type) && get_type_size(type) <= 16) {
        assignment.value_class = AArch64AbiValueClass::GeneralComposite;
        std::size_t remaining = get_type_size(type);
        for (std::size_t index = 0; remaining > 0; ++index) {
            const std::size_t part_size = std::min<std::size_t>(8, remaining);
            assignment.locations.push_back(AArch64AbiLocation{
                AArch64AbiLocationKind::GeneralRegister,
                static_cast<unsigned>(AArch64PhysicalReg::X0) +
                    static_cast<unsigned>(index),
                0,
                index * 8,
                part_size,
                AArch64VirtualRegKind::General64});
            remaining -= part_size;
        }
        return assignment;
    }
    assignment.value_class = AArch64AbiValueClass::IndirectComposite;
    assignment.is_indirect = true;
    assignment.locations.push_back(AArch64AbiLocation{
        AArch64AbiLocationKind::IndirectResultRegister,
        static_cast<unsigned>(AArch64PhysicalReg::X8),
        0,
        0,
        8,
        AArch64VirtualRegKind::General64});
    return assignment;
}

AArch64AbiAssignment AArch64AbiLoweringPass::classify_parameter(
    const CoreIrType *type, unsigned &next_gpr, unsigned &next_fpr,
    std::size_t &next_stack_offset) const {
    AArch64AbiAssignment assignment;
    assignment.type = type;
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        assignment.value_class = AArch64AbiValueClass::FloatingScalar;
        const AArch64VirtualRegKind reg_kind =
            classify_float_reg_kind(float_type->get_float_kind());
        if (next_fpr < 8) {
            assignment.locations.push_back(AArch64AbiLocation{
                AArch64AbiLocationKind::FloatingRegister,
                static_cast<unsigned>(AArch64PhysicalReg::V0) + next_fpr,
                0,
                0,
                get_storage_size(type),
                reg_kind});
            ++next_fpr;
            return assignment;
        }
    } else if (is_supported_scalar_storage_type(type) && !is_aggregate_type(type)) {
        assignment.value_class = AArch64AbiValueClass::GeneralScalar;
        if (next_gpr < 8) {
            assignment.locations.push_back(AArch64AbiLocation{
                AArch64AbiLocationKind::GeneralRegister,
                static_cast<unsigned>(AArch64PhysicalReg::X0) + next_gpr,
                0,
                0,
                get_storage_size(type),
                classify_virtual_reg_kind(type)});
            ++next_gpr;
            return assignment;
        }
    } else if (const auto hfa = classify_hfa(type); hfa.has_value()) {
        assignment.value_class = AArch64AbiValueClass::HfaComposite;
        if (next_fpr + hfa->member_offsets.size() <= 8) {
            for (std::size_t index = 0; index < hfa->member_offsets.size(); ++index) {
                assignment.locations.push_back(AArch64AbiLocation{
                    AArch64AbiLocationKind::FloatingRegister,
                    static_cast<unsigned>(AArch64PhysicalReg::V0) + next_fpr +
                        static_cast<unsigned>(index),
                    0,
                    hfa->member_offsets[index],
                    virtual_reg_size(hfa->element_kind),
                    hfa->element_kind});
            }
            next_fpr += static_cast<unsigned>(hfa->member_offsets.size());
            return assignment;
        }
    } else if (is_aggregate_type(type) && get_type_size(type) <= 16) {
        assignment.value_class = AArch64AbiValueClass::GeneralComposite;
        const std::size_t chunk_count = align_to(get_type_size(type), 8) / 8;
        if (next_gpr + chunk_count <= 8) {
            std::size_t remaining = get_type_size(type);
            for (std::size_t index = 0; index < chunk_count; ++index) {
                const std::size_t part_size = std::min<std::size_t>(8, remaining);
                assignment.locations.push_back(AArch64AbiLocation{
                    AArch64AbiLocationKind::GeneralRegister,
                    static_cast<unsigned>(AArch64PhysicalReg::X0) + next_gpr +
                        static_cast<unsigned>(index),
                    0,
                    index * 8,
                    part_size,
                    AArch64VirtualRegKind::General64});
                remaining = remaining > part_size ? remaining - part_size : 0;
            }
            next_gpr += static_cast<unsigned>(chunk_count);
            return assignment;
        }
    } else if (is_aggregate_type(type)) {
        assignment.value_class = AArch64AbiValueClass::IndirectComposite;
        assignment.is_indirect = true;
        if (next_gpr < 8) {
            assignment.locations.push_back(AArch64AbiLocation{
                AArch64AbiLocationKind::GeneralRegister,
                static_cast<unsigned>(AArch64PhysicalReg::X0) + next_gpr,
                0,
                0,
                8,
                AArch64VirtualRegKind::General64});
            ++next_gpr;
            return assignment;
        }
    }

    const std::size_t stack_alignment = get_stack_abi_alignment(type);
    next_stack_offset = align_to(next_stack_offset, stack_alignment);
    assignment.stack_size = get_stack_abi_size(type);
    assignment.locations.push_back(AArch64AbiLocation{
        AArch64AbiLocationKind::Stack,
        0,
        next_stack_offset,
        0,
        get_type_size(type),
        classify_virtual_reg_kind(type)});
    next_stack_offset += assignment.stack_size;
    return assignment;
}

AArch64FunctionAbiInfo
AArch64AbiLoweringPass::classify_function(const CoreIrFunction &function) const {
    AArch64FunctionAbiInfo abi_info;
    abi_info.return_value =
        classify_return(function.get_function_type()->get_return_type());

    unsigned next_gpr = 0;
    unsigned next_fpr = 0;
    std::size_t next_stack_offset = 16;
    for (const auto &parameter : function.get_parameters()) {
        abi_info.parameters.push_back(classify_parameter(
            parameter->get_type(), next_gpr, next_fpr, next_stack_offset));
    }
    return abi_info;
}

AArch64FunctionAbiInfo
AArch64AbiLoweringPass::classify_call(const CoreIrCallInst &call) const {
    AArch64FunctionAbiInfo abi_info;
    const CoreIrFunctionType *callee_type = call.get_callee_type();
    abi_info.return_value = classify_return(call.get_type());
    if (callee_type == nullptr) {
        return abi_info;
    }

    unsigned next_gpr = 0;
    unsigned next_fpr = 0;
    std::size_t next_stack_offset = 0;
    const auto &arguments = call.get_operands();
    for (std::size_t index = call.get_argument_begin_index(); index < arguments.size();
         ++index) {
        abi_info.parameters.push_back(classify_parameter(
            arguments[index]->get_type(), next_gpr, next_fpr, next_stack_offset));
    }
    return abi_info;
}

} // namespace sysycc
