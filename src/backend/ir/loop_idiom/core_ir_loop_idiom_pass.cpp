#include "backend/ir/loop_idiom/core_ir_loop_idiom_pass.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::insert_instruction_before;
using sysycc::detail::clone_instruction_remapped;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

bool value_is_loop_invariant(CoreIrValue *value, const CoreIrLoopInfo &loop) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    return !loop_contains_block(loop, instruction->get_parent());
}

bool instruction_is_zero_fill_rematerializable(const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
        return true;
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

bool value_is_recursively_loop_invariant(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    std::unordered_set<const CoreIrInstruction *> &visiting) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    if (!loop_contains_block(loop, instruction->get_parent())) {
        return true;
    }
    if (!instruction_is_zero_fill_rematerializable(*instruction) ||
        !visiting.insert(instruction).second) {
        return false;
    }
    for (CoreIrValue *operand : instruction->get_operands()) {
        if (!value_is_recursively_loop_invariant(operand, loop, visiting)) {
            visiting.erase(instruction);
            return false;
        }
    }
    visiting.erase(instruction);
    return true;
}

bool collect_loop_local_pure_subtree(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    const CoreIrInstruction *allowed_loop_leaf,
    std::unordered_set<const CoreIrInstruction *> &visiting,
    std::unordered_set<CoreIrInstruction *> &collected) {
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr || instruction == allowed_loop_leaf ||
        !loop_contains_block(loop, instruction->get_parent())) {
        return true;
    }
    if (!instruction_is_zero_fill_rematerializable(*instruction) ||
        !visiting.insert(instruction).second) {
        return false;
    }
    for (CoreIrValue *operand : instruction->get_operands()) {
        if (!collect_loop_local_pure_subtree(operand, loop, allowed_loop_leaf,
                                             visiting, collected)) {
            visiting.erase(instruction);
            return false;
        }
    }
    visiting.erase(instruction);
    collected.insert(instruction);
    return true;
}

std::optional<std::size_t> get_static_type_size_in_bytes(const CoreIrType *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Void:
    case CoreIrTypeKind::Function:
        return std::nullopt;
    case CoreIrTypeKind::Integer: {
        const auto *integer_type = static_cast<const CoreIrIntegerType *>(type);
        return (integer_type->get_bit_width() + 7U) / 8U;
    }
    case CoreIrTypeKind::Float: {
        const auto *float_type = static_cast<const CoreIrFloatType *>(type);
        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return 2;
        case CoreIrFloatKind::Float32:
            return 4;
        case CoreIrFloatKind::Float64:
            return 8;
        case CoreIrFloatKind::Float128:
            return 16;
        }
        return std::nullopt;
    }
    case CoreIrTypeKind::Pointer:
        return 8;
    case CoreIrTypeKind::Array: {
        const auto *array_type = static_cast<const CoreIrArrayType *>(type);
        const std::optional<std::size_t> element_size =
            get_static_type_size_in_bytes(array_type->get_element_type());
        if (!element_size.has_value()) {
            return std::nullopt;
        }
        return (*element_size) * array_type->get_element_count();
    }
    case CoreIrTypeKind::Struct: {
        std::size_t total = 0;
        for (const CoreIrType *element_type :
             static_cast<const CoreIrStructType *>(type)->get_element_types()) {
            const std::optional<std::size_t> element_size =
                get_static_type_size_in_bytes(element_type);
            if (!element_size.has_value()) {
                return std::nullopt;
            }
            total += *element_size;
        }
        return total;
    }
    }
    return std::nullopt;
}

struct AdditiveReductionInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrInstruction *update = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *increment_value = nullptr;
    bool subtract = false;
};

struct BitwiseReductionInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrInstruction *update = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *reduction_value = nullptr;
    CoreIrBinaryOpcode opcode = CoreIrBinaryOpcode::Or;
};

struct ZeroFillLoopInfo {
    CoreIrStoreInst *store = nullptr;
    CoreIrValue *root_base = nullptr;
    std::vector<CoreIrValue *> flattened_indices;
    std::unordered_set<CoreIrInstruction *> address_subtree;
    const CoreIrType *element_type = nullptr;
};

bool instruction_is_allowed_loop_idiom_instruction(
    const CoreIrInstruction &instruction, const CoreIrCanonicalInductionVarInfo &iv,
    const AdditiveReductionInfo &reduction) {
    if (&instruction == iv.phi || &instruction == iv.latch_update ||
        &instruction == iv.exit_compare || &instruction == reduction.phi ||
        &instruction == reduction.update) {
        return true;
    }
    if (instruction.get_opcode() == CoreIrOpcode::Phi ||
        instruction.get_opcode() == CoreIrOpcode::Jump ||
        instruction.get_opcode() == CoreIrOpcode::CondJump ||
        instruction.get_opcode() == CoreIrOpcode::IndirectJump) {
        return true;
    }
    return false;
}

bool instruction_is_allowed_loop_idiom_instruction(
    const CoreIrInstruction &instruction, const CoreIrCanonicalInductionVarInfo &iv,
    const BitwiseReductionInfo &reduction) {
    if (&instruction == iv.phi || &instruction == iv.latch_update ||
        &instruction == iv.exit_compare || &instruction == reduction.phi ||
        &instruction == reduction.update) {
        return true;
    }
    if (instruction.get_opcode() == CoreIrOpcode::Phi ||
        instruction.get_opcode() == CoreIrOpcode::Jump ||
        instruction.get_opcode() == CoreIrOpcode::CondJump ||
        instruction.get_opcode() == CoreIrOpcode::IndirectJump) {
        return true;
    }
    return false;
}

std::optional<AdditiveReductionInfo>
find_additive_reduction(const CoreIrLoopInfo &loop,
                        const CoreIrCanonicalInductionVarInfo &iv) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr || iv.latch == nullptr) {
        return std::nullopt;
    }

    for (const auto &instruction_ptr : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi == iv.phi || phi->get_incoming_count() != 2) {
            continue;
        }

        std::size_t preheader_index =
            phi->get_incoming_block(0) == preheader
                ? 0
                : phi->get_incoming_block(1) == preheader
                      ? 1
                      : phi->get_incoming_count();
        if (preheader_index >= phi->get_incoming_count()) {
            continue;
        }
        const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
        if (phi->get_incoming_block(latch_index) != iv.latch) {
            continue;
        }

        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(phi->get_incoming_value(latch_index));
        if (binary == nullptr) {
            continue;
        }

        AdditiveReductionInfo info;
        info.phi = phi;
        info.update = binary;
        info.initial_value = phi->get_incoming_value(preheader_index);

        switch (binary->get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            if (binary->get_lhs() == phi &&
                value_is_loop_invariant(binary->get_rhs(), loop)) {
                info.increment_value = binary->get_rhs();
            } else if (binary->get_rhs() == phi &&
                       value_is_loop_invariant(binary->get_lhs(), loop)) {
                info.increment_value = binary->get_lhs();
            } else {
                continue;
            }
            break;
        case CoreIrBinaryOpcode::Sub:
            if (binary->get_lhs() == phi &&
                value_is_loop_invariant(binary->get_rhs(), loop)) {
                info.increment_value = binary->get_rhs();
                info.subtract = true;
            } else {
                continue;
            }
            break;
        default:
            continue;
        }
        return info;
    }

    return std::nullopt;
}

std::optional<BitwiseReductionInfo>
find_bitwise_reduction(const CoreIrLoopInfo &loop,
                       const CoreIrCanonicalInductionVarInfo &iv) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr || iv.latch == nullptr) {
        return std::nullopt;
    }

    for (const auto &instruction_ptr : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi == iv.phi || phi->get_incoming_count() != 2) {
            continue;
        }

        std::size_t preheader_index =
            phi->get_incoming_block(0) == preheader
                ? 0
                : phi->get_incoming_block(1) == preheader
                      ? 1
                      : phi->get_incoming_count();
        if (preheader_index >= phi->get_incoming_count()) {
            continue;
        }
        const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
        if (phi->get_incoming_block(latch_index) != iv.latch) {
            continue;
        }

        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(phi->get_incoming_value(latch_index));
        if (binary == nullptr) {
            continue;
        }
        const CoreIrBinaryOpcode opcode = binary->get_binary_opcode();
        if (opcode != CoreIrBinaryOpcode::And &&
            opcode != CoreIrBinaryOpcode::Or &&
            opcode != CoreIrBinaryOpcode::Xor) {
            continue;
        }

        BitwiseReductionInfo info;
        info.phi = phi;
        info.update = binary;
        info.initial_value = phi->get_incoming_value(preheader_index);
        info.opcode = opcode;

        if (binary->get_lhs() == phi &&
            value_is_loop_invariant(binary->get_rhs(), loop)) {
            info.reduction_value = binary->get_rhs();
        } else if (binary->get_rhs() == phi &&
                   value_is_loop_invariant(binary->get_lhs(), loop)) {
            info.reduction_value = binary->get_lhs();
        } else {
            continue;
        }
        return info;
    }

    return std::nullopt;
}

std::optional<ZeroFillLoopInfo>
find_zero_fill_loop(const CoreIrLoopInfo &loop,
                    const CoreIrCanonicalInductionVarInfo &iv) {
    if (iv.step != 1 || !detail::is_zero_integer_constant(iv.initial_value) ||
        loop.get_exit_blocks().size() != 1) {
        return std::nullopt;
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    if (exit_block == nullptr) {
        return std::nullopt;
    }
    for (const auto &instruction_ptr : exit_block->get_instructions()) {
        if (dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get()) != nullptr) {
            return std::nullopt;
        }
        break;
    }

    ZeroFillLoopInfo info;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
            if (store != nullptr) {
                if (info.store != nullptr ||
                    !detail::is_zero_integer_constant(store->get_value()) ||
                    store->get_address() == nullptr) {
                    return std::nullopt;
                }
                auto *gep =
                    dynamic_cast<CoreIrGetElementPtrInst *>(store->get_address());
                if (gep == nullptr) {
                    return std::nullopt;
                }

                CoreIrValue *root_base = nullptr;
                std::vector<CoreIrValue *> indices;
                if (!detail::collect_structural_gep_chain(*gep, root_base, indices) ||
                    indices.empty() || indices.back() != iv.phi) {
                    return std::nullopt;
                }

                std::unordered_set<const CoreIrInstruction *> visiting;
                if (!value_is_recursively_loop_invariant(root_base, loop, visiting)) {
                    return std::nullopt;
                }
                for (std::size_t index = 0; index + 1 < indices.size(); ++index) {
                    if (!value_is_recursively_loop_invariant(indices[index], loop,
                                                             visiting)) {
                        return std::nullopt;
                    }
                }

                std::unordered_set<CoreIrInstruction *> address_subtree;
                std::unordered_set<const CoreIrInstruction *> collect_visiting;
                if (!collect_loop_local_pure_subtree(store->get_address(), loop,
                                                     iv.phi,
                                                     collect_visiting,
                                                     address_subtree)) {
                    return std::nullopt;
                }

                info.store = store;
                info.root_base = root_base;
                info.flattened_indices = std::move(indices);
                info.address_subtree = std::move(address_subtree);
                info.element_type =
                    store->get_value() == nullptr ? nullptr
                                                  : store->get_value()->get_type();
            }
        }
    }

    if (info.store == nullptr || info.element_type == nullptr) {
        return std::nullopt;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr || instruction == info.store ||
                info.address_subtree.find(instruction) != info.address_subtree.end()) {
                continue;
            }
            if (instruction == iv.phi || instruction == iv.latch_update ||
                instruction == iv.exit_compare || instruction == iv.exit_branch ||
                dynamic_cast<CoreIrJumpInst *>(instruction) != nullptr ||
                dynamic_cast<CoreIrCondJumpInst *>(instruction) != nullptr) {
                continue;
            }
            return std::nullopt;
        }
    }

    return info;
}

CoreIrPhiInst *find_lcssa_phi_for_value(CoreIrBasicBlock &exit_block,
                                        CoreIrInstruction &value) {
    for (const auto &instruction_ptr : exit_block.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_value(index) == &value) {
                return phi;
            }
        }
    }
    return nullptr;
}

CoreIrInstruction *insert_binary_before_terminator(CoreIrBasicBlock &block,
                                                   CoreIrBinaryOpcode opcode,
                                                   const CoreIrType *type,
                                                   std::string name,
                                                   CoreIrValue *lhs,
                                                   CoreIrValue *rhs) {
    auto instruction =
        std::make_unique<CoreIrBinaryInst>(opcode, type, std::move(name), lhs, rhs);
    return insert_instruction_before(block, block.get_instructions().back().get(),
                                     std::move(instruction));
}

CoreIrFunction *get_or_create_memset_decl(CoreIrModule &module,
                                          CoreIrContext &core_ir_context) {
    if (CoreIrFunction *existing = module.find_function("llvm.memset.p0.i64");
        existing != nullptr) {
        return existing;
    }

    auto *void_type = core_ir_context.create_type<CoreIrVoidType>();
    auto *i1_type = core_ir_context.create_type<CoreIrIntegerType>(1);
    auto *i8_type = core_ir_context.create_type<CoreIrIntegerType>(8, false);
    auto *i64_type = core_ir_context.create_type<CoreIrIntegerType>(64, false);
    auto *ptr_i8_type = core_ir_context.create_type<CoreIrPointerType>(i8_type);
    auto *function_type = core_ir_context.create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{ptr_i8_type, i8_type, i64_type,
                                                   i1_type},
        false);
    return module.create_function<CoreIrFunction>("llvm.memset.p0.i64", function_type,
                                                  false);
}

CoreIrValue *materialize_loop_invariant_value(
    CoreIrValue *value, const CoreIrLoopInfo &loop, CoreIrBasicBlock &block,
    std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map) {
    if (value == nullptr) {
        return nullptr;
    }
    auto mapped_it = value_map.find(value);
    if (mapped_it != value_map.end()) {
        return mapped_it->second;
    }

    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr || !loop_contains_block(loop, instruction->get_parent())) {
        value_map.emplace(value, value);
        return value;
    }
    if (!instruction_is_zero_fill_rematerializable(*instruction)) {
        return nullptr;
    }

    for (CoreIrValue *operand : instruction->get_operands()) {
        if (materialize_loop_invariant_value(operand, loop, block, value_map) ==
            nullptr) {
            return nullptr;
        }
    }

    std::unique_ptr<CoreIrInstruction> clone =
        clone_instruction_remapped(*instruction, value_map);
    if (clone == nullptr) {
        return nullptr;
    }
    CoreIrInstruction *clone_ptr = block.append_instruction(std::move(clone));
    value_map.emplace(value, clone_ptr);
    return clone_ptr;
}

bool fold_zero_fill_loop(CoreIrFunction &function, const CoreIrLoopInfo &loop,
                         const CoreIrCanonicalInductionVarInfo &iv,
                         const ZeroFillLoopInfo &zero_fill,
                         CoreIrModule &module,
                         CoreIrContext &core_ir_context) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *exit_block = loop.get_exit_blocks().empty()
                                       ? nullptr
                                       : *loop.get_exit_blocks().begin();
    if (header == nullptr || exit_block == nullptr || iv.exit_bound == nullptr ||
        iv.exit_compare == nullptr || iv.exit_branch == nullptr) {
        return false;
    }

    auto *bound_type =
        dynamic_cast<const CoreIrIntegerType *>(iv.exit_bound->get_type());
    auto *iv_type =
        dynamic_cast<const CoreIrIntegerType *>(iv.phi == nullptr ? nullptr
                                                                  : iv.phi->get_type());
    if (bound_type == nullptr || iv_type == nullptr) {
        return false;
    }

    const std::optional<std::size_t> element_size =
        get_static_type_size_in_bytes(zero_fill.element_type);
    if (!element_size.has_value() || *element_size == 0) {
        return false;
    }

    if (iv.normalized_predicate != CoreIrComparePredicate::SignedLess &&
        iv.normalized_predicate != CoreIrComparePredicate::UnsignedLess) {
        return false;
    }

    CoreIrFunction *memset_decl = get_or_create_memset_decl(module, core_ir_context);
    if (memset_decl == nullptr || memset_decl->get_function_type() == nullptr) {
        return false;
    }

    auto *void_type = core_ir_context.create_type<CoreIrVoidType>();
    auto *i1_type = core_ir_context.create_type<CoreIrIntegerType>(1);
    auto *i8_type = core_ir_context.create_type<CoreIrIntegerType>(8, false);
    auto *i64_type = core_ir_context.create_type<CoreIrIntegerType>(64, false);
    auto *ptr_i8_type = core_ir_context.create_type<CoreIrPointerType>(i8_type);

    CoreIrBasicBlock *memset_block = function.append_basic_block(
        std::make_unique<CoreIrBasicBlock>(header->get_name() + ".memset"));
    if (memset_block == nullptr) {
        return false;
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> materialized_values;
    CoreIrValue *root_base = materialize_loop_invariant_value(
        zero_fill.root_base, loop, *memset_block, materialized_values);
    if (root_base == nullptr) {
        return false;
    }

    std::vector<CoreIrValue *> start_indices;
    start_indices.reserve(zero_fill.flattened_indices.size());
    for (std::size_t index = 0; index < zero_fill.flattened_indices.size(); ++index) {
        if (index + 1 == zero_fill.flattened_indices.size()) {
            start_indices.push_back(
                core_ir_context.create_constant<CoreIrConstantInt>(iv_type, 0));
            continue;
        }
        CoreIrValue *materialized_index = materialize_loop_invariant_value(
            zero_fill.flattened_indices[index], loop, *memset_block,
            materialized_values);
        if (materialized_index == nullptr) {
            return false;
        }
        start_indices.push_back(materialized_index);
    }

    auto gep = std::make_unique<CoreIrGetElementPtrInst>(
        zero_fill.store->get_address()->get_type(),
        zero_fill.store->get_name() + ".memset.base", root_base,
        std::move(start_indices));
    CoreIrValue *memset_dest = memset_block->append_instruction(std::move(gep));
    if (memset_dest == nullptr) {
        return false;
    }

    if (!detail::are_equivalent_types(memset_dest->get_type(), ptr_i8_type)) {
        auto ptr_to_int = std::make_unique<CoreIrCastInst>(
            CoreIrCastKind::PtrToInt, i64_type,
            zero_fill.store->get_name() + ".memset.ptrint", memset_dest);
        CoreIrInstruction *ptr_int =
            memset_block->append_instruction(std::move(ptr_to_int));
        if (ptr_int == nullptr) {
            return false;
        }
        auto int_to_ptr = std::make_unique<CoreIrCastInst>(
            CoreIrCastKind::IntToPtr, ptr_i8_type,
            zero_fill.store->get_name() + ".memset.dest", ptr_int);
        memset_dest = memset_block->append_instruction(std::move(int_to_ptr));
        if (memset_dest == nullptr) {
            return false;
        }
    }

    CoreIrValue *bound_value = iv.exit_bound;
    if (!detail::are_equivalent_types(bound_value->get_type(), i64_type)) {
        CoreIrCastKind cast_kind =
            bound_type->get_bit_width() < 64 ? CoreIrCastKind::ZeroExtend
                                             : CoreIrCastKind::Truncate;
        auto cast = std::make_unique<CoreIrCastInst>(
            cast_kind, i64_type, zero_fill.store->get_name() + ".memset.bytes",
            bound_value);
        bound_value = memset_block->append_instruction(std::move(cast));
        if (bound_value == nullptr) {
            return false;
        }
    }

    CoreIrValue *byte_count = bound_value;
    if (*element_size != 1) {
        auto *size_constant = core_ir_context.create_constant<CoreIrConstantInt>(
            i64_type, *element_size);
        auto mul = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Mul, i64_type,
            zero_fill.store->get_name() + ".memset.bytecount", bound_value,
            size_constant);
        byte_count = memset_block->append_instruction(std::move(mul));
        if (byte_count == nullptr) {
            return false;
        }
    }

    auto *zero_i8 =
        core_ir_context.create_constant<CoreIrConstantInt>(i8_type, 0);
    auto *is_volatile_false =
        core_ir_context.create_constant<CoreIrConstantInt>(i1_type, 0);
    auto memset_call = std::make_unique<CoreIrCallInst>(
        void_type, "", memset_decl->get_name(), memset_decl->get_function_type(),
        std::vector<CoreIrValue *>{memset_dest, zero_i8, byte_count, is_volatile_false});
    if (memset_block->append_instruction(std::move(memset_call)) == nullptr) {
        return false;
    }
    memset_block->append_instruction(
        std::make_unique<CoreIrJumpInst>(void_type, exit_block));

    auto &header_instructions = header->get_instructions();
    while (!header_instructions.empty()) {
        CoreIrInstruction *instruction = header_instructions.back().get();
        if (dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr) {
            break;
        }
        if (!erase_instruction(*header, instruction)) {
            return false;
        }
    }

    auto *zero_bound =
        core_ir_context.create_constant<CoreIrConstantInt>(bound_type, 0);
    const CoreIrComparePredicate guard_predicate =
        iv.normalized_predicate == CoreIrComparePredicate::SignedLess
            ? CoreIrComparePredicate::SignedGreater
            : CoreIrComparePredicate::NotEqual;
    auto guard_compare = std::make_unique<CoreIrCompareInst>(
        guard_predicate, i1_type, zero_fill.store->get_name() + ".memset.guard",
        iv.exit_bound, zero_bound);
    CoreIrInstruction *guard_value =
        header->append_instruction(std::move(guard_compare));
    if (guard_value == nullptr) {
        return false;
    }
    header->append_instruction(
        std::make_unique<CoreIrCondJumpInst>(void_type, guard_value, memset_block,
                                             exit_block));
    return true;
}

bool fold_counted_additive_reduction(CoreIrFunction &,
                                     const CoreIrLoopInfo &loop,
                                     const CoreIrCanonicalInductionVarInfo &iv,
                                     const AdditiveReductionInfo &reduction,
                                     const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                                     CoreIrContext &core_ir_context) {
    std::optional<std::uint64_t> trip_count = scev.get_constant_trip_count(loop);
    if (!trip_count.has_value() || loop.get_exit_blocks().size() != 1 ||
        loop.get_preheader() == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                instruction_is_allowed_loop_idiom_instruction(*instruction, iv,
                                                              reduction)) {
                continue;
            }
            return false;
        }
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    CoreIrPhiInst *exit_phi = exit_block == nullptr
                                  ? nullptr
                                  : find_lcssa_phi_for_value(*exit_block, *reduction.phi);
    if (exit_phi == nullptr) {
        return false;
    }

    auto *integer_type =
        dynamic_cast<const CoreIrIntegerType *>(reduction.phi->get_type());
    if (integer_type == nullptr) {
        return false;
    }

    CoreIrValue *trip_count_value = core_ir_context.create_constant<CoreIrConstantInt>(
        integer_type, *trip_count);
    CoreIrInstruction *mul = insert_binary_before_terminator(
        *loop.get_preheader(), CoreIrBinaryOpcode::Mul, reduction.phi->get_type(),
        reduction.phi->get_name() + ".idiom.mul", reduction.increment_value,
        trip_count_value);
    if (mul == nullptr) {
        return false;
    }
    CoreIrInstruction *final_value = insert_binary_before_terminator(
        *loop.get_preheader(),
        reduction.subtract ? CoreIrBinaryOpcode::Sub : CoreIrBinaryOpcode::Add,
        reduction.phi->get_type(), reduction.phi->get_name() + ".idiom.final",
        reduction.initial_value, mul);
    if (final_value == nullptr) {
        return false;
    }

    exit_phi->replace_all_uses_with(final_value);
    erase_instruction(*exit_block, exit_phi);

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().back().get());
    if (preheader_jump == nullptr) {
        return false;
    }
    preheader_jump->set_target_block(exit_block);
    return true;
}

bool fold_counted_bitwise_reduction(CoreIrFunction &,
                                    const CoreIrLoopInfo &loop,
                                    const CoreIrCanonicalInductionVarInfo &iv,
                                    const BitwiseReductionInfo &reduction,
                                    const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    std::optional<std::uint64_t> trip_count = scev.get_constant_trip_count(loop);
    if (!trip_count.has_value() || loop.get_exit_blocks().size() != 1 ||
        loop.get_preheader() == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                instruction_is_allowed_loop_idiom_instruction(*instruction, iv,
                                                              reduction)) {
                continue;
            }
            return false;
        }
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    CoreIrPhiInst *exit_phi = exit_block == nullptr
                                  ? nullptr
                                  : find_lcssa_phi_for_value(*exit_block, *reduction.phi);
    if (exit_phi == nullptr) {
        return false;
    }

    CoreIrValue *final_value = reduction.initial_value;
    if (*trip_count > 0) {
        switch (reduction.opcode) {
        case CoreIrBinaryOpcode::And:
        case CoreIrBinaryOpcode::Or:
            final_value = insert_binary_before_terminator(
                *loop.get_preheader(), reduction.opcode, reduction.phi->get_type(),
                reduction.phi->get_name() + ".idiom.final",
                reduction.initial_value, reduction.reduction_value);
            break;
        case CoreIrBinaryOpcode::Xor:
            if ((*trip_count & 1U) != 0U) {
                final_value = insert_binary_before_terminator(
                    *loop.get_preheader(), reduction.opcode, reduction.phi->get_type(),
                    reduction.phi->get_name() + ".idiom.final",
                    reduction.initial_value, reduction.reduction_value);
            }
            break;
        default:
            return false;
        }
    }
    if (final_value == nullptr) {
        return false;
    }

    exit_phi->replace_all_uses_with(final_value);
    erase_instruction(*exit_block, exit_phi);

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().back().get());
    if (preheader_jump == nullptr) {
        return false;
    }
    preheader_jump->set_target_block(exit_block);
    return true;
}

} // namespace

PassKind CoreIrLoopIdiomPass::Kind() const { return PassKind::CoreIrLoopIdiom; }

const char *CoreIrLoopIdiomPass::Name() const { return "CoreIrLoopIdiomPass"; }

PassResult CoreIrLoopIdiomPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    CoreIrContext *core_ir_context = build_result->get_context();
    if (analysis_manager == nullptr || core_ir_context == nullptr) {
        return PassResult::Failure("missing core ir loop idiom dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrInductionVarAnalysisResult &induction_vars =
            analysis_manager->get_or_compute<CoreIrInductionVarAnalysis>(*function);
        const CoreIrScalarEvolutionLiteAnalysisResult &scev =
            analysis_manager->get_or_compute<CoreIrScalarEvolutionLiteAnalysis>(*function);

        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr == nullptr) {
                continue;
            }
            const CoreIrCanonicalInductionVarInfo *iv =
                induction_vars.get_canonical_induction_var(*loop_ptr);
            if (iv == nullptr) {
                continue;
            }
            const std::optional<ZeroFillLoopInfo> zero_fill =
                find_zero_fill_loop(*loop_ptr, *iv);
            if (zero_fill.has_value()) {
                function_changed =
                    fold_zero_fill_loop(*function, *loop_ptr, *iv, *zero_fill,
                                        *module, *core_ir_context) ||
                    function_changed;
                continue;
            }
            const std::optional<AdditiveReductionInfo> reduction =
                find_additive_reduction(*loop_ptr, *iv);
            if (reduction.has_value()) {
                function_changed =
                    fold_counted_additive_reduction(*function, *loop_ptr, *iv,
                                                   *reduction, scev,
                                                   *core_ir_context) ||
                    function_changed;
                continue;
            }
            const std::optional<BitwiseReductionInfo> bitwise_reduction =
                find_bitwise_reduction(*loop_ptr, *iv);
            if (!bitwise_reduction.has_value()) {
                continue;
            }
            function_changed =
                fold_counted_bitwise_reduction(*function, *loop_ptr, *iv,
                                               *bitwise_reduction, scev) ||
                function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }

    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
