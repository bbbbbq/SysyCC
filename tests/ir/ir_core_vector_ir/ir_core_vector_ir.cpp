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
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *vec4_i32 = context->create_type<CoreIrVectorType>(i32_type, 4);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_vector_ir");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("slot", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);
    auto *vec_zero = context->create_constant<CoreIrConstantAggregate>(
        vec4_i32, std::vector<const CoreIrConstant *>{zero, zero, zero, zero});

    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32, "addr", slot);
    auto *vec_load =
        entry->create_instruction<CoreIrLoadInst>(vec4_i32, "vec.load", addr);
    auto *vec_add = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, vec4_i32, "vec.add", vec_load, vec_zero);
    entry->create_instruction<CoreIrExtractElementInst>(i32_type, "extract", vec_add,
                                                        zero);
    auto *insert = entry->create_instruction<CoreIrInsertElementInst>(
        vec4_i32, "insert", vec_add, one, two);
    auto *shuffle = entry->create_instruction<CoreIrShuffleVectorInst>(
        vec4_i32, "shuffle", vec_add, insert,
        std::vector<CoreIrValue *>{zero, one, two, three});
    auto *reduce = entry->create_instruction<CoreIrVectorReduceAddInst>(
        i32_type, "reduce", shuffle);
    entry->create_instruction<CoreIrStoreInst>(void_type, shuffle, addr);
    entry->create_instruction<CoreIrReturnInst>(void_type, reduce);

    CoreIrRawPrinter printer;
    const std::string raw = printer.print_module(*module);
    assert(raw.find("<4 x i32>") != std::string::npos);
    assert(raw.find("vector_reduce_add") != std::string::npos);

    DiagnosticEngine diagnostics;
    CoreIrLlvmTargetBackend backend;
    std::unique_ptr<IRResult> lowered = backend.Lower(*module, diagnostics);
    assert(lowered != nullptr);
    const std::string &llvm = lowered->get_text();
    assert(llvm.find("<4 x i32>") != std::string::npos);
    assert(llvm.find("extractelement") != std::string::npos);
    assert(llvm.find("insertelement") != std::string::npos);
    assert(llvm.find("shufflevector") != std::string::npos);
    assert(llvm.find("llvm.vector.reduce.add") != std::string::npos);
    return 0;
}
