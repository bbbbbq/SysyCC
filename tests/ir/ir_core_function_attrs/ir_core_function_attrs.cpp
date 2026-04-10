#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/function_attrs/core_ir_function_attrs_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *scalar_fn_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *ptr_read_fn_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *ptr_ret_fn_type = context->create_type<CoreIrFunctionType>(
        ptr_i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_function_attrs");

    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    auto *const_fn =
        module->create_function<CoreIrFunction>("const_fn", scalar_fn_type, true);
    auto *const_entry = const_fn->create_basic_block<CoreIrBasicBlock>("entry");
    const_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    auto *read_fn =
        module->create_function<CoreIrFunction>("read_fn", ptr_read_fn_type, true);
    auto *read_param =
        read_fn->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *read_entry = read_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *loaded =
        read_entry->create_instruction<CoreIrLoadInst>(i32_type, "loaded", read_param);
    read_entry->create_instruction<CoreIrReturnInst>(void_type, loaded);

    auto *write_alias_fn =
        module->create_function<CoreIrFunction>("write_alias_fn", ptr_read_fn_type, true);
    auto *write_alias_param =
        write_alias_fn->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *write_alias_slot =
        write_alias_fn->create_stack_slot<CoreIrStackSlot>("alias", ptr_i32_type, 8);
    auto *write_alias_entry =
        write_alias_fn->create_basic_block<CoreIrBasicBlock>("entry");
    write_alias_entry->create_instruction<CoreIrStoreInst>(void_type,
                                                           write_alias_param,
                                                           write_alias_slot);
    auto *write_alias_loaded =
        write_alias_entry->create_instruction<CoreIrLoadInst>(ptr_i32_type, "alias.load",
                                                              write_alias_slot);
    write_alias_entry->create_instruction<CoreIrStoreInst>(void_type, seven,
                                                           write_alias_loaded);
    write_alias_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    auto *ret_param =
        module->create_function<CoreIrFunction>("ret_param", ptr_ret_fn_type, true);
    auto *ret_param_value =
        ret_param->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *ret_param_entry = ret_param->create_basic_block<CoreIrBasicBlock>("entry");
    ret_param_entry->create_instruction<CoreIrReturnInst>(void_type, ret_param_value);

    auto *recur =
        module->create_function<CoreIrFunction>("recur", scalar_fn_type, true);
    auto *recur_entry = recur->create_basic_block<CoreIrBasicBlock>("entry");
    recur_entry->create_instruction<CoreIrCallInst>(
        i32_type, "self", "recur", scalar_fn_type, std::vector<CoreIrValue *>{});
    recur_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrAnalysisManager *analysis_manager =
        compiler_context.get_core_ir_build_result()->get_analysis_manager();
    assert(analysis_manager != nullptr);

    const auto &analysis =
        analysis_manager->get_or_compute<CoreIrFunctionAttrsAnalysis>(*module);
    const auto *const_attrs = analysis.get_summary(const_fn);
    const auto *read_attrs = analysis.get_summary(read_fn);
    const auto *ret_param_attrs = analysis.get_summary(ret_param);
    const auto *recur_attrs = analysis.get_summary(recur);
    const auto *write_alias_attrs = analysis.get_summary(write_alias_fn);

    assert(const_attrs != nullptr);
    assert(const_attrs->memory_behavior == CoreIrMemoryBehavior::None);
    assert(const_attrs->constant_return == seven);
    assert(const_attrs->is_norecurse);

    assert(read_attrs != nullptr);
    assert(read_attrs->memory_behavior == CoreIrMemoryBehavior::Read);
    assert(read_attrs->parameter_nocapture.size() == 1);
    assert(read_attrs->parameter_nocapture.front());
    assert(read_attrs->parameter_readonly.size() == 1);
    assert(read_attrs->parameter_readonly.front());

    assert(write_alias_attrs != nullptr);
    assert(write_alias_attrs->parameter_readonly.size() == 1);
    assert(!write_alias_attrs->parameter_readonly.front());

    assert(ret_param_attrs != nullptr);
    assert(ret_param_attrs->returned_parameter_index.has_value());
    assert(*ret_param_attrs->returned_parameter_index == 0);

    assert(recur_attrs != nullptr);
    assert(!recur_attrs->is_norecurse);

    CoreIrFunctionAttrsPass pass;
    assert(pass.Run(compiler_context).ok);
    assert(const_fn->get_is_readnone());
    assert(!const_fn->get_is_readonly());
    assert(!const_fn->get_is_writeonly());
    assert(read_fn->get_is_readonly());
    assert(read_fn->get_parameter_readonly().size() == 1);
    assert(read_fn->get_parameter_readonly().front());
    assert(!read_fn->get_is_readnone());
    assert(!read_fn->get_is_writeonly());
    assert(!write_alias_fn->get_is_readonly());
    assert(write_alias_fn->get_parameter_readonly().size() == 1);
    assert(!write_alias_fn->get_parameter_readonly().front());
    assert(!write_alias_fn->get_is_readnone());
    assert(!recur->get_is_norecurse());
    return 0;
}
