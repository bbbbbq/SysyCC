#include "backend/ir/slp_vectorize/core_ir_slp_vectorize_pass.hpp"

#include <memory>
#include <optional>
#include <vector>

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

constexpr std::size_t kSlpWidth = 4;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

const CoreIrIntegerType *as_i32_type(const CoreIrType *type) {
    const auto *integer_type = dynamic_cast<const CoreIrIntegerType *>(type);
    return integer_type != nullptr && integer_type->get_bit_width() == 32
               ? integer_type
               : nullptr;
}

struct ScalarAccess {
    CoreIrValue *root_base = nullptr;
    std::vector<CoreIrValue *> prefix_indices;
    std::uint64_t lane = 0;
};

bool match_const_lane_gep(CoreIrValue *address, ScalarAccess &access) {
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(address);
    if (gep == nullptr) {
        return false;
    }
    std::vector<CoreIrValue *> indices;
    if (!detail::collect_structural_gep_chain(*gep, access.root_base, indices) ||
        indices.empty()) {
        return false;
    }
    const auto *lane_constant =
        dynamic_cast<const CoreIrConstantInt *>(indices.back());
    if (lane_constant == nullptr) {
        return false;
    }
    access.lane = lane_constant->get_value();
    for (std::size_t index = 0; index + 1 < indices.size(); ++index) {
        if (dynamic_cast<CoreIrInstruction *>(indices[index]) != nullptr) {
            return false;
        }
        access.prefix_indices.push_back(indices[index]);
    }
    return true;
}

struct SlpLane {
    CoreIrStoreInst *store = nullptr;
    CoreIrBinaryInst *binary = nullptr;
    CoreIrLoadInst *lhs_load = nullptr;
    CoreIrLoadInst *rhs_load = nullptr;
    ScalarAccess store_access;
    ScalarAccess lhs_access;
    ScalarAccess rhs_access;
};

bool match_slp_lane(CoreIrInstruction *instruction, SlpLane &lane) {
    lane.store = dynamic_cast<CoreIrStoreInst *>(instruction);
    if (lane.store == nullptr || lane.store->get_address() == nullptr ||
        !match_const_lane_gep(lane.store->get_address(), lane.store_access)) {
        return false;
    }
    lane.binary = dynamic_cast<CoreIrBinaryInst *>(lane.store->get_value());
    if (lane.binary == nullptr) {
        return false;
    }
    lane.lhs_load = dynamic_cast<CoreIrLoadInst *>(lane.binary->get_lhs());
    lane.rhs_load = dynamic_cast<CoreIrLoadInst *>(lane.binary->get_rhs());
    if (lane.lhs_load == nullptr || lane.rhs_load == nullptr ||
        lane.lhs_load->get_address() == nullptr ||
        lane.rhs_load->get_address() == nullptr ||
        !match_const_lane_gep(lane.lhs_load->get_address(), lane.lhs_access) ||
        !match_const_lane_gep(lane.rhs_load->get_address(), lane.rhs_access)) {
        return false;
    }
    return true;
}

bool lanes_form_pack(const std::vector<SlpLane> &lanes) {
    if (lanes.size() != kSlpWidth) {
        return false;
    }
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        if (lanes[lane].store_access.lane != lane ||
            lanes[lane].lhs_access.lane != lane ||
            lanes[lane].rhs_access.lane != lane) {
            return false;
        }
    }
    const auto &first = lanes.front();
    for (const SlpLane &lane : lanes) {
        if (lane.binary == nullptr || lane.binary->get_binary_opcode() !=
                                        first.binary->get_binary_opcode() ||
            lane.store_access.root_base != first.store_access.root_base ||
            lane.lhs_access.root_base != first.lhs_access.root_base ||
            lane.rhs_access.root_base != first.rhs_access.root_base ||
            lane.store_access.prefix_indices != first.store_access.prefix_indices ||
            lane.lhs_access.prefix_indices != first.lhs_access.prefix_indices ||
            lane.rhs_access.prefix_indices != first.rhs_access.prefix_indices) {
            return false;
        }
    }
    return true;
}

bool instruction_has_only_lane_uses(CoreIrInstruction &instruction,
                                    const std::vector<SlpLane> &lanes) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        bool allowed = false;
        for (const SlpLane &lane : lanes) {
            if (user == lane.binary || user == lane.store) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            return false;
        }
    }
    return true;
}

bool vectorize_pack(CoreIrBasicBlock &block, std::size_t start_index,
                    const std::vector<SlpLane> &lanes, CoreIrContext &context) {
    auto *i32_type = as_i32_type(lanes.front().binary->get_type());
    if (i32_type == nullptr) {
        return false;
    }
    auto *vec_type = context.create_type<CoreIrVectorType>(i32_type, kSlpWidth);
    auto *ptr_type = context.create_type<CoreIrPointerType>(i32_type);
    auto *zero = context.create_constant<CoreIrConstantInt>(i32_type, 0);

    CoreIrInstruction *anchor = lanes.front().store;
    auto make_addr = [&](const char *name, const ScalarAccess &access) {
        std::vector<CoreIrValue *> indices = access.prefix_indices;
        indices.push_back(const_cast<CoreIrConstantInt *>(zero));
        auto gep = std::make_unique<CoreIrGetElementPtrInst>(ptr_type, name,
                                                             access.root_base,
                                                             std::move(indices));
        return detail::insert_instruction_before(block, anchor, std::move(gep));
    };

    CoreIrInstruction *lhs_addr = make_addr("slp.lhs.addr", lanes.front().lhs_access);
    CoreIrInstruction *rhs_addr = make_addr("slp.rhs.addr", lanes.front().rhs_access);
    CoreIrInstruction *dst_addr = make_addr("slp.dst.addr", lanes.front().store_access);

    auto lhs_vec = std::make_unique<CoreIrLoadInst>(vec_type, "slp.lhs", lhs_addr);
    CoreIrInstruction *lhs_load =
        detail::insert_instruction_before(block, anchor, std::move(lhs_vec));
    auto rhs_vec = std::make_unique<CoreIrLoadInst>(vec_type, "slp.rhs", rhs_addr);
    CoreIrInstruction *rhs_load =
        detail::insert_instruction_before(block, anchor, std::move(rhs_vec));
    auto vec_binary = std::make_unique<CoreIrBinaryInst>(
        lanes.front().binary->get_binary_opcode(), vec_type, "slp.vec", lhs_load,
        rhs_load);
    CoreIrInstruction *vec_op =
        detail::insert_instruction_before(block, anchor, std::move(vec_binary));
    auto vec_store = std::make_unique<CoreIrStoreInst>(lanes.front().store->get_type(),
                                                       vec_op, dst_addr);
    detail::insert_instruction_before(block, anchor, std::move(vec_store));

    for (std::size_t offset = 0; offset < lanes.size(); ++offset) {
        std::size_t index = start_index;
        detail::erase_instruction(block, lanes[offset].store);
        detail::erase_instruction(block, lanes[offset].binary);
        detail::erase_instruction(block, lanes[offset].lhs_load);
        detail::erase_instruction(block, lanes[offset].rhs_load);
        (void)index;
    }
    return true;
}

bool run_slp_on_function(CoreIrFunction &function, CoreIrContext &context) {
    bool changed = false;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        std::vector<SlpLane> lanes;
        lanes.reserve(kSlpWidth);
        for (const auto &instruction_ptr : block->get_instructions()) {
            SlpLane lane;
            if (match_slp_lane(instruction_ptr.get(), lane)) {
                lanes.push_back(lane);
                if (lanes.size() == kSlpWidth) {
                    break;
                }
            }
        }
        if (lanes.size() != kSlpWidth || !lanes_form_pack(lanes)) {
            continue;
        }
        bool legal = true;
        for (const SlpLane &lane : lanes) {
            legal = legal &&
                    instruction_has_only_lane_uses(*lane.lhs_load, lanes) &&
                    instruction_has_only_lane_uses(*lane.rhs_load, lanes) &&
                    instruction_has_only_lane_uses(*lane.binary, lanes);
        }
        if (!legal) {
            continue;
        }
        if (vectorize_pack(*block, 0, lanes, context)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace

PassKind CoreIrSlpVectorizePass::Kind() const {
    return PassKind::CoreIrSlpVectorize;
}

const char *CoreIrSlpVectorizePass::Name() const {
    return "CoreIrSlpVectorizePass";
}

PassResult CoreIrSlpVectorizePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    CoreIrContext *core_ir_context = build_result == nullptr ? nullptr : build_result->get_context();
    if (module == nullptr || core_ir_context == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        if (function != nullptr && run_slp_on_function(*function, *core_ir_context)) {
            effects.changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
