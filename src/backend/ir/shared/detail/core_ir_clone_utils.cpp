#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"

#include <vector>

#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc::detail {

std::unique_ptr<CoreIrInstruction> clone_instruction_remapped(
    const CoreIrInstruction &instruction,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map,
    const std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
        *block_map) {
    auto remap = [&value_map](CoreIrValue *value) -> CoreIrValue * {
        auto it = value_map.find(value);
        return it == value_map.end() ? value : it->second;
    };
    auto remap_block =
        [block_map](CoreIrBasicBlock *block) -> CoreIrBasicBlock * {
        if (block_map == nullptr || block == nullptr) {
            return block;
        }
        auto it = block_map->find(block);
        return it == block_map->end() ? block : it->second;
    };

    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary: {
        const auto &binary = static_cast<const CoreIrBinaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrBinaryInst>(
            binary.get_binary_opcode(), binary.get_type(), binary.get_name(),
            remap(binary.get_lhs()), remap(binary.get_rhs()));
        clone->set_source_span(binary.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Unary: {
        const auto &unary = static_cast<const CoreIrUnaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrUnaryInst>(
            unary.get_unary_opcode(), unary.get_type(), unary.get_name(),
            remap(unary.get_operand()));
        clone->set_source_span(unary.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Compare: {
        const auto &compare =
            static_cast<const CoreIrCompareInst &>(instruction);
        auto clone = std::make_unique<CoreIrCompareInst>(
            compare.get_predicate(), compare.get_type(), compare.get_name(),
            remap(compare.get_lhs()), remap(compare.get_rhs()));
        clone->set_source_span(compare.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Select: {
        const auto &select = static_cast<const CoreIrSelectInst &>(instruction);
        auto clone = std::make_unique<CoreIrSelectInst>(
            select.get_type(), select.get_name(), remap(select.get_condition()),
            remap(select.get_true_value()), remap(select.get_false_value()));
        clone->set_source_span(select.get_source_span());
        return clone;
    }
    case CoreIrOpcode::ExtractElement: {
        const auto &extract =
            static_cast<const CoreIrExtractElementInst &>(instruction);
        auto clone = std::make_unique<CoreIrExtractElementInst>(
            extract.get_type(), extract.get_name(), remap(extract.get_vector_value()),
            remap(extract.get_index()));
        clone->set_source_span(extract.get_source_span());
        return clone;
    }
    case CoreIrOpcode::InsertElement: {
        const auto &insert =
            static_cast<const CoreIrInsertElementInst &>(instruction);
        auto clone = std::make_unique<CoreIrInsertElementInst>(
            insert.get_type(), insert.get_name(), remap(insert.get_vector_value()),
            remap(insert.get_element_value()), remap(insert.get_index()));
        clone->set_source_span(insert.get_source_span());
        return clone;
    }
    case CoreIrOpcode::ShuffleVector: {
        const auto &shuffle =
            static_cast<const CoreIrShuffleVectorInst &>(instruction);
        std::vector<CoreIrValue *> mask_values;
        mask_values.reserve(shuffle.get_mask_count());
        for (std::size_t index = 0; index < shuffle.get_mask_count(); ++index) {
            mask_values.push_back(remap(shuffle.get_mask_value(index)));
        }
        auto clone = std::make_unique<CoreIrShuffleVectorInst>(
            shuffle.get_type(), shuffle.get_name(), remap(shuffle.get_lhs()),
            remap(shuffle.get_rhs()), std::move(mask_values));
        clone->set_source_span(shuffle.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Cast: {
        const auto &cast = static_cast<const CoreIrCastInst &>(instruction);
        auto clone = std::make_unique<CoreIrCastInst>(
            cast.get_cast_kind(), cast.get_type(), cast.get_name(),
            remap(cast.get_operand()));
        clone->set_source_span(cast.get_source_span());
        return clone;
    }
    case CoreIrOpcode::VectorReduceAdd: {
        const auto &reduce =
            static_cast<const CoreIrVectorReduceAddInst &>(instruction);
        auto clone = std::make_unique<CoreIrVectorReduceAddInst>(
            reduce.get_type(), reduce.get_name(), remap(reduce.get_vector_value()));
        clone->set_source_span(reduce.get_source_span());
        return clone;
    }
    case CoreIrOpcode::AddressOfFunction: {
        const auto &address =
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfFunctionInst>(
            address.get_type(), address.get_name(), address.get_function());
        clone->set_source_span(address.get_source_span());
        return clone;
    }
    case CoreIrOpcode::AddressOfGlobal: {
        const auto &address =
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfGlobalInst>(
            address.get_type(), address.get_name(), address.get_global());
        clone->set_source_span(address.get_source_span());
        return clone;
    }
    case CoreIrOpcode::AddressOfStackSlot: {
        const auto &address =
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfStackSlotInst>(
            address.get_type(), address.get_name(), address.get_stack_slot());
        clone->set_source_span(address.get_source_span());
        return clone;
    }
    case CoreIrOpcode::GetElementPtr: {
        const auto &gep =
            static_cast<const CoreIrGetElementPtrInst &>(instruction);
        std::vector<CoreIrValue *> indices;
        indices.reserve(gep.get_index_count());
        for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
            indices.push_back(remap(gep.get_index(index)));
        }
        auto clone = std::make_unique<CoreIrGetElementPtrInst>(
            gep.get_type(), gep.get_name(), remap(gep.get_base()),
            std::move(indices));
        clone->set_source_span(gep.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Load: {
        const auto &load = static_cast<const CoreIrLoadInst &>(instruction);
        std::unique_ptr<CoreIrLoadInst> clone;
        if (load.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), load.get_stack_slot(),
                load.get_alignment());
        } else {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), remap(load.get_address()),
                load.get_alignment());
        }
        clone->set_source_span(load.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Store: {
        const auto &store = static_cast<const CoreIrStoreInst &>(instruction);
        std::unique_ptr<CoreIrStoreInst> clone;
        if (store.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrStoreInst>(store.get_type(),
                                                      remap(store.get_value()),
                                                      store.get_stack_slot(),
                                                      store.get_alignment());
        } else {
            clone = std::make_unique<CoreIrStoreInst>(
                store.get_type(), remap(store.get_value()),
                remap(store.get_address()), store.get_alignment());
        }
        clone->set_source_span(store.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Call: {
        const auto &call = static_cast<const CoreIrCallInst &>(instruction);
        std::vector<CoreIrValue *> arguments;
        arguments.reserve(call.get_argument_count());
        for (std::size_t index = 0; index < call.get_argument_count();
             ++index) {
            arguments.push_back(remap(call.get_argument(index)));
        }
        std::unique_ptr<CoreIrCallInst> clone;
        if (call.get_is_direct_call()) {
            clone = std::make_unique<CoreIrCallInst>(
                call.get_type(), call.get_name(), call.get_callee_name(),
                call.get_callee_type(), std::move(arguments));
        } else {
            clone = std::make_unique<CoreIrCallInst>(
                call.get_type(), call.get_name(),
                remap(call.get_callee_value()), call.get_callee_type(),
                std::move(arguments));
        }
        clone->set_source_span(call.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Jump: {
        const auto &jump = static_cast<const CoreIrJumpInst &>(instruction);
        auto clone = std::make_unique<CoreIrJumpInst>(
            jump.get_type(), remap_block(jump.get_target_block()));
        clone->set_source_span(jump.get_source_span());
        return clone;
    }
    case CoreIrOpcode::CondJump: {
        const auto &jump = static_cast<const CoreIrCondJumpInst &>(instruction);
        auto clone = std::make_unique<CoreIrCondJumpInst>(
            jump.get_type(), remap(jump.get_condition()),
            remap_block(jump.get_true_block()),
            remap_block(jump.get_false_block()));
        clone->set_source_span(jump.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Return: {
        const auto &ret = static_cast<const CoreIrReturnInst &>(instruction);
        std::unique_ptr<CoreIrReturnInst> clone;
        if (ret.get_return_value() == nullptr) {
            clone = std::make_unique<CoreIrReturnInst>(ret.get_type());
        } else {
            clone = std::make_unique<CoreIrReturnInst>(
                ret.get_type(), remap(ret.get_return_value()));
        }
        clone->set_source_span(ret.get_source_span());
        return clone;
    }
    case CoreIrOpcode::Phi:
        break;
    }

    return nullptr;
}

} // namespace sysycc::detail
