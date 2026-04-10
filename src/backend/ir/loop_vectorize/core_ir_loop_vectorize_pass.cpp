#include "backend/ir/loop_vectorize/core_ir_loop_vectorize_pass.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr std::size_t kVectorWidth = 4;
constexpr std::size_t kVectorInterleaveFactor = 4;

using sysycc::detail::erase_instruction;
using sysycc::detail::insert_instruction_before;

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

bool value_is_loop_invariant(const CoreIrLoopInfo &loop, CoreIrValue *value) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    return instruction == nullptr || !loop_contains_block(loop, instruction->get_parent());
}

bool value_is_block_pair_invariant(CoreIrBasicBlock *header, CoreIrBasicBlock *body,
                                   CoreIrValue *value) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    CoreIrBasicBlock *parent = instruction->get_parent();
    return parent != nullptr && parent != header && parent != body;
}

const CoreIrIntegerType *as_i32_type(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    return integer_type != nullptr && integer_type->get_bit_width() == 32
               ? integer_type
               : nullptr;
}

std::size_t get_vector_alignment_bytes(const CoreIrType *type) {
    const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
    if (vector_type == nullptr) {
        return 0;
    }
    const auto *element_type =
        dynamic_cast<const CoreIrIntegerType *>(vector_type->get_element_type());
    if (element_type != nullptr && element_type->get_bit_width() == 32 &&
        vector_type->get_element_count() == kVectorWidth) {
        return 16;
    }
    return 0;
}

struct AccessInfo {
    CoreIrValue *root_base = nullptr;
    std::vector<CoreIrValue *> prefix_indices;
    CoreIrInstruction *address_instruction = nullptr;
};

CoreIrBasicBlock *insert_new_block_before(CoreIrFunction &function,
                                          CoreIrBasicBlock *anchor,
                                          std::unique_ptr<CoreIrBasicBlock> block) {
    if (anchor == nullptr || block == nullptr) {
        return nullptr;
    }
    block->set_parent(&function);
    CoreIrBasicBlock *block_ptr = block.get();
    auto &blocks = function.get_basic_blocks();
    auto it = std::find_if(blocks.begin(), blocks.end(),
                           [anchor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
                               return candidate.get() == anchor;
                           });
    blocks.insert(it, std::move(block));
    return block_ptr;
}

bool redirect_successor_edge(CoreIrBasicBlock &block, CoreIrBasicBlock *from,
                             CoreIrBasicBlock *to) {
    if (block.get_instructions().empty()) {
        return false;
    }
    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        if (jump->get_target_block() == from) {
            jump->set_target_block(to);
            return true;
        }
        return false;
    }
    auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return false;
    }
    bool changed = false;
    if (cond_jump->get_true_block() == from) {
        cond_jump->set_true_block(to);
        changed = true;
    }
    if (cond_jump->get_false_block() == from) {
        cond_jump->set_false_block(to);
        changed = true;
    }
    return changed;
}

std::string make_unique_block_name(const CoreIrFunction &function,
                                   const std::string &base_name) {
    auto is_used = [&function](const std::string &name) {
        for (const auto &block_ptr : function.get_basic_blocks()) {
            if (block_ptr != nullptr && block_ptr->get_name() == name) {
                return true;
            }
        }
        return false;
    };

    if (!is_used(base_name)) {
        return base_name;
    }
    for (std::size_t suffix = 1;; ++suffix) {
        std::string candidate = base_name + "." + std::to_string(suffix);
        if (!is_used(candidate)) {
            return candidate;
        }
    }
}

CoreIrBasicBlock *get_loop_outside_predecessor(const CoreIrLoopInfo &loop,
                                               const CoreIrCfgAnalysisResult &cfg) {
    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr) {
        return nullptr;
    }
    CoreIrBasicBlock *outside_pred = nullptr;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(header)) {
        if (pred != nullptr && !loop_contains_block(loop, pred)) {
            if (outside_pred != nullptr) {
                return nullptr;
            }
            outside_pred = pred;
        }
    }
    return outside_pred;
}

bool match_iv_gep(CoreIrValue *address, CoreIrPhiInst *iv, AccessInfo &info) {
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(address);
    if (gep == nullptr || iv == nullptr) {
        return false;
    }

    std::vector<CoreIrValue *> indices;
    if (!detail::collect_structural_gep_chain(*gep, info.root_base, indices) ||
        indices.empty() || indices.back() != iv) {
        return false;
    }
    for (std::size_t index = 0; index + 1 < indices.size(); ++index) {
        if (dynamic_cast<CoreIrInstruction *>(indices[index]) != nullptr) {
            return false;
        }
        info.prefix_indices.push_back(indices[index]);
    }
    info.address_instruction = gep;
    return true;
}

CoreIrConstantAggregate *make_splat_vector_constant(CoreIrContext &context,
                                                    const CoreIrType *vector_type,
                                                    const CoreIrConstantInt *scalar) {
    if (vector_type == nullptr || scalar == nullptr) {
        return nullptr;
    }
    const auto *vec_type = static_cast<const CoreIrVectorType *>(vector_type);
    std::vector<const CoreIrConstant *> elements(vec_type->get_element_count(), scalar);
    return context.create_constant<CoreIrConstantAggregate>(vector_type,
                                                            std::move(elements));
}

struct StoreLoopPattern {
    CoreIrBasicBlock *header = nullptr;
    CoreIrBasicBlock *body = nullptr;
    CoreIrBasicBlock *preheader = nullptr;
    CoreIrBasicBlock *exit_block = nullptr;
    CoreIrPhiInst *iv = nullptr;
    CoreIrInstruction *iv_next = nullptr;
    CoreIrStoreInst *store = nullptr;
    CoreIrLoadInst *accumulator_load = nullptr;
    CoreIrLoadInst *lane_load = nullptr;
    CoreIrLoadInst *scalar_load = nullptr;
    CoreIrValue *scalar_value = nullptr;
    const CoreIrConstantInt *scalar_constant = nullptr;
    CoreIrBinaryInst *final_binary = nullptr;
    CoreIrBinaryInst *mul_binary = nullptr;
    AccessInfo store_access;
    AccessInfo lane_access;
};

struct ReductionLoopPattern {
    CoreIrBasicBlock *body = nullptr;
    CoreIrBasicBlock *exit_block = nullptr;
    CoreIrBasicBlock *header = nullptr;
    CoreIrBasicBlock *preheader = nullptr;
    CoreIrPhiInst *iv = nullptr;
    CoreIrPhiInst *reduction_phi = nullptr;
    CoreIrInstruction *iv_next = nullptr;
    const CoreIrConstantInt *initial_zero = nullptr;
    CoreIrLoadInst *lhs_load = nullptr;
    CoreIrLoadInst *rhs_load = nullptr;
    CoreIrBinaryInst *mul = nullptr;
    CoreIrBinaryInst *reduction_update = nullptr;
    AccessInfo lhs_access;
    AccessInfo rhs_access;
};

struct RuntimeReductionPattern {
    CoreIrBasicBlock *header = nullptr;
    CoreIrBasicBlock *body = nullptr;
    CoreIrBasicBlock *preheader = nullptr;
    CoreIrBasicBlock *exit_block = nullptr;
    CoreIrPhiInst *iv = nullptr;
    CoreIrPhiInst *reduction_phi = nullptr;
    CoreIrInstruction *iv_next = nullptr;
    CoreIrBinaryInst *reduction_update = nullptr;
    CoreIrLoadInst *lane_load = nullptr;
    AccessInfo lane_access;
};

bool instruction_is_iv_increment(CoreIrInstruction *instruction, CoreIrPhiInst *iv) {
    auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
    if (binary == nullptr || iv == nullptr ||
        binary->get_binary_opcode() != CoreIrBinaryOpcode::Add) {
        return false;
    }
    const auto *lhs_constant =
        dynamic_cast<const CoreIrConstantInt *>(binary->get_lhs());
    const auto *rhs_constant =
        dynamic_cast<const CoreIrConstantInt *>(binary->get_rhs());
    return (binary->get_lhs() == iv && rhs_constant != nullptr &&
            rhs_constant->get_value() == 1) ||
           (binary->get_rhs() == iv && lhs_constant != nullptr &&
            lhs_constant->get_value() == 1);
}

bool match_runtime_iv_access(CoreIrBasicBlock *header, CoreIrBasicBlock *body,
                             CoreIrValue *address, CoreIrPhiInst *iv,
                             AccessInfo &info) {
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(address);
    if (gep == nullptr || iv == nullptr) {
        return false;
    }
    std::vector<CoreIrValue *> indices;
    if (!detail::collect_structural_gep_chain(*gep, info.root_base, indices) ||
        indices.empty() || indices.back() != iv) {
        return false;
    }
    for (std::size_t index = 0; index + 1 < indices.size(); ++index) {
        if (auto *instruction = dynamic_cast<CoreIrInstruction *>(indices[index]);
            instruction != nullptr &&
            (instruction->get_parent() == header || instruction->get_parent() == body)) {
            return false;
        }
        info.prefix_indices.push_back(indices[index]);
    }
    info.address_instruction = gep;
    return true;
}

bool match_store_loop_operations(CoreIrBasicBlock *header, CoreIrBasicBlock *body,
                                 StoreLoopPattern &pattern) {
    if (header == nullptr || body == nullptr || pattern.iv == nullptr) {
        return false;
    }

    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().empty() ? nullptr
                                           : header->get_instructions().back().get());
    auto *header_compare = header_branch == nullptr
                               ? nullptr
                               : dynamic_cast<CoreIrCompareInst *>(
                                     header_branch->get_condition());
    if (header_compare == nullptr ||
        (header_compare->get_lhs() != pattern.iv &&
         header_compare->get_rhs() != pattern.iv)) {
        return false;
    }

    std::vector<CoreIrInstruction *> instructions;
    std::vector<CoreIrLoadInst *> loads;
    for (const auto &instruction_ptr : body->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction != nullptr && !instruction->get_is_terminator()) {
            instructions.push_back(instruction);
        }
    }

    for (CoreIrInstruction *instruction : instructions) {
        auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
        if (store != nullptr) {
            pattern.store = store;
        }
        if (instruction_is_iv_increment(instruction, pattern.iv)) {
            pattern.iv_next = instruction;
            continue;
        }
        if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
            binary != nullptr) {
            if (binary->get_binary_opcode() == CoreIrBinaryOpcode::Mul) {
                pattern.mul_binary = binary;
            } else {
                pattern.final_binary = binary;
            }
        }
        if (load != nullptr) {
            loads.push_back(load);
        }
    }

    if (pattern.store == nullptr || pattern.final_binary == nullptr ||
        pattern.iv_next == nullptr || pattern.store->get_address() == nullptr ||
        !match_iv_gep(pattern.store->get_address(), pattern.iv, pattern.store_access)) {
        return false;
    }

    pattern.accumulator_load = nullptr;
    for (CoreIrLoadInst *load : loads) {
        if (load == nullptr) {
            continue;
        }
        if (pattern.store != nullptr &&
            load->get_address() == pattern.store->get_address()) {
            pattern.accumulator_load = load;
            continue;
        }
        AccessInfo tmp_access;
        if (load->get_address() != nullptr &&
            match_iv_gep(load->get_address(), pattern.iv, tmp_access)) {
            pattern.lane_load = load;
            pattern.lane_access = std::move(tmp_access);
            continue;
        }
        if (value_is_block_pair_invariant(header, body, load->get_address())) {
            pattern.scalar_load = load;
        }
    }

    if (pattern.accumulator_load == nullptr) {
        return false;
    }

    CoreIrValue *lhs = pattern.final_binary->get_lhs();
    CoreIrValue *rhs = pattern.final_binary->get_rhs();
    if (lhs == pattern.accumulator_load && rhs == pattern.lane_load &&
        pattern.lane_load != nullptr) {
        return true;
    }
    if (rhs == pattern.accumulator_load && lhs == pattern.lane_load &&
        pattern.lane_load != nullptr) {
        return true;
    }

    if (pattern.mul_binary == nullptr) {
        return false;
    }

    CoreIrValue *mul_value = nullptr;
    if (lhs == pattern.accumulator_load && pattern.mul_binary != nullptr) {
        mul_value = rhs;
    } else if (rhs == pattern.accumulator_load && pattern.mul_binary != nullptr) {
        mul_value = lhs;
    }
    if (mul_value != pattern.mul_binary) {
        return false;
    }

    CoreIrValue *mul_lhs = pattern.mul_binary->get_lhs();
    CoreIrValue *mul_rhs = pattern.mul_binary->get_rhs();
    if (mul_lhs == pattern.lane_load &&
        dynamic_cast<const CoreIrConstantInt *>(mul_rhs) != nullptr) {
        pattern.scalar_constant = static_cast<const CoreIrConstantInt *>(mul_rhs);
        return true;
    }
    if (mul_rhs == pattern.lane_load &&
        dynamic_cast<const CoreIrConstantInt *>(mul_lhs) != nullptr) {
        pattern.scalar_constant = static_cast<const CoreIrConstantInt *>(mul_lhs);
        return true;
    }
    if ((mul_lhs == pattern.lane_load && pattern.scalar_load != nullptr &&
         mul_rhs == pattern.scalar_load) ||
        (mul_rhs == pattern.lane_load && pattern.scalar_load != nullptr &&
         mul_lhs == pattern.scalar_load)) {
        pattern.scalar_value = pattern.scalar_load;
        return true;
    }
    if (mul_lhs == pattern.lane_load) {
        if (auto *invariant_load = dynamic_cast<CoreIrLoadInst *>(mul_rhs);
            invariant_load != nullptr &&
            value_is_block_pair_invariant(header, body, invariant_load)) {
            pattern.scalar_load = invariant_load;
            pattern.scalar_value = invariant_load;
            return true;
        }
    }
    if (mul_rhs == pattern.lane_load) {
        if (auto *invariant_load = dynamic_cast<CoreIrLoadInst *>(mul_lhs);
            invariant_load != nullptr &&
            value_is_block_pair_invariant(header, body, invariant_load)) {
            pattern.scalar_load = invariant_load;
            pattern.scalar_value = invariant_load;
            return true;
        }
    }
    if (mul_lhs == pattern.lane_load &&
        value_is_block_pair_invariant(header, body, mul_rhs)) {
        pattern.scalar_value = mul_rhs;
        return true;
    }
    if (mul_rhs == pattern.lane_load &&
        value_is_block_pair_invariant(header, body, mul_lhs)) {
        pattern.scalar_value = mul_lhs;
        return true;
    }
    return false;
}

bool match_runtime_store_loop_pattern(const CoreIrCfgAnalysisResult &cfg,
                                      CoreIrBasicBlock &header,
                                      StoreLoopPattern &pattern) {
    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header.get_instructions().empty() ? nullptr
                                          : header.get_instructions().back().get());
    if (header_branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *body = nullptr;
    CoreIrBasicBlock *exit_block = nullptr;
    auto body_is_latch = [&header](CoreIrBasicBlock *candidate) {
        if (candidate == nullptr || candidate->get_instructions().empty()) {
            return false;
        }
        auto *jump = dynamic_cast<CoreIrJumpInst *>(
            candidate->get_instructions().back().get());
        return jump != nullptr && jump->get_target_block() == &header;
    };
    if (body_is_latch(header_branch->get_true_block())) {
        body = header_branch->get_true_block();
        exit_block = header_branch->get_false_block();
    } else if (body_is_latch(header_branch->get_false_block())) {
        body = header_branch->get_false_block();
        exit_block = header_branch->get_true_block();
    } else {
        return false;
    }

    CoreIrBasicBlock *outside_pred = nullptr;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(&header)) {
        if (pred != nullptr && pred != body) {
            if (outside_pred != nullptr) {
                return false;
            }
            outside_pred = pred;
        }
    }
    auto *iv = dynamic_cast<CoreIrPhiInst *>(
        header.get_instructions().empty() ? nullptr
                                          : header.get_instructions().front().get());
    if (body == nullptr || exit_block == nullptr || outside_pred == nullptr ||
        iv == nullptr || iv->get_incoming_count() != 2) {
        return false;
    }

    bool saw_body_incoming = false;
    bool saw_outside_incoming = false;
    for (std::size_t index = 0; index < iv->get_incoming_count(); ++index) {
        CoreIrBasicBlock *incoming_block = iv->get_incoming_block(index);
        saw_body_incoming = saw_body_incoming || incoming_block == body;
        saw_outside_incoming = saw_outside_incoming || incoming_block == outside_pred;
    }
    if (!saw_body_incoming || !saw_outside_incoming) {
        return false;
    }

    pattern.header = &header;
    pattern.body = body;
    pattern.preheader = outside_pred;
    pattern.exit_block = exit_block;
    pattern.iv = iv;
    return match_store_loop_operations(&header, body, pattern);
}

bool match_loop_shape(const CoreIrLoopInfo &loop,
                      const CoreIrCanonicalInductionVarInfo &iv_info,
                      std::uint64_t trip_count, CoreIrBasicBlock *&body,
                      CoreIrBasicBlock *&exit_block) {
    if (trip_count == 0 || trip_count % kVectorWidth != 0 || iv_info.phi == nullptr ||
        iv_info.step != 1 || loop.get_blocks().size() != 2 ||
        loop.get_latches().size() != 1) {
        return false;
    }
    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr || iv_info.latch == nullptr) {
        return false;
    }
    body = iv_info.latch;
    if (body == header || body->get_instructions().empty()) {
        return false;
    }
    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    auto *body_jump = dynamic_cast<CoreIrJumpInst *>(
        body->get_instructions().back().get());
    if (header_branch == nullptr || body_jump == nullptr ||
        body_jump->get_target_block() != header) {
        return false;
    }
    if (loop_contains_block(loop, header_branch->get_true_block()) &&
        !loop_contains_block(loop, header_branch->get_false_block())) {
        exit_block = header_branch->get_false_block();
    } else if (!loop_contains_block(loop, header_branch->get_true_block()) &&
               loop_contains_block(loop, header_branch->get_false_block())) {
        exit_block = header_branch->get_true_block();
    } else {
        return false;
    }
    return exit_block != nullptr;
}

bool match_store_loop_pattern(const CoreIrLoopInfo &loop,
                              const CoreIrCfgAnalysisResult &cfg,
                              const CoreIrCanonicalInductionVarInfo &iv_info,
                              std::uint64_t trip_count, StoreLoopPattern &pattern) {
    if (!match_loop_shape(loop, iv_info, trip_count, pattern.body,
                          pattern.exit_block)) {
        return false;
    }
    pattern.header = loop.get_header();
    pattern.preheader = loop.get_preheader();
    if (pattern.preheader == nullptr) {
        pattern.preheader = get_loop_outside_predecessor(loop, cfg);
    }
    pattern.iv = iv_info.phi;

    std::vector<CoreIrInstruction *> instructions;
    std::vector<CoreIrLoadInst *> loads;
    for (const auto &instruction_ptr : pattern.body->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction != nullptr && !instruction->get_is_terminator()) {
            instructions.push_back(instruction);
        }
    }

    for (CoreIrInstruction *instruction : instructions) {
        auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
        if (store != nullptr) {
            pattern.store = store;
        }
        if (auto *next = dynamic_cast<CoreIrBinaryInst *>(instruction);
            next != nullptr && next == iv_info.latch_update) {
            pattern.iv_next = next;
            continue;
        }
        if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
            binary != nullptr) {
            if (binary->get_binary_opcode() == CoreIrBinaryOpcode::Mul) {
                pattern.mul_binary = binary;
            } else {
                pattern.final_binary = binary;
            }
        }
        if (load != nullptr) {
            loads.push_back(load);
        }
    }

    if (pattern.store == nullptr || pattern.final_binary == nullptr ||
        pattern.iv_next == nullptr || pattern.accumulator_load == nullptr ||
        pattern.store->get_address() == nullptr ||
        !match_iv_gep(pattern.store->get_address(), pattern.iv, pattern.store_access)) {
        pattern.accumulator_load = nullptr;
        for (CoreIrLoadInst *load : loads) {
            if (load == nullptr) {
                continue;
            }
            if (pattern.store != nullptr &&
                load->get_address() == pattern.store->get_address()) {
                pattern.accumulator_load = load;
                continue;
            }
            AccessInfo tmp_access;
            if (load->get_address() != nullptr &&
                match_iv_gep(load->get_address(), pattern.iv, tmp_access)) {
                pattern.lane_load = load;
                pattern.lane_access = std::move(tmp_access);
                continue;
            }
            if (value_is_loop_invariant(loop, load->get_address())) {
                pattern.scalar_load = load;
            }
        }
        if (pattern.store == nullptr || pattern.final_binary == nullptr ||
            pattern.iv_next == nullptr || pattern.accumulator_load == nullptr ||
            pattern.store->get_address() == nullptr ||
            !match_iv_gep(pattern.store->get_address(), pattern.iv,
                          pattern.store_access)) {
            return false;
        }
    }

    CoreIrValue *lhs = pattern.final_binary->get_lhs();
    CoreIrValue *rhs = pattern.final_binary->get_rhs();
    if (lhs == pattern.accumulator_load && rhs == pattern.lane_load &&
        pattern.lane_load != nullptr) {
        return true;
    }
    if (rhs == pattern.accumulator_load && lhs == pattern.lane_load &&
        pattern.lane_load != nullptr) {
        return true;
    }

    CoreIrValue *mul_value = nullptr;
    if (lhs == pattern.accumulator_load && pattern.mul_binary != nullptr) {
        mul_value = rhs;
    } else if (rhs == pattern.accumulator_load && pattern.mul_binary != nullptr) {
        mul_value = lhs;
    }
    if (mul_value != pattern.mul_binary) {
        return false;
    }

    CoreIrValue *mul_lhs = pattern.mul_binary->get_lhs();
    CoreIrValue *mul_rhs = pattern.mul_binary->get_rhs();
    if (mul_lhs == pattern.lane_load &&
        dynamic_cast<const CoreIrConstantInt *>(mul_rhs) != nullptr) {
        pattern.scalar_constant = static_cast<const CoreIrConstantInt *>(mul_rhs);
        return true;
    }
    if (mul_rhs == pattern.lane_load &&
        dynamic_cast<const CoreIrConstantInt *>(mul_lhs) != nullptr) {
        pattern.scalar_constant = static_cast<const CoreIrConstantInt *>(mul_lhs);
        return true;
    }
    if ((mul_lhs == pattern.lane_load && pattern.scalar_load != nullptr &&
         mul_rhs == pattern.scalar_load) ||
        (mul_rhs == pattern.lane_load && pattern.scalar_load != nullptr &&
         mul_lhs == pattern.scalar_load)) {
        pattern.scalar_value = pattern.scalar_load;
        return true;
    }
    if (mul_lhs == pattern.lane_load) {
        if (auto *invariant_load = dynamic_cast<CoreIrLoadInst *>(mul_rhs);
            invariant_load != nullptr && value_is_loop_invariant(loop, invariant_load)) {
            pattern.scalar_load = invariant_load;
            pattern.scalar_value = invariant_load;
            return true;
        }
    }
    if (mul_rhs == pattern.lane_load) {
        if (auto *invariant_load = dynamic_cast<CoreIrLoadInst *>(mul_lhs);
            invariant_load != nullptr && value_is_loop_invariant(loop, invariant_load)) {
            pattern.scalar_load = invariant_load;
            pattern.scalar_value = invariant_load;
            return true;
        }
    }
    if (mul_lhs == pattern.lane_load && value_is_loop_invariant(loop, mul_rhs)) {
        pattern.scalar_value = mul_rhs;
        return true;
    }
    if (mul_rhs == pattern.lane_load && value_is_loop_invariant(loop, mul_lhs)) {
        pattern.scalar_value = mul_lhs;
        return true;
    }
    return false;
}

bool match_reduction_loop_pattern(const CoreIrLoopInfo &loop,
                                  const CoreIrCanonicalInductionVarInfo &iv_info,
                                  std::uint64_t trip_count,
                                  ReductionLoopPattern &pattern) {
    if (!match_loop_shape(loop, iv_info, trip_count, pattern.body,
                          pattern.exit_block)) {
        return false;
    }
    pattern.iv = iv_info.phi;

    CoreIrBasicBlock *header = loop.get_header();
    for (const auto &instruction_ptr : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi != iv_info.phi && phi->get_incoming_count() == 2) {
            pattern.reduction_phi = phi;
            if (auto *initial =
                    dynamic_cast<const CoreIrConstantInt *>(phi->get_incoming_value(0));
                initial != nullptr && initial->get_value() == 0) {
                pattern.initial_zero = initial;
            } else if (auto *initial =
                           dynamic_cast<const CoreIrConstantInt *>(phi->get_incoming_value(1));
                       initial != nullptr && initial->get_value() == 0) {
                pattern.initial_zero = initial;
            }
        }
    }
    if (pattern.reduction_phi == nullptr || pattern.initial_zero == nullptr) {
        return false;
    }

    for (const auto &instruction_ptr : pattern.body->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            load != nullptr && pattern.lhs_load == nullptr) {
            pattern.lhs_load = load;
        } else if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
                   load != nullptr) {
            pattern.rhs_load = load;
        }
        if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
            binary != nullptr && binary->get_binary_opcode() == CoreIrBinaryOpcode::Mul) {
            pattern.mul = binary;
        } else if (auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
                   binary != nullptr && binary->get_binary_opcode() == CoreIrBinaryOpcode::Add &&
                   (binary->get_lhs() == pattern.reduction_phi ||
                    binary->get_rhs() == pattern.reduction_phi)) {
            pattern.reduction_update = binary;
        }
        if (instruction == iv_info.latch_update) {
            pattern.iv_next = instruction;
        }
    }

    if (pattern.lhs_load == nullptr || pattern.rhs_load == nullptr ||
        pattern.mul == nullptr || pattern.reduction_update == nullptr ||
        pattern.iv_next == nullptr ||
        !match_iv_gep(pattern.lhs_load->get_address(), pattern.iv, pattern.lhs_access) ||
        !match_iv_gep(pattern.rhs_load->get_address(), pattern.iv, pattern.rhs_access)) {
        return false;
    }
    return (pattern.mul->get_lhs() == pattern.lhs_load &&
            pattern.mul->get_rhs() == pattern.rhs_load) ||
           (pattern.mul->get_rhs() == pattern.lhs_load &&
            pattern.mul->get_lhs() == pattern.rhs_load);
}

bool match_runtime_add_reduction_loop_pattern(const CoreIrCfgAnalysisResult &cfg,
                                              CoreIrBasicBlock &header,
                                              RuntimeReductionPattern &pattern) {
    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header.get_instructions().empty() ? nullptr
                                          : header.get_instructions().back().get());
    if (header_branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *body = nullptr;
    CoreIrBasicBlock *exit_block = nullptr;
    auto body_is_latch = [&header](CoreIrBasicBlock *candidate) {
        if (candidate == nullptr || candidate->get_instructions().empty()) {
            return false;
        }
        auto *jump = dynamic_cast<CoreIrJumpInst *>(
            candidate->get_instructions().back().get());
        return jump != nullptr && jump->get_target_block() == &header;
    };
    if (body_is_latch(header_branch->get_true_block())) {
        body = header_branch->get_true_block();
        exit_block = header_branch->get_false_block();
    } else if (body_is_latch(header_branch->get_false_block())) {
        body = header_branch->get_false_block();
        exit_block = header_branch->get_true_block();
    } else {
        return false;
    }

    CoreIrBasicBlock *outside_pred = nullptr;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(&header)) {
        if (pred != nullptr && pred != body) {
            if (outside_pred != nullptr) {
                return false;
            }
            outside_pred = pred;
        }
    }
    if (outside_pred == nullptr) {
        return false;
    }

    auto *header_compare =
        dynamic_cast<CoreIrCompareInst *>(header_branch->get_condition());
    if (header_compare == nullptr) {
        return false;
    }

    pattern.header = &header;
    pattern.body = body;
    pattern.preheader = outside_pred;
    pattern.exit_block = exit_block;

    for (const auto &instruction_ptr : header.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if ((header_compare->get_lhs() == phi || header_compare->get_rhs() == phi) &&
            phi->get_incoming_count() == 2) {
            CoreIrValue *body_incoming = nullptr;
            CoreIrValue *outside_incoming = nullptr;
            for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
                if (phi->get_incoming_block(index) == body) {
                    body_incoming = phi->get_incoming_value(index);
                } else if (phi->get_incoming_block(index) == outside_pred) {
                    outside_incoming = phi->get_incoming_value(index);
                }
            }
            if (outside_incoming != nullptr &&
                instruction_is_iv_increment(
                    dynamic_cast<CoreIrInstruction *>(body_incoming), phi)) {
                pattern.iv = phi;
                pattern.iv_next = dynamic_cast<CoreIrInstruction *>(body_incoming);
                break;
            }
        }
    }
    if (pattern.iv == nullptr || pattern.iv_next == nullptr) {
        return false;
    }

    for (const auto &instruction_ptr : header.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi == pattern.iv || phi->get_incoming_count() != 2) {
            continue;
        }
        CoreIrValue *body_incoming = nullptr;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == body) {
                body_incoming = phi->get_incoming_value(index);
                break;
            }
        }
        auto *update = dynamic_cast<CoreIrBinaryInst *>(body_incoming);
        if (update != nullptr &&
            update->get_binary_opcode() == CoreIrBinaryOpcode::Add &&
            (update->get_lhs() == phi || update->get_rhs() == phi)) {
            pattern.reduction_phi = phi;
            pattern.reduction_update = update;
            break;
        }
    }
    if (pattern.reduction_phi == nullptr || pattern.reduction_update == nullptr) {
        return false;
    }

    for (const auto &instruction_ptr : body->get_instructions()) {
        auto *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            load != nullptr &&
            (pattern.reduction_update->get_lhs() == load ||
             pattern.reduction_update->get_rhs() == load)) {
            pattern.lane_load = load;
            break;
        }
    }
    if (pattern.lane_load == nullptr ||
        !match_runtime_iv_access(pattern.header, pattern.body,
                                 pattern.lane_load->get_address(), pattern.iv,
                                 pattern.lane_access)) {
        return false;
    }
    return true;
}

CoreIrValue *materialize_vector_load(CoreIrBasicBlock &body, CoreIrInstruction *anchor,
                                     CoreIrContext &context, const AccessInfo &access,
                                     CoreIrValue *index_value,
                                     const CoreIrType *vector_type,
                                     const std::string &name) {
    std::vector<CoreIrValue *> indices = access.prefix_indices;
    indices.push_back(index_value);
    auto *ptr_type = context.create_type<CoreIrPointerType>(
        static_cast<const CoreIrVectorType *>(vector_type)->get_element_type());
    auto gep = std::make_unique<CoreIrGetElementPtrInst>(ptr_type, name + ".addr",
                                                         access.root_base, std::move(indices));
    CoreIrInstruction *addr_inst =
        insert_instruction_before(body, anchor, std::move(gep));
    auto load = std::make_unique<CoreIrLoadInst>(
        vector_type, name, addr_inst, get_vector_alignment_bytes(vector_type));
    return insert_instruction_before(body, anchor, std::move(load));
}

CoreIrValue *materialize_vector_load(CoreIrBasicBlock &body, CoreIrInstruction *anchor,
                                     CoreIrContext &context, const AccessInfo &access,
                                     CoreIrPhiInst &iv, const CoreIrType *vector_type,
                                     const std::string &name) {
    return materialize_vector_load(body, anchor, context, access, &iv, vector_type,
                                   name);
}

CoreIrValue *materialize_broadcast_value(CoreIrBasicBlock &body,
                                         CoreIrInstruction *anchor,
                                         CoreIrContext &context,
                                         const CoreIrType *vector_type,
                                         CoreIrValue *scalar_value,
                                         const std::string &name) {
    const auto *vector_ty = static_cast<const CoreIrVectorType *>(vector_type);
    auto *i64_type = context.create_type<CoreIrIntegerType>(64);
    auto *zero = context.create_constant<CoreIrConstantInt>(i64_type, 0);
    std::vector<CoreIrValue *> mask_values;
    for (std::size_t index = 0; index < vector_ty->get_element_count(); ++index) {
        (void)index;
        mask_values.push_back(zero);
    }

    auto insert = std::make_unique<CoreIrInsertElementInst>(
        vector_type, name + ".splatinsert",
        context.create_constant<CoreIrConstantZeroInitializer>(vector_type),
        scalar_value, zero);
    CoreIrInstruction *insert_inst =
        insert_instruction_before(body, anchor, std::move(insert));
    auto shuffle = std::make_unique<CoreIrShuffleVectorInst>(
        vector_type, name + ".splat", insert_inst,
        context.create_constant<CoreIrConstantZeroInitializer>(vector_type),
        std::move(mask_values));
    return insert_instruction_before(body, anchor, std::move(shuffle));
}

CoreIrInstruction *materialize_prefixed_gep(CoreIrBasicBlock &block,
                                            CoreIrInstruction *anchor,
                                            CoreIrContext &context,
                                            const AccessInfo &access,
                                            const CoreIrType *pointer_type,
                                            const std::string &name) {
    std::vector<CoreIrValue *> indices = access.prefix_indices;
    auto gep = std::make_unique<CoreIrGetElementPtrInst>(pointer_type, name,
                                                         access.root_base,
                                                         std::move(indices));
    return insert_instruction_before(block, anchor, std::move(gep));
}

CoreIrValue *materialize_row_range_guard(CoreIrBasicBlock &guard_block,
                                         CoreIrInstruction *anchor,
                                         CoreIrContext &context,
                                         CoreIrValue *n, CoreIrInstruction *base_a,
                                         CoreIrInstruction *base_b,
                                         const std::string &prefix) {
    auto *i64_type = context.create_type<CoreIrIntegerType>(64);
    auto n64 = std::make_unique<CoreIrCastInst>(CoreIrCastKind::ZeroExtend, i64_type,
                                                prefix + ".n64", n);
    CoreIrInstruction *n64_inst =
        insert_instruction_before(guard_block, anchor, std::move(n64));
    auto *four = context.create_constant<CoreIrConstantInt>(i64_type, 4);
    auto bytes = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Mul, i64_type,
                                                    prefix + ".bytes", n64_inst, four);
    CoreIrInstruction *bytes_inst =
        insert_instruction_before(guard_block, anchor, std::move(bytes));
    auto ptr_a = std::make_unique<CoreIrCastInst>(CoreIrCastKind::PtrToInt, i64_type,
                                                  prefix + ".ptr.a", base_a);
    auto ptr_b = std::make_unique<CoreIrCastInst>(CoreIrCastKind::PtrToInt, i64_type,
                                                  prefix + ".ptr.b", base_b);
    CoreIrInstruction *ptr_a_inst =
        insert_instruction_before(guard_block, anchor, std::move(ptr_a));
    CoreIrInstruction *ptr_b_inst =
        insert_instruction_before(guard_block, anchor, std::move(ptr_b));
    auto end_a = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i64_type,
                                                    prefix + ".end.a", ptr_a_inst,
                                                    bytes_inst);
    auto end_b = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i64_type,
                                                    prefix + ".end.b", ptr_b_inst,
                                                    bytes_inst);
    CoreIrInstruction *end_a_inst =
        insert_instruction_before(guard_block, anchor, std::move(end_a));
    CoreIrInstruction *end_b_inst =
        insert_instruction_before(guard_block, anchor, std::move(end_b));
    auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    auto before_ab = std::make_unique<CoreIrCompareInst>(
        CoreIrComparePredicate::UnsignedLessEqual, i1_type, prefix + ".before.ab",
        end_a_inst, ptr_b_inst);
    auto before_ba = std::make_unique<CoreIrCompareInst>(
        CoreIrComparePredicate::UnsignedLessEqual, i1_type, prefix + ".before.ba",
        end_b_inst, ptr_a_inst);
    CoreIrInstruction *before_ab_inst =
        insert_instruction_before(guard_block, anchor, std::move(before_ab));
    CoreIrInstruction *before_ba_inst =
        insert_instruction_before(guard_block, anchor, std::move(before_ba));
    auto safe = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Or, i1_type,
                                                   prefix + ".safe", before_ab_inst,
                                                   before_ba_inst);
    return insert_instruction_before(guard_block, anchor, std::move(safe));
}

bool vectorize_runtime_mm_store_loop(CoreIrFunction &function,
                                     const CoreIrCfgAnalysisResult &cfg,
                                     StoreLoopPattern &pattern,
                                     CoreIrContext &context) {
    (void)cfg;
    if (pattern.preheader == nullptr || pattern.header == nullptr ||
        pattern.lane_load == nullptr ||
        pattern.mul_binary == nullptr || pattern.exit_block == nullptr) {
        return false;
    }
    if (pattern.scalar_load == nullptr && pattern.scalar_constant == nullptr &&
        pattern.scalar_value == nullptr) {
        return false;
    }
    auto *i32_type = as_i32_type(pattern.final_binary->get_type());
    if (i32_type == nullptr || pattern.iv == nullptr || pattern.iv_next == nullptr) {
        return false;
    }

    if (pattern.preheader->get_instructions().empty()) {
        return false;
    }

    auto guard_block = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.guard"));
    auto vector_header = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.header"));
    auto vector_body = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.body"));
    CoreIrBasicBlock *guard_block_ptr =
        insert_new_block_before(function, pattern.header, std::move(guard_block));
    CoreIrBasicBlock *vector_header_ptr =
        insert_new_block_before(function, pattern.header, std::move(vector_header));
    CoreIrBasicBlock *vector_body_ptr =
        insert_new_block_before(function, pattern.header, std::move(vector_body));
    if (guard_block_ptr == nullptr || vector_header_ptr == nullptr ||
        vector_body_ptr == nullptr) {
        return false;
    }

    redirect_successor_edge(*pattern.preheader, pattern.header, guard_block_ptr);
    for (const auto &instruction_ptr : pattern.header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == pattern.preheader) {
                phi->set_incoming_block(index, guard_block_ptr);
            }
        }
    }

    auto *void_type = context.create_type<CoreIrVoidType>();
    auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    auto *i64_type = context.create_type<CoreIrIntegerType>(64);
    auto *zero32 = context.create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *four32 = context.create_constant<CoreIrConstantInt>(i32_type, kVectorWidth);
    auto *fifteen32 = context.create_constant<CoreIrConstantInt>(
        i32_type, kVectorWidth * kVectorInterleaveFactor - 1);
    auto *sixteen32 = context.create_constant<CoreIrConstantInt>(
        i32_type, kVectorWidth * kVectorInterleaveFactor);

    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        pattern.header->get_instructions().empty()
            ? nullptr
            : pattern.header->get_instructions().back().get());
    auto *header_compare = header_branch == nullptr
                               ? nullptr
                               : dynamic_cast<CoreIrCompareInst *>(
                                     header_branch->get_condition());
    if (header_compare == nullptr) {
        return false;
    }
    CoreIrValue *n_value = header_compare->get_lhs() == pattern.iv
                               ? header_compare->get_rhs()
                               : header_compare->get_lhs();
    auto rem_inst = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::And, i32_type,
                                                       "vec.n.rem", n_value,
                                                       fifteen32);
    CoreIrInstruction *rem_value =
        insert_instruction_before(*guard_block_ptr, nullptr, std::move(rem_inst));
    auto vec_trip_inst = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "vec.n.trip", n_value, rem_value);
    CoreIrInstruction *vec_trip_value =
        insert_instruction_before(*guard_block_ptr, nullptr,
                                  std::move(vec_trip_inst));

    auto *ptr_i32 = context.create_type<CoreIrPointerType>(i32_type);
    CoreIrInstruction *c_row_base = materialize_prefixed_gep(
        *guard_block_ptr, nullptr, context, pattern.store_access, ptr_i32,
        "vec.c.row");
    CoreIrInstruction *b_row_base = materialize_prefixed_gep(
        *guard_block_ptr, nullptr, context, pattern.lane_access, ptr_i32,
        "vec.b.row");
    CoreIrValue *bc_safe = materialize_row_range_guard(*guard_block_ptr, nullptr, context,
                                                       n_value, c_row_base, b_row_base,
                                                       "vec.bc");

    CoreIrValue *alias_safe_value = bc_safe;
    if (pattern.scalar_load != nullptr && pattern.scalar_load->get_address() != nullptr) {
        auto ptr_scalar = std::make_unique<CoreIrCastInst>(
            CoreIrCastKind::PtrToInt, i64_type, "vec.a.ptr",
            pattern.scalar_load->get_address());
        CoreIrInstruction *ptr_scalar_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(ptr_scalar));
        auto ptr_c = std::make_unique<CoreIrCastInst>(CoreIrCastKind::PtrToInt, i64_type,
                                                      "vec.c.ptr", c_row_base);
        CoreIrInstruction *ptr_c_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(ptr_c));
        auto n64 = std::make_unique<CoreIrCastInst>(CoreIrCastKind::ZeroExtend, i64_type,
                                                    "vec.n64", n_value);
        CoreIrInstruction *n64_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(n64));
        auto *four64 = context.create_constant<CoreIrConstantInt>(i64_type, 4);
        auto c_bytes = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Mul, i64_type, "vec.c.bytes", n64_value, four64);
        CoreIrInstruction *c_bytes_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(c_bytes));
        auto c_end = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i64_type,
                                                        "vec.c.end", ptr_c_value,
                                                        c_bytes_value);
        CoreIrInstruction *c_end_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(c_end));
        auto scalar_before = std::make_unique<CoreIrCompareInst>(
            CoreIrComparePredicate::UnsignedLess, i1_type, "vec.a.before",
            ptr_scalar_value, ptr_c_value);
        auto scalar_after = std::make_unique<CoreIrCompareInst>(
            CoreIrComparePredicate::UnsignedGreaterEqual, i1_type, "vec.a.after",
            ptr_scalar_value, c_end_value);
        CoreIrInstruction *scalar_before_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(scalar_before));
        CoreIrInstruction *scalar_after_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(scalar_after));
        auto scalar_safe = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Or, i1_type, "vec.a.safe", scalar_before_value,
            scalar_after_value);
        CoreIrInstruction *scalar_safe_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(scalar_safe));
        auto alias_safe = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::And, i1_type, "vec.alias.safe", bc_safe,
            scalar_safe_value);
        alias_safe_value =
            insert_instruction_before(*guard_block_ptr, nullptr, std::move(alias_safe));
    } else if (pattern.scalar_value != nullptr &&
               dynamic_cast<CoreIrInstruction *>(pattern.scalar_value) != nullptr) {
        return false;
    }
    guard_block_ptr->append_instruction(
        std::make_unique<CoreIrCondJumpInst>(void_type, alias_safe_value,
                                             vector_header_ptr,
                                             pattern.header));

    auto *vec_type = context.create_type<CoreIrVectorType>(i32_type, kVectorWidth);
    auto *vec_iv = vector_header_ptr->create_instruction<CoreIrPhiInst>(i32_type, "vec.iv");
    auto *vec_cmp = vector_header_ptr->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "vec.cmp", vec_iv,
        vec_trip_value);
    vector_header_ptr->create_instruction<CoreIrCondJumpInst>(void_type, vec_cmp,
                                                              vector_body_ptr,
                                                              pattern.header);
    vec_iv->add_incoming(guard_block_ptr, zero32);

    CoreIrValue *hoisted_scalar_value = nullptr;
    if (pattern.scalar_load != nullptr) {
        CoreIrInstruction *guard_terminator =
            guard_block_ptr->get_instructions().empty()
                ? nullptr
                : guard_block_ptr->get_instructions().back().get();
        auto scalar_load = std::make_unique<CoreIrLoadInst>(
            pattern.scalar_load->get_type(), "vec.scalar",
            pattern.scalar_load->get_address());
        hoisted_scalar_value =
            insert_instruction_before(*guard_block_ptr, guard_terminator,
                                      std::move(scalar_load));
    } else if (pattern.scalar_value != nullptr) {
        hoisted_scalar_value = pattern.scalar_value;
    }

    for (const auto &instruction_ptr : pattern.header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        CoreIrValue *incoming_value = nullptr;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == guard_block_ptr) {
                incoming_value =
                    phi == pattern.iv ? static_cast<CoreIrValue *>(vec_iv)
                                      : phi->get_incoming_value(index);
                break;
            }
        }
        if (incoming_value != nullptr) {
            phi->add_incoming(vector_header_ptr, incoming_value);
        }
    }

    CoreIrValue *scalar_vec = hoisted_scalar_value != nullptr
                                  ? materialize_broadcast_value(*vector_body_ptr, nullptr,
                                                                context, vec_type,
                                                                hoisted_scalar_value,
                                                                "vec.broadcast")
                                  : static_cast<CoreIrValue *>(
                                        make_splat_vector_constant(context, vec_type,
                                                                   pattern.scalar_constant));
    for (std::size_t lane_group = 0; lane_group < kVectorInterleaveFactor;
         ++lane_group) {
        CoreIrValue *lane_index = vec_iv;
        if (lane_group != 0) {
            auto *lane_offset = context.create_constant<CoreIrConstantInt>(
                i32_type, lane_group * kVectorWidth);
            lane_index = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Add, i32_type,
                "vec.iv.offset." + std::to_string(lane_group), vec_iv,
                lane_offset);
        }

        CoreIrValue *acc_vec = materialize_vector_load(
            *vector_body_ptr, nullptr, context, pattern.store_access, lane_index,
            vec_type, "vec.acc." + std::to_string(lane_group));
        CoreIrValue *lane_vec = materialize_vector_load(
            *vector_body_ptr, nullptr, context, pattern.lane_access, lane_index,
            vec_type, "vec.lane." + std::to_string(lane_group));
        auto *vec_mul = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
            pattern.mul_binary->get_binary_opcode(), vec_type,
            "vec.mul." + std::to_string(lane_group), lane_vec, scalar_vec);
        auto *vec_add = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
            pattern.final_binary->get_binary_opcode(), vec_type,
            "vec.add." + std::to_string(lane_group), acc_vec, vec_mul);
        std::vector<CoreIrValue *> indices = pattern.store_access.prefix_indices;
        indices.push_back(lane_index);
        auto *vec_store_addr =
            vector_body_ptr->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32, "vec.store.addr." + std::to_string(lane_group),
                pattern.store_access.root_base, std::move(indices));
        vector_body_ptr->create_instruction<CoreIrStoreInst>(
            void_type, vec_add, vec_store_addr, get_vector_alignment_bytes(vec_type));
    }
    auto *vec_next = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "vec.iv.next", vec_iv, sixteen32);
    vector_body_ptr->create_instruction<CoreIrJumpInst>(void_type, vector_header_ptr);
    vec_iv->add_incoming(vector_body_ptr, vec_next);
    return true;
}

bool vectorize_runtime_store_loop_in_function(CoreIrFunction &function,
                                              const CoreIrCfgAnalysisResult &cfg,
                                              CoreIrContext &context) {
    (void)cfg;
    CoreIrCfgAnalysis cfg_analysis;
    bool changed = false;
    while (true) {
        CoreIrCfgAnalysisResult fresh_cfg = cfg_analysis.Run(function);
        bool transformed_this_round = false;
        for (const auto &block_ptr : function.get_basic_blocks()) {
            CoreIrBasicBlock *header = block_ptr.get();
            if (header == nullptr) {
                continue;
            }
            StoreLoopPattern pattern;
            if (!match_runtime_store_loop_pattern(fresh_cfg, *header, pattern)) {
                continue;
            }
            if (vectorize_runtime_mm_store_loop(function, fresh_cfg, pattern, context)) {
                transformed_this_round = true;
                changed = true;
                break;
            }
        }
        if (!transformed_this_round) {
            break;
        }
    }
    return changed;
}

bool vectorize_runtime_reduction_loop(CoreIrFunction &function,
                                      RuntimeReductionPattern &pattern,
                                      CoreIrContext &context) {
    if (pattern.preheader == nullptr || pattern.header == nullptr ||
        pattern.body == nullptr || pattern.exit_block == nullptr ||
        pattern.iv == nullptr || pattern.reduction_phi == nullptr ||
        pattern.iv_next == nullptr || pattern.lane_load == nullptr) {
        return false;
    }
    auto *i32_type = as_i32_type(pattern.reduction_phi->get_type());
    if (i32_type == nullptr) {
        return false;
    }

    auto guard_block = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.guard"));
    auto vector_header = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.header"));
    auto vector_body = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.body"));
    auto vector_exit = std::make_unique<CoreIrBasicBlock>(
        make_unique_block_name(function, "vector.exit"));
    CoreIrBasicBlock *guard_block_ptr =
        insert_new_block_before(function, pattern.header, std::move(guard_block));
    CoreIrBasicBlock *vector_header_ptr =
        insert_new_block_before(function, pattern.header, std::move(vector_header));
    CoreIrBasicBlock *vector_body_ptr =
        insert_new_block_before(function, pattern.header, std::move(vector_body));
    CoreIrBasicBlock *vector_exit_ptr =
        insert_new_block_before(function, pattern.header, std::move(vector_exit));
    if (guard_block_ptr == nullptr || vector_header_ptr == nullptr ||
        vector_body_ptr == nullptr || vector_exit_ptr == nullptr) {
        return false;
    }

    redirect_successor_edge(*pattern.preheader, pattern.header, guard_block_ptr);
    for (const auto &instruction_ptr : pattern.header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == pattern.preheader) {
                phi->set_incoming_block(index, guard_block_ptr);
            }
        }
    }

    auto *void_type = context.create_type<CoreIrVoidType>();
    auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    auto *zero32 = context.create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *fifteen32 = context.create_constant<CoreIrConstantInt>(
        i32_type, kVectorWidth * kVectorInterleaveFactor - 1);
    auto *sixteen32 = context.create_constant<CoreIrConstantInt>(
        i32_type, kVectorWidth * kVectorInterleaveFactor);
    auto *four32 = context.create_constant<CoreIrConstantInt>(i32_type, kVectorWidth);

    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        pattern.header->get_instructions().empty()
            ? nullptr
            : pattern.header->get_instructions().back().get());
    auto *header_compare = header_branch == nullptr
                               ? nullptr
                               : dynamic_cast<CoreIrCompareInst *>(
                                     header_branch->get_condition());
    if (header_compare == nullptr) {
        return false;
    }
    CoreIrValue *n_value = header_compare->get_lhs() == pattern.iv
                               ? header_compare->get_rhs()
                               : header_compare->get_lhs();
    auto rem_inst = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::And, i32_type,
                                                       "vec.n.rem", n_value,
                                                       fifteen32);
    CoreIrInstruction *rem_value =
        insert_instruction_before(*guard_block_ptr, nullptr, std::move(rem_inst));
    auto vec_trip_inst = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "vec.n.trip", n_value, rem_value);
    CoreIrInstruction *vec_trip_value =
        insert_instruction_before(*guard_block_ptr, nullptr,
                                  std::move(vec_trip_inst));
    auto can_run_inst = std::make_unique<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "vec.can.run",
        vec_trip_value, zero32);
    CoreIrInstruction *can_run_value =
        insert_instruction_before(*guard_block_ptr, nullptr, std::move(can_run_inst));
    guard_block_ptr->append_instruction(
        std::make_unique<CoreIrCondJumpInst>(void_type, can_run_value,
                                             vector_header_ptr, pattern.header));

    auto *vec_type = context.create_type<CoreIrVectorType>(i32_type, kVectorWidth);
    auto *zero = context.create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero_vec = context.create_constant<CoreIrConstantAggregate>(
        vec_type, std::vector<const CoreIrConstant *>(kVectorWidth, zero));

    auto *vec_iv = vector_header_ptr->create_instruction<CoreIrPhiInst>(i32_type, "vec.iv");
    std::array<CoreIrPhiInst *, kVectorInterleaveFactor> vec_accs{};
    for (std::size_t index = 0; index < kVectorInterleaveFactor; ++index) {
        vec_accs[index] = vector_header_ptr->create_instruction<CoreIrPhiInst>(
            vec_type, "vec.acc." + std::to_string(index));
    }
    auto *vec_cmp = vector_header_ptr->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "vec.cmp", vec_iv, vec_trip_value);
    vector_header_ptr->create_instruction<CoreIrCondJumpInst>(void_type, vec_cmp,
                                                              vector_body_ptr,
                                                              vector_exit_ptr);
    vec_iv->add_incoming(guard_block_ptr, zero32);

    CoreIrValue *initial_scalar = nullptr;
    for (std::size_t index = 0; index < pattern.reduction_phi->get_incoming_count();
         ++index) {
        if (pattern.reduction_phi->get_incoming_block(index) == guard_block_ptr) {
            initial_scalar = pattern.reduction_phi->get_incoming_value(index);
            break;
        }
    }
    if (initial_scalar == nullptr) {
        return false;
    }
    CoreIrInstruction *guard_terminator =
        guard_block_ptr->get_instructions().empty()
            ? nullptr
            : guard_block_ptr->get_instructions().back().get();
    auto *zero64 = context.create_constant<CoreIrConstantInt>(
        context.create_type<CoreIrIntegerType>(64), 0);
    auto init_insert = std::make_unique<CoreIrInsertElementInst>(
        vec_type, "vec.init",
        context.create_constant<CoreIrConstantZeroInitializer>(vec_type),
        initial_scalar, zero64);
    CoreIrValue *initial_vec = insert_instruction_before(
        *guard_block_ptr, guard_terminator, std::move(init_insert));
    vec_accs[0]->add_incoming(guard_block_ptr, initial_vec);
    for (std::size_t index = 1; index < kVectorInterleaveFactor; ++index) {
        vec_accs[index]->add_incoming(guard_block_ptr, zero_vec);
    }

    std::array<CoreIrValue *, kVectorInterleaveFactor> next_acc_values{};
    for (std::size_t lane_group = 0; lane_group < kVectorInterleaveFactor;
         ++lane_group) {
        CoreIrValue *lane_index = vec_iv;
        if (lane_group != 0) {
            auto *lane_offset = context.create_constant<CoreIrConstantInt>(
                i32_type, lane_group * kVectorWidth);
            lane_index = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Add, i32_type,
                "vec.iv.offset." + std::to_string(lane_group), vec_iv,
                lane_offset);
        }
        CoreIrValue *lane_vec = materialize_vector_load(
            *vector_body_ptr, nullptr, context, pattern.lane_access, lane_index,
            vec_type, "vec.lane." + std::to_string(lane_group));
        next_acc_values[lane_group] =
            vector_body_ptr->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, vec_type,
            "vec.acc.next." + std::to_string(lane_group), vec_accs[lane_group],
            lane_vec);
    }
    auto *vec_next = vector_body_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "vec.iv.next", vec_iv, sixteen32);
    vector_body_ptr->create_instruction<CoreIrJumpInst>(void_type, vector_header_ptr);
    vec_iv->add_incoming(vector_body_ptr, vec_next);
    for (std::size_t index = 0; index < kVectorInterleaveFactor; ++index) {
        vec_accs[index]->add_incoming(vector_body_ptr, next_acc_values[index]);
    }

    auto *vec_pair0 = vector_exit_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, vec_type, "vec.acc.merge.0", vec_accs[0],
        vec_accs[1]);
    auto *vec_pair1 = vector_exit_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, vec_type, "vec.acc.merge.1", vec_accs[2],
        vec_accs[3]);
    auto *vec_total = vector_exit_ptr->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, vec_type, "vec.acc.total", vec_pair0, vec_pair1);
    auto *reduce_inst = vector_exit_ptr->create_instruction<CoreIrVectorReduceAddInst>(
        i32_type, "vec.reduce", vec_total);
    vector_exit_ptr->create_instruction<CoreIrJumpInst>(void_type, pattern.header);

    pattern.iv->add_incoming(vector_exit_ptr, vec_iv);
    pattern.reduction_phi->add_incoming(vector_exit_ptr, reduce_inst);
    (void)four32;
    return true;
}

bool vectorize_runtime_reduction_loop_in_function(CoreIrFunction &function,
                                                  const CoreIrCfgAnalysisResult &cfg,
                                                  CoreIrContext &context) {
    (void)cfg;
    CoreIrCfgAnalysis cfg_analysis;
    bool changed = false;
    while (true) {
        CoreIrCfgAnalysisResult fresh_cfg = cfg_analysis.Run(function);
        bool transformed_this_round = false;
        for (const auto &block_ptr : function.get_basic_blocks()) {
            CoreIrBasicBlock *header = block_ptr.get();
            if (header == nullptr) {
                continue;
            }
            RuntimeReductionPattern pattern;
            if (!match_runtime_add_reduction_loop_pattern(fresh_cfg, *header, pattern)) {
                continue;
            }
            if (vectorize_runtime_reduction_loop(function, pattern, context)) {
                transformed_this_round = true;
                changed = true;
                break;
            }
        }
        if (!transformed_this_round) {
            break;
        }
    }
    return changed;
}

bool vectorize_store_loop(CoreIrFunction &function, StoreLoopPattern &pattern,
                          CoreIrContext &context) {
    auto *i32_type = as_i32_type(pattern.final_binary->get_type());
    if (i32_type == nullptr) {
        return false;
    }
    auto *vec_type = context.create_type<CoreIrVectorType>(i32_type, kVectorWidth);
    auto *step = context.create_constant<CoreIrConstantInt>(i32_type, kVectorWidth);

    pattern.body->set_name("vector.body");
    auto &instructions = pattern.body->get_instructions();
    for (std::size_t index = 0; index + 1 < instructions.size();) {
        if (instructions[index].get() == pattern.iv_next) {
            ++index;
            continue;
        }
        instructions[index]->detach_operands();
        instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(index));
    }

    CoreIrInstruction *anchor = pattern.iv_next;
    CoreIrValue *acc_vec = materialize_vector_load(*pattern.body, anchor, context,
                                                   pattern.store_access, *pattern.iv,
                                                   vec_type, "vec.acc");
    CoreIrValue *vec_result = nullptr;

    if (pattern.mul_binary != nullptr) {
        CoreIrValue *lane_vec = materialize_vector_load(*pattern.body, anchor, context,
                                                        pattern.lane_access, *pattern.iv,
                                                        vec_type, "vec.lane");
        CoreIrValue *scalar_vec = nullptr;
        if (pattern.scalar_value != nullptr) {
            scalar_vec = materialize_broadcast_value(*pattern.body, anchor, context,
                                                     vec_type, pattern.scalar_value,
                                                     "vec.broadcast");
        } else {
            scalar_vec = make_splat_vector_constant(context, vec_type,
                                                    pattern.scalar_constant);
        }
        auto vec_mul = std::make_unique<CoreIrBinaryInst>(
            pattern.mul_binary->get_binary_opcode(), vec_type, "vec.mul",
            lane_vec, scalar_vec);
        CoreIrInstruction *mul_inst =
            insert_instruction_before(*pattern.body, anchor, std::move(vec_mul));
        auto vec_add = std::make_unique<CoreIrBinaryInst>(
            pattern.final_binary->get_binary_opcode(), vec_type, "vec.add",
            acc_vec, mul_inst);
        vec_result =
            insert_instruction_before(*pattern.body, anchor, std::move(vec_add));
    } else {
        CoreIrValue *rhs_vec = nullptr;
        if (pattern.lane_load != nullptr) {
            rhs_vec = materialize_vector_load(*pattern.body, anchor, context,
                                              pattern.lane_access, *pattern.iv,
                                              vec_type, "vec.rhs");
        } else {
            rhs_vec = make_splat_vector_constant(context, vec_type,
                                                 pattern.scalar_constant);
        }
        auto vec_binary = std::make_unique<CoreIrBinaryInst>(
            pattern.final_binary->get_binary_opcode(), vec_type, "vec.op", acc_vec,
            rhs_vec);
        vec_result =
            insert_instruction_before(*pattern.body, anchor, std::move(vec_binary));
    }

    std::vector<CoreIrValue *> indices = pattern.store_access.prefix_indices;
    indices.push_back(pattern.iv);
    auto *ptr_type = context.create_type<CoreIrPointerType>(i32_type);
    auto vec_store_addr = std::make_unique<CoreIrGetElementPtrInst>(
        ptr_type, "vec.store.addr", pattern.store_access.root_base, std::move(indices));
    CoreIrInstruction *addr_inst =
        insert_instruction_before(*pattern.body, anchor, std::move(vec_store_addr));
    auto vec_store = std::make_unique<CoreIrStoreInst>(
        pattern.store->get_type(), vec_result, addr_inst,
        get_vector_alignment_bytes(vec_type));
    insert_instruction_before(*pattern.body, anchor, std::move(vec_store));
    pattern.iv_next->set_operand(1, step);
    (void)function;
    return true;
}

bool vectorize_reduction_loop(CoreIrFunction &function, ReductionLoopPattern &pattern,
                              CoreIrContext &context) {
    auto *i32_type = as_i32_type(pattern.reduction_phi->get_type());
    if (i32_type == nullptr) {
        return false;
    }
    auto *vec_type = context.create_type<CoreIrVectorType>(i32_type, kVectorWidth);
    auto *step = context.create_constant<CoreIrConstantInt>(i32_type, kVectorWidth);
    auto *zero = context.create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero_vec = context.create_constant<CoreIrConstantAggregate>(
        vec_type, std::vector<const CoreIrConstant *>(kVectorWidth, zero));

    CoreIrBasicBlock *header = function.get_basic_blocks().front().get();
    (void)header;
    pattern.body->set_name("vector.body");

    auto *vector_phi = pattern.body->get_parent()->get_basic_blocks()[1].get();
    (void)vector_phi;

    CoreIrBasicBlock *loop_header = nullptr;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr != nullptr &&
            block_ptr.get() != pattern.body &&
            block_ptr.get() != pattern.exit_block &&
            block_ptr->get_name().find("header") != std::string::npos) {
            loop_header = block_ptr.get();
            break;
        }
    }
    if (loop_header == nullptr) {
        return false;
    }

    auto *vec_acc = loop_header->insert_instruction_before_first_non_phi(
        std::make_unique<CoreIrPhiInst>(vec_type, "vec.acc"));
    auto *vec_acc_phi = dynamic_cast<CoreIrPhiInst *>(vec_acc);
    if (vec_acc_phi == nullptr) {
        return false;
    }
    vec_acc_phi->add_incoming(pattern.iv->get_incoming_block(0), zero_vec);

    auto &instructions = pattern.body->get_instructions();
    for (std::size_t index = 0; index + 1 < instructions.size();) {
        if (instructions[index].get() == pattern.iv_next) {
            ++index;
            continue;
        }
        instructions[index]->detach_operands();
        instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(index));
    }

    CoreIrInstruction *anchor = pattern.iv_next;
    CoreIrValue *lhs_vec = materialize_vector_load(*pattern.body, anchor, context,
                                                   pattern.lhs_access, *pattern.iv,
                                                   vec_type, "vec.lhs");
    CoreIrValue *rhs_vec = materialize_vector_load(*pattern.body, anchor, context,
                                                   pattern.rhs_access, *pattern.iv,
                                                   vec_type, "vec.rhs");
    auto vec_mul = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Mul, vec_type,
                                                      "vec.mul", lhs_vec, rhs_vec);
    CoreIrInstruction *mul_inst =
        insert_instruction_before(*pattern.body, anchor, std::move(vec_mul));
    auto vec_add = std::make_unique<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, vec_type,
                                                      "vec.acc.next", vec_acc_phi, mul_inst);
    CoreIrInstruction *vec_next =
        insert_instruction_before(*pattern.body, anchor, std::move(vec_add));
    vec_acc_phi->add_incoming(pattern.body, vec_next);
    pattern.iv_next->set_operand(1, step);

    auto *ret = dynamic_cast<CoreIrReturnInst *>(
        pattern.exit_block->get_instructions().back().get());
    if (ret == nullptr) {
        return false;
    }
    auto reduce = std::make_unique<CoreIrVectorReduceAddInst>(i32_type, "vec.reduce",
                                                              vec_acc_phi);
    CoreIrInstruction *reduce_inst =
        insert_instruction_before(*pattern.exit_block, ret, std::move(reduce));
    ret->set_operand(0, reduce_inst);
    return true;
}

} // namespace

PassKind CoreIrLoopVectorizePass::Kind() const {
    return PassKind::CoreIrLoopVectorize;
}

const char *CoreIrLoopVectorizePass::Name() const {
    return "CoreIrLoopVectorizePass";
}

PassResult CoreIrLoopVectorizePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    CoreIrContext *core_ir_context = build_result == nullptr ? nullptr : build_result->get_context();
    if (module == nullptr || core_ir_context == nullptr) {
        return fail_missing_core_ir(context, Name());
    }
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir loop vectorize dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const auto &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const auto &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const auto &induction =
            analysis_manager->get_or_compute<CoreIrInductionVarAnalysis>(*function);
        const auto &scev =
            analysis_manager->get_or_compute<CoreIrScalarEvolutionLiteAnalysis>(*function);
        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr == nullptr) {
                continue;
            }
            const auto *iv_info = induction.get_canonical_induction_var(*loop_ptr);
            if (iv_info == nullptr) {
                continue;
            }
            std::optional<std::uint64_t> trip_count =
                scev.get_constant_trip_count(*loop_ptr);

            StoreLoopPattern store_pattern;
            ReductionLoopPattern reduction_pattern;
            if (trip_count.has_value() &&
                match_store_loop_pattern(*loop_ptr, cfg, *iv_info, *trip_count,
                                         store_pattern)) {
                function_changed = vectorize_store_loop(*function, store_pattern,
                                                        *core_ir_context) ||
                                   function_changed;
            } else if (trip_count.has_value() &&
                       match_reduction_loop_pattern(*loop_ptr, *iv_info,
                                                    *trip_count,
                                                    reduction_pattern)) {
                function_changed =
                    vectorize_reduction_loop(*function, reduction_pattern,
                                             *core_ir_context) ||
                    function_changed;
            }
            if (function_changed) {
                // This pass rewrites CFG and loop shape directly. Stop after the
                // first successful transform so later iterations do not keep
                // walking stale loop/CFG/SCEV snapshots for the same function.
                break;
            }
        }
        if (!function_changed) {
            function_changed = vectorize_runtime_store_loop_in_function(
                *function, cfg, *core_ir_context);
        }
        function_changed =
            vectorize_runtime_reduction_loop_in_function(*function, cfg,
                                                         *core_ir_context) ||
            function_changed;
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
