#include "backend/ir/argument_promotion/core_ir_argument_promotion_pass.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::clone_instruction_remapped;
using sysycc::detail::insert_instruction_before;
using sysycc::detail::replace_instruction;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

struct PromotionCandidate {
    CoreIrFunction *function = nullptr;
    std::size_t parameter_index = 0;
    const CoreIrType *promoted_type = nullptr;
};

const CoreIrType *find_direct_load_only_parameter_type(CoreIrParameter &parameter) {
    const CoreIrType *loaded_type = nullptr;
    bool found_load = false;
    for (const CoreIrUse &use : parameter.get_uses()) {
        auto *load = dynamic_cast<CoreIrLoadInst *>(use.get_user());
        if (load == nullptr || load->get_address() != &parameter) {
            return nullptr;
        }
        found_load = true;
        if (loaded_type == nullptr) {
            loaded_type = load->get_type();
            continue;
        }
        if (loaded_type != load->get_type()) {
            return nullptr;
        }
    }
    return found_load ? loaded_type : nullptr;
}

bool function_is_arg_promotion_candidate(const CoreIrFunction &function,
                                         std::size_t parameter_index) {
    return function.get_is_internal_linkage() &&
           function.get_basic_blocks().size() == 1 &&
           function.get_stack_slots().empty() &&
           parameter_index < function.get_parameters().size();
}

std::unique_ptr<CoreIrFunction> clone_promoted_function(
    CoreIrContext &context, const CoreIrFunction &original,
    const PromotionCandidate &candidate, std::string promoted_name) {
    const CoreIrFunctionType *original_type = original.get_function_type();
    if (original_type == nullptr || candidate.promoted_type == nullptr) {
        return nullptr;
    }

    std::vector<const CoreIrType *> parameter_types =
        original_type->get_parameter_types();
    if (candidate.parameter_index >= parameter_types.size()) {
        return nullptr;
    }
    parameter_types[candidate.parameter_index] = candidate.promoted_type;
    const auto *promoted_function_type =
        context.create_type<CoreIrFunctionType>(original_type->get_return_type(),
                                               std::move(parameter_types),
                                               original_type->get_is_variadic());

    auto promoted = std::make_unique<CoreIrFunction>(
        std::move(promoted_name), promoted_function_type,
        original.get_is_internal_linkage(), original.get_is_always_inline());
    promoted->set_is_readnone(original.get_is_readnone());
    promoted->set_is_readonly(original.get_is_readonly());
    promoted->set_is_writeonly(original.get_is_writeonly());
    promoted->set_is_norecurse(original.get_is_norecurse());

    std::unordered_map<const CoreIrValue *, CoreIrValue *> value_map;
    for (std::size_t index = 0; index < original.get_parameters().size(); ++index) {
        CoreIrParameter *new_param = promoted->create_parameter<CoreIrParameter>(
            index == candidate.parameter_index ? candidate.promoted_type
                                               : original.get_parameters()[index]->get_type(),
            original.get_parameters()[index]->get_name());
        value_map.emplace(original.get_parameters()[index].get(), new_param);
    }

    CoreIrBasicBlock *original_block = original.get_basic_blocks().front().get();
    CoreIrBasicBlock *new_block = promoted->create_basic_block<CoreIrBasicBlock>(
        original_block == nullptr ? "entry" : original_block->get_name());
    if (original_block == nullptr) {
        return nullptr;
    }

    for (const auto &instruction_ptr : original_block->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr) {
            continue;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            load != nullptr && load->get_address() ==
                                   original.get_parameters()[candidate.parameter_index].get()) {
            value_map.emplace(instruction,
                              value_map.at(original.get_parameters()[candidate.parameter_index].get()));
            continue;
        }

        std::unique_ptr<CoreIrInstruction> clone =
            clone_instruction_remapped(*instruction, value_map);
        if (clone == nullptr) {
            return nullptr;
        }
        CoreIrInstruction *clone_ptr = new_block->append_instruction(std::move(clone));
        value_map.emplace(instruction, clone_ptr);
    }

    return promoted;
}

bool rewrite_callsites_for_promoted_function(CoreIrModule &module,
                                             const PromotionCandidate &candidate,
                                             CoreIrFunction &promoted_function) {
    const auto *promoted_type = promoted_function.get_function_type();
    if (promoted_type == nullptr) {
        return false;
    }

    bool changed = false;
    for (const auto &function_ptr : module.get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function == nullptr) {
            continue;
        }
        for (const auto &block_ptr : function->get_basic_blocks()) {
            CoreIrBasicBlock *block = block_ptr.get();
            if (block == nullptr) {
                continue;
            }

            auto &instructions = block->get_instructions();
            for (std::size_t index = 0; index < instructions.size(); ++index) {
                auto *call = dynamic_cast<CoreIrCallInst *>(instructions[index].get());
                if (call == nullptr || !call->get_is_direct_call() ||
                    call->get_callee_name() != candidate.function->get_name() ||
                    candidate.parameter_index >= call->get_argument_count()) {
                    continue;
                }

                CoreIrValue *pointer_argument = call->get_argument(candidate.parameter_index);
                auto load = std::make_unique<CoreIrLoadInst>(
                    candidate.promoted_type, call->get_name() + ".argprom.load",
                    pointer_argument);
                CoreIrInstruction *load_ptr =
                    insert_instruction_before(*block, call, std::move(load));
                if (load_ptr == nullptr) {
                    continue;
                }

                std::vector<CoreIrValue *> arguments;
                arguments.reserve(call->get_argument_count());
                for (std::size_t arg = 0; arg < call->get_argument_count(); ++arg) {
                    arguments.push_back(arg == candidate.parameter_index ? load_ptr
                                                                         : call->get_argument(arg));
                }
                auto replacement = std::make_unique<CoreIrCallInst>(
                    call->get_type(), call->get_name(), promoted_function.get_name(),
                    promoted_type, std::move(arguments));
                replacement->set_source_span(call->get_source_span());
                if (replace_instruction(*block, call, std::move(replacement)) != nullptr) {
                    changed = true;
                }
            }
        }
    }
    return changed;
}

} // namespace

PassKind CoreIrArgumentPromotionPass::Kind() const {
    return PassKind::CoreIrArgumentPromotion;
}

const char *CoreIrArgumentPromotionPass::Name() const {
    return "CoreIrArgumentPromotionPass";
}

CoreIrPassMetadata CoreIrArgumentPromotionPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrArgumentPromotionPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    CoreIrContext *core_ir_context = build_result == nullptr ? nullptr : build_result->get_context();
    if (module == nullptr || core_ir_context == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    std::vector<PromotionCandidate> candidates;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function == nullptr) {
            continue;
        }
        for (std::size_t index = 0; index < function->get_parameters().size(); ++index) {
            CoreIrParameter *parameter = function->get_parameters()[index].get();
            if (parameter == nullptr || parameter->get_type() == nullptr ||
                parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer ||
                !function_is_arg_promotion_candidate(*function, index)) {
                continue;
            }
            const CoreIrType *promoted_type =
                find_direct_load_only_parameter_type(*parameter);
            if (promoted_type != nullptr) {
                candidates.push_back(PromotionCandidate{function, index, promoted_type});
                break;
            }
        }
    }

    CoreIrPassEffects effects;
    std::size_t promotion_counter = 0;
    for (const PromotionCandidate &candidate : candidates) {
        if (candidate.function == nullptr) {
            continue;
        }
        auto promoted_function = clone_promoted_function(
            *core_ir_context, *candidate.function, candidate,
            candidate.function->get_name() + ".argprom." +
                std::to_string(promotion_counter++));
        if (promoted_function == nullptr) {
            continue;
        }
        CoreIrFunction *promoted_ptr = module->append_function(std::move(promoted_function));
        if (promoted_ptr == nullptr ||
            !rewrite_callsites_for_promoted_function(*module, candidate, *promoted_ptr)) {
            if (promoted_ptr != nullptr) {
                module->erase_function(promoted_ptr);
            }
            continue;
        }
        effects.module_changed = true;
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }

    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
