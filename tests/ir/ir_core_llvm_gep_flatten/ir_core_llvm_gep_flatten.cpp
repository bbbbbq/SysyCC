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
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *f32_type =
        context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array8_f32 = context->create_type<CoreIrArrayType>(f32_type, 8);
    auto *ptr_array8_f32 = context->create_type<CoreIrPointerType>(array8_f32);
    auto *ptr_f32 = context->create_type<CoreIrPointerType>(f32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_array8_f32, i32_type, i32_type},
        false);
    auto *module = context->create_module<CoreIrModule>("llvm_gep_flatten");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *matrix =
        function->create_parameter<CoreIrParameter>(ptr_array8_f32, "matrix");
    auto *row = function->create_parameter<CoreIrParameter>(i32_type, "row");
    auto *col = function->create_parameter<CoreIrParameter>(i32_type, "col");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    auto *row_gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array8_f32, "row_gep", matrix, std::vector<CoreIrValue *>{row});
    auto *elt_gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_f32, "elt_gep", row_gep, std::vector<CoreIrValue *>{zero, col});
    auto *load = entry->create_instruction<CoreIrLoadInst>(f32_type, "load", elt_gep);
    auto *ret = entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::FloatToSignedInt, i32_type, "ret", load);
    entry->create_instruction<CoreIrReturnInst>(void_type, ret);

    DiagnosticEngine diagnostics;
    CoreIrLlvmTargetBackend backend;
    std::unique_ptr<IRResult> lowered = backend.Lower(*module, diagnostics);
    assert(lowered != nullptr);

    const std::string &llvm = lowered->get_text();
    assert(llvm.find("getelementptr inbounds [8 x float], ptr %matrix, i32 %row, i32 %col") !=
           std::string::npos);
    return 0;
}
