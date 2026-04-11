#include "backend/ir/effect/core_ir_memory_location.hpp"

#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool append_constant_indices(CoreIrMemoryLocation &location,
                             const CoreIrGetElementPtrInst &gep) {
    if (!location.exact_access_path) {
        return false;
    }
    for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep.get_index(index));
        if (constant_index == nullptr) {
            location.exact_access_path = false;
            return false;
        }
        location.access_path.push_back(constant_index->get_value());
    }
    if (!location.access_path.empty() && location.access_path.front() == 0) {
        location.access_path.erase(location.access_path.begin());
    }
    return true;
}

} // namespace

CoreIrMemoryLocation describe_memory_location(const CoreIrValue *value) {
    if (auto *address =
            dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        CoreIrMemoryLocation location;
        location.kind = CoreIrMemoryLocationKind::StackSlot;
        location.stack_slot = address->get_stack_slot();
        return location;
    }
    if (auto *address = dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
        address != nullptr) {
        CoreIrMemoryLocation location;
        location.kind = CoreIrMemoryLocationKind::Global;
        location.global = address->get_global();
        return location;
    }
    if (auto *parameter = dynamic_cast<const CoreIrParameter *>(value);
        parameter != nullptr) {
        CoreIrMemoryLocation location;
        if (parameter->get_type() == nullptr ||
            parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer ||
            parameter->get_parent() == nullptr) {
            return location;
        }
        location.kind = CoreIrMemoryLocationKind::ArgumentDerived;
        location.parameter = parameter;
        const auto &parameters = parameter->get_parent()->get_parameters();
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (parameters[index].get() == parameter) {
                location.parameter_index = index;
                break;
            }
        }
        return location;
    }

    auto *gep = dynamic_cast<const CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return {};
    }

    CoreIrMemoryLocation location = describe_memory_location(gep->get_base());
    if (location.is_unknown()) {
        return location;
    }
    append_constant_indices(location, *gep);
    return location;
}

} // namespace sysycc
