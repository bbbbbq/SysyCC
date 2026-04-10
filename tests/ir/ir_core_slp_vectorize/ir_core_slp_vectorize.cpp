#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/slp_vectorize/core_ir_slp_vectorize_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array4_i32 = context->create_type<CoreIrArrayType>(i32_type, 4);
    auto *ptr_array4_i32 = context->create_type<CoreIrPointerType>(array4_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_slp_vectorize");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *a_slot = function->create_stack_slot<CoreIrStackSlot>("a", array4_i32, 4);
    auto *b_slot = function->create_stack_slot<CoreIrStackSlot>("b", array4_i32, 4);
    auto *c_slot = function->create_stack_slot<CoreIrStackSlot>("c", array4_i32, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    auto *a_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32, "a.addr", a_slot);
    auto *b_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32, "b.addr", b_slot);
    auto *c_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32, "c.addr", c_slot);

    auto make_lane = [&](CoreIrValue *lane) {
        auto *a_elem = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "a.elem", a_addr, std::vector<CoreIrValue *>{zero, lane});
        auto *b_elem = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "b.elem", b_addr, std::vector<CoreIrValue *>{zero, lane});
        auto *c_elem = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "c.elem", c_addr, std::vector<CoreIrValue *>{zero, lane});
        auto *a_load =
            entry->create_instruction<CoreIrLoadInst>(i32_type, "a.load", a_elem);
        auto *b_load =
            entry->create_instruction<CoreIrLoadInst>(i32_type, "b.load", b_elem);
        auto *sum = entry->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type, "sum", a_load, b_load);
        entry->create_instruction<CoreIrStoreInst>(void_type, sum, c_elem);
    };

    make_lane(zero);
    make_lane(one);
    make_lane(two);
    make_lane(three);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrSlpVectorizePass pass;
    assert(pass.Run(compiler_context).ok);

    DiagnosticEngine diagnostics;
    CoreIrLlvmTargetBackend backend;
    std::unique_ptr<IRResult> lowered = backend.Lower(*module, diagnostics);
    assert(lowered != nullptr);
    const std::string &llvm = lowered->get_text();
    assert(llvm.find("load <4 x i32>") != std::string::npos);
    assert(llvm.find("add <4 x i32>") != std::string::npos);
    assert(llvm.find("store <4 x i32>") != std::string::npos);
    return 0;
}
