#include "backend/ir/sccp/core_ir_sccp_pass.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

enum class SccpLatticeKind : unsigned char {
    Unknown,
    Constant,
    Overdefined,
};

struct SccpLatticeValue {
    SccpLatticeKind kind = SccpLatticeKind::Unknown;
    CoreIrValue *constant = nullptr;
};

struct BlockEdgeKey {
    const CoreIrBasicBlock *from = nullptr;
    const CoreIrBasicBlock *to = nullptr;

    bool operator==(const BlockEdgeKey &other) const noexcept {
        return from == other.from && to == other.to;
    }
};

struct BlockEdgeKeyHash {
    std::size_t operator()(const BlockEdgeKey &key) const noexcept {
        std::size_t hash =
            reinterpret_cast<std::uintptr_t>(key.from) + 0x9e3779b9U;
        hash ^= reinterpret_cast<std::uintptr_t>(key.to) + 0x9e3779b9U +
                (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool are_equivalent_constants(CoreIrValue *lhs, CoreIrValue *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr || lhs->get_type() != rhs->get_type()) {
        return false;
    }
    if (const auto *lhs_int = dynamic_cast<const CoreIrConstantInt *>(lhs);
        lhs_int != nullptr) {
        const auto *rhs_int = dynamic_cast<const CoreIrConstantInt *>(rhs);
        return rhs_int != nullptr && lhs_int->get_value() == rhs_int->get_value();
    }
    if (const auto *lhs_float = dynamic_cast<const CoreIrConstantFloat *>(lhs);
        lhs_float != nullptr) {
        const auto *rhs_float = dynamic_cast<const CoreIrConstantFloat *>(rhs);
        return rhs_float != nullptr &&
               lhs_float->get_literal_text() == rhs_float->get_literal_text();
    }
    if (dynamic_cast<const CoreIrConstantNull *>(lhs) != nullptr) {
        return dynamic_cast<const CoreIrConstantNull *>(rhs) != nullptr;
    }
    if (dynamic_cast<const CoreIrConstantZeroInitializer *>(lhs) != nullptr) {
        return dynamic_cast<const CoreIrConstantZeroInitializer *>(rhs) != nullptr;
    }
    return false;
}

bool mark_block_executable(std::unordered_set<CoreIrBasicBlock *> &blocks,
                           CoreIrBasicBlock *block) {
    return block != nullptr && blocks.insert(block).second;
}

bool mark_edge_executable(
    std::unordered_set<BlockEdgeKey, BlockEdgeKeyHash> &edges,
    const CoreIrBasicBlock *from, const CoreIrBasicBlock *to) {
    return from != nullptr && to != nullptr &&
           edges.insert(BlockEdgeKey{from, to}).second;
}

const CoreIrConstantInt *as_int_constant(CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
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

std::uint64_t truncate_to_bit_width(std::uint64_t value, std::size_t bit_width) {
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

SccpLatticeValue make_unknown() { return {}; }

SccpLatticeValue make_overdefined() {
    return SccpLatticeValue{SccpLatticeKind::Overdefined, nullptr};
}

SccpLatticeValue make_constant(CoreIrValue *constant) {
    return SccpLatticeValue{SccpLatticeKind::Constant, constant};
}

SccpLatticeValue join_lattice(SccpLatticeValue current, SccpLatticeValue next) {
    if (current.kind == SccpLatticeKind::Overdefined ||
        next.kind == SccpLatticeKind::Overdefined) {
        return make_overdefined();
    }
    if (current.kind == SccpLatticeKind::Unknown) {
        return next;
    }
    if (next.kind == SccpLatticeKind::Unknown) {
        return current;
    }
    if (are_equivalent_constants(current.constant, next.constant)) {
        return current;
    }
    return make_overdefined();
}

SccpLatticeValue get_value_lattice(
    CoreIrValue *value,
    const std::unordered_map<const CoreIrValue *, SccpLatticeValue> &lattice_map) {
    if (value == nullptr) {
        return make_overdefined();
    }
    if (dynamic_cast<CoreIrConstant *>(value) != nullptr) {
        return make_constant(value);
    }
    auto it = lattice_map.find(value);
    if (it == lattice_map.end()) {
        return make_overdefined();
    }
    return it->second;
}

CoreIrValue *fold_integer_binary(CoreIrContext &context, const CoreIrBinaryInst &inst,
                                 const CoreIrConstantInt &lhs,
                                 const CoreIrConstantInt &rhs) {
    const std::size_t bit_width = get_integer_bit_width(inst.get_type());
    if (bit_width == 0) {
        return nullptr;
    }

    std::uint64_t lhs_value = truncate_to_bit_width(lhs.get_value(), bit_width);
    std::uint64_t rhs_value = truncate_to_bit_width(rhs.get_value(), bit_width);
    std::uint64_t result = 0;
    switch (inst.get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        result = lhs_value + rhs_value;
        break;
    case CoreIrBinaryOpcode::Sub:
        result = lhs_value - rhs_value;
        break;
    case CoreIrBinaryOpcode::Mul:
        result = lhs_value * rhs_value;
        break;
    case CoreIrBinaryOpcode::SDiv: {
        const std::int64_t lhs_signed = sign_extend_to_i64(lhs_value, bit_width);
        const std::int64_t rhs_signed = sign_extend_to_i64(rhs_value, bit_width);
        if (rhs_signed == 0) {
            return nullptr;
        }
        result = static_cast<std::uint64_t>(lhs_signed / rhs_signed);
        break;
    }
    case CoreIrBinaryOpcode::UDiv:
        if (rhs_value == 0) {
            return nullptr;
        }
        result = lhs_value / rhs_value;
        break;
    case CoreIrBinaryOpcode::SRem: {
        const std::int64_t lhs_signed = sign_extend_to_i64(lhs_value, bit_width);
        const std::int64_t rhs_signed = sign_extend_to_i64(rhs_value, bit_width);
        if (rhs_signed == 0) {
            return nullptr;
        }
        result = static_cast<std::uint64_t>(lhs_signed % rhs_signed);
        break;
    }
    case CoreIrBinaryOpcode::URem:
        if (rhs_value == 0) {
            return nullptr;
        }
        result = lhs_value % rhs_value;
        break;
    case CoreIrBinaryOpcode::And:
        result = lhs_value & rhs_value;
        break;
    case CoreIrBinaryOpcode::Or:
        result = lhs_value | rhs_value;
        break;
    case CoreIrBinaryOpcode::Xor:
        result = lhs_value ^ rhs_value;
        break;
    case CoreIrBinaryOpcode::Shl:
        result = lhs_value << rhs_value;
        break;
    case CoreIrBinaryOpcode::LShr:
        result = lhs_value >> rhs_value;
        break;
    case CoreIrBinaryOpcode::AShr:
        result = static_cast<std::uint64_t>(
            sign_extend_to_i64(lhs_value, bit_width) >> rhs_value);
        break;
    }
    result = truncate_to_bit_width(result, bit_width);
    return context.create_constant<CoreIrConstantInt>(inst.get_type(), result);
}

SccpLatticeValue evaluate_phi(
    CoreIrPhiInst &phi, const std::unordered_set<CoreIrBasicBlock *> &executable_blocks,
    const std::unordered_set<BlockEdgeKey, BlockEdgeKeyHash> &executable_edges,
    const std::unordered_map<const CoreIrValue *, SccpLatticeValue> &lattice_map) {
    SccpLatticeValue merged = make_unknown();
    for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
        CoreIrBasicBlock *incoming_block = phi.get_incoming_block(index);
        if (incoming_block == nullptr ||
            executable_blocks.find(incoming_block) == executable_blocks.end()) {
            continue;
        }
        if (phi.get_parent() != nullptr &&
            executable_edges.find(BlockEdgeKey{incoming_block, phi.get_parent()}) ==
                executable_edges.end()) {
            continue;
        }
        merged = join_lattice(
            merged, get_value_lattice(phi.get_incoming_value(index), lattice_map));
        if (merged.kind == SccpLatticeKind::Overdefined) {
            break;
        }
    }
    return merged;
}

SccpLatticeValue evaluate_instruction(
    CoreIrContext &context, CoreIrInstruction &instruction,
    const std::unordered_set<CoreIrBasicBlock *> &executable_blocks,
    const std::unordered_set<BlockEdgeKey, BlockEdgeKeyHash> &executable_edges,
    const std::unordered_map<const CoreIrValue *, SccpLatticeValue> &lattice_map) {
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(&instruction); phi != nullptr) {
        return evaluate_phi(*phi, executable_blocks, executable_edges, lattice_map);
    }

    if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(&instruction); binary != nullptr) {
        SccpLatticeValue lhs = get_value_lattice(binary->get_lhs(), lattice_map);
        SccpLatticeValue rhs = get_value_lattice(binary->get_rhs(), lattice_map);
        if (lhs.kind == SccpLatticeKind::Unknown || rhs.kind == SccpLatticeKind::Unknown) {
            return make_unknown();
        }
        if (lhs.kind != SccpLatticeKind::Constant ||
            rhs.kind != SccpLatticeKind::Constant) {
            return make_overdefined();
        }
        auto *lhs_int = dynamic_cast<const CoreIrConstantInt *>(lhs.constant);
        auto *rhs_int = dynamic_cast<const CoreIrConstantInt *>(rhs.constant);
        if (lhs_int == nullptr || rhs_int == nullptr) {
            return make_overdefined();
        }
        CoreIrValue *result = fold_integer_binary(context, *binary, *lhs_int, *rhs_int);
        return result == nullptr ? make_overdefined() : make_constant(result);
    }

    if (auto *unary = dynamic_cast<CoreIrUnaryInst *>(&instruction); unary != nullptr) {
        SccpLatticeValue operand = get_value_lattice(unary->get_operand(), lattice_map);
        if (operand.kind == SccpLatticeKind::Unknown) {
            return make_unknown();
        }
        if (operand.kind != SccpLatticeKind::Constant) {
            return make_overdefined();
        }
        auto *operand_int = dynamic_cast<const CoreIrConstantInt *>(operand.constant);
        if (operand_int == nullptr) {
            return make_overdefined();
        }
        switch (unary->get_unary_opcode()) {
        case CoreIrUnaryOpcode::LogicalNot:
            return make_constant(context.create_constant<CoreIrConstantInt>(
                unary->get_type(), operand_int->get_value() == 0 ? 1 : 0));
        case CoreIrUnaryOpcode::Negate:
        case CoreIrUnaryOpcode::BitwiseNot:
            return make_overdefined();
        }
    }

    if (auto *compare = dynamic_cast<CoreIrCompareInst *>(&instruction);
        compare != nullptr) {
        SccpLatticeValue lhs = get_value_lattice(compare->get_lhs(), lattice_map);
        SccpLatticeValue rhs = get_value_lattice(compare->get_rhs(), lattice_map);
        if (lhs.kind == SccpLatticeKind::Unknown || rhs.kind == SccpLatticeKind::Unknown) {
            return make_unknown();
        }
        if (lhs.kind != SccpLatticeKind::Constant ||
            rhs.kind != SccpLatticeKind::Constant) {
            return make_overdefined();
        }

        auto *lhs_int = dynamic_cast<const CoreIrConstantInt *>(lhs.constant);
        auto *rhs_int = dynamic_cast<const CoreIrConstantInt *>(rhs.constant);
        if (lhs_int == nullptr || rhs_int == nullptr) {
            return make_overdefined();
        }

        const std::int64_t lhs_signed = sign_extend_to_i64(lhs_int->get_value(),
                                                           get_integer_bit_width(
                                                               lhs_int->get_type()));
        const std::int64_t rhs_signed = sign_extend_to_i64(rhs_int->get_value(),
                                                           get_integer_bit_width(
                                                               rhs_int->get_type()));
        bool result = false;
        switch (compare->get_predicate()) {
        case CoreIrComparePredicate::Equal:
            result = lhs_int->get_value() == rhs_int->get_value();
            break;
        case CoreIrComparePredicate::NotEqual:
            result = lhs_int->get_value() != rhs_int->get_value();
            break;
        case CoreIrComparePredicate::SignedLess:
            result = lhs_signed < rhs_signed;
            break;
        case CoreIrComparePredicate::SignedLessEqual:
            result = lhs_signed <= rhs_signed;
            break;
        case CoreIrComparePredicate::SignedGreater:
            result = lhs_signed > rhs_signed;
            break;
        case CoreIrComparePredicate::SignedGreaterEqual:
            result = lhs_signed >= rhs_signed;
            break;
        case CoreIrComparePredicate::UnsignedLess:
            result = lhs_int->get_value() < rhs_int->get_value();
            break;
        case CoreIrComparePredicate::UnsignedLessEqual:
            result = lhs_int->get_value() <= rhs_int->get_value();
            break;
        case CoreIrComparePredicate::UnsignedGreater:
            result = lhs_int->get_value() > rhs_int->get_value();
            break;
        case CoreIrComparePredicate::UnsignedGreaterEqual:
            result = lhs_int->get_value() >= rhs_int->get_value();
            break;
        }
        return make_constant(context.create_constant<CoreIrConstantInt>(
            compare->get_type(), result ? 1 : 0));
    }

    if (auto *cast = dynamic_cast<CoreIrCastInst *>(&instruction); cast != nullptr) {
        SccpLatticeValue operand = get_value_lattice(cast->get_operand(), lattice_map);
        if (operand.kind == SccpLatticeKind::Unknown) {
            return make_unknown();
        }
        if (operand.kind != SccpLatticeKind::Constant) {
            return make_overdefined();
        }
        if (cast->get_operand()->get_type() == cast->get_type()) {
            return operand;
        }
        auto *operand_int = dynamic_cast<const CoreIrConstantInt *>(operand.constant);
        if (operand_int == nullptr) {
            return make_overdefined();
        }
        switch (cast->get_cast_kind()) {
        case CoreIrCastKind::SignExtend:
        case CoreIrCastKind::ZeroExtend:
        case CoreIrCastKind::Truncate:
            return make_constant(context.create_constant<CoreIrConstantInt>(
                cast->get_type(), operand_int->get_value()));
        case CoreIrCastKind::SignedIntToFloat:
        case CoreIrCastKind::UnsignedIntToFloat:
        case CoreIrCastKind::FloatToSignedInt:
        case CoreIrCastKind::FloatToUnsignedInt:
        case CoreIrCastKind::FloatExtend:
        case CoreIrCastKind::FloatTruncate:
        case CoreIrCastKind::PtrToInt:
        case CoreIrCastKind::IntToPtr:
            return make_overdefined();
        }
    }

    return make_overdefined();
}

bool update_lattice(
    std::unordered_map<const CoreIrValue *, SccpLatticeValue> &lattice_map,
    const CoreIrValue *value, SccpLatticeValue next_value) {
    const SccpLatticeValue current_value =
        lattice_map.find(value) == lattice_map.end() ? make_unknown()
                                                     : lattice_map[value];
    const SccpLatticeValue merged = join_lattice(current_value, next_value);
    if (merged.kind == current_value.kind &&
        are_equivalent_constants(merged.constant, current_value.constant)) {
        return false;
    }
    lattice_map[value] = merged;
    return true;
}

bool update_executable_successors(
    CoreIrBasicBlock &block,
    std::unordered_set<CoreIrBasicBlock *> &executable_blocks,
    std::unordered_set<BlockEdgeKey, BlockEdgeKeyHash> &executable_edges,
    const std::unordered_map<const CoreIrValue *, SccpLatticeValue> &lattice_map) {
    if (block.get_instructions().empty()) {
        return false;
    }
    bool changed = false;
    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        changed = mark_block_executable(executable_blocks, jump->get_target_block()) ||
                  changed;
        changed = mark_edge_executable(executable_edges, &block,
                                       jump->get_target_block()) ||
                  changed;
        return changed;
    }
    auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return false;
    }

    const SccpLatticeValue condition =
        get_value_lattice(cond_jump->get_condition(), lattice_map);
    if (condition.kind == SccpLatticeKind::Constant) {
        auto *constant = as_int_constant(condition.constant);
        if (constant != nullptr && constant->get_value() == 0) {
            changed = mark_block_executable(executable_blocks,
                                            cond_jump->get_false_block()) ||
                      changed;
            changed = mark_edge_executable(executable_edges, &block,
                                           cond_jump->get_false_block()) ||
                      changed;
            return changed;
        }
        changed = mark_block_executable(executable_blocks, cond_jump->get_true_block()) ||
                  changed;
        changed = mark_edge_executable(executable_edges, &block,
                                       cond_jump->get_true_block()) ||
                  changed;
        return changed;
    }

    changed = mark_block_executable(executable_blocks, cond_jump->get_true_block()) ||
              changed;
    changed = mark_block_executable(executable_blocks, cond_jump->get_false_block()) ||
              changed;
    changed = mark_edge_executable(executable_edges, &block,
                                   cond_jump->get_true_block()) ||
              changed;
    changed = mark_edge_executable(executable_edges, &block,
                                   cond_jump->get_false_block()) ||
              changed;
    return changed;
}

} // namespace

PassKind CoreIrSccpPass::Kind() const { return PassKind::CoreIrSccp; }

const char *CoreIrSccpPass::Name() const { return "CoreIrSccpPass"; }

PassResult CoreIrSccpPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrModule *module = build_result->get_module();
    if (core_ir_context == nullptr || module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    for (const auto &function : module->get_functions()) {
        if (function == nullptr || function->get_basic_blocks().empty()) {
            continue;
        }

        std::unordered_map<const CoreIrValue *, SccpLatticeValue> lattice_map;
        std::unordered_set<CoreIrBasicBlock *> executable_blocks;
        std::unordered_set<BlockEdgeKey, BlockEdgeKeyHash> executable_edges;
        mark_block_executable(executable_blocks,
                              function->get_basic_blocks().front().get());

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto &block : function->get_basic_blocks()) {
                if (block == nullptr ||
                    executable_blocks.find(block.get()) == executable_blocks.end()) {
                    continue;
                }
                for (const auto &instruction : block->get_instructions()) {
                    if (instruction == nullptr) {
                        continue;
                    }
                    if (dynamic_cast<CoreIrPhiInst *>(instruction.get()) == nullptr &&
                        instruction->get_has_side_effect()) {
                        continue;
                    }
                    if (dynamic_cast<CoreIrBinaryInst *>(instruction.get()) == nullptr &&
                        dynamic_cast<CoreIrUnaryInst *>(instruction.get()) == nullptr &&
                        dynamic_cast<CoreIrCompareInst *>(instruction.get()) == nullptr &&
                        dynamic_cast<CoreIrCastInst *>(instruction.get()) == nullptr &&
                        dynamic_cast<CoreIrPhiInst *>(instruction.get()) == nullptr) {
                        continue;
                    }
                    changed = update_lattice(
                                  lattice_map, instruction.get(),
                                  evaluate_instruction(*core_ir_context,
                                                       *instruction, executable_blocks,
                                                       executable_edges, lattice_map)) ||
                              changed;
                }
                changed = update_executable_successors(*block, executable_blocks,
                                                       executable_edges, lattice_map) ||
                          changed;
            }
        }

        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            if (block == nullptr ||
                executable_blocks.find(block.get()) == executable_blocks.end()) {
                continue;
            }
            for (const auto &instruction : block->get_instructions()) {
                if (instruction == nullptr || instruction->get_has_side_effect()) {
                    continue;
                }
                auto it = lattice_map.find(instruction.get());
                if (it == lattice_map.end() ||
                    it->second.kind != SccpLatticeKind::Constant ||
                    it->second.constant == nullptr) {
                    continue;
                }
                instruction->replace_all_uses_with(it->second.constant);
                function_changed = true;
            }
        }

        if (function_changed) {
            build_result->invalidate_core_ir_analyses(*function);
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
