#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_post_ssa_dce");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, merge);
    dead->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    phi->add_incoming(entry, one);
    phi->add_incoming(dead, two);
    merge->create_instruction<CoreIrReturnInst>(void_type, phi);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrDcePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("dead:") == std::string::npos);
    assert(text.find("phi i32") == std::string::npos);
    assert(text.find("ret i32 1") != std::string::npos);
    return 0;
}
