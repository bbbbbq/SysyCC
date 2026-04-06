#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "tests/asm/aarch64_native_backend_test_utils.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *pair_array_type =
        context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *pair_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{pair_array_type});
    auto *outer_pairs_type =
        context->create_type<CoreIrArrayType>(pair_type, 3);
    auto *outer_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, outer_pairs_type});
    auto *outer_ptr_type =
        context->create_type<CoreIrPointerType>(outer_type);
    auto *i32_ptr_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type,
        std::vector<const CoreIrType *>{outer_ptr_type, i32_type, i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "aarch64_native_gep_mixed_pointer_array_struct");
    auto *function =
        module->create_function<CoreIrFunction>("read_mix", function_type, false);
    auto *base = function->create_parameter<CoreIrParameter>(outer_ptr_type, "base");
    auto *index_i = function->create_parameter<CoreIrParameter>(i32_type, "i");
    auto *index_j = function->create_parameter<CoreIrParameter>(i32_type, "j");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");

    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    auto *address = entry->create_instruction<CoreIrGetElementPtrInst>(
        i32_ptr_type, "mixed_addr", base,
        std::vector<CoreIrValue *>{zero, one, index_i, zero, index_j});
    auto *loaded =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "loaded", address);
    entry->create_instruction<CoreIrReturnInst>(void_type, loaded);

    std::cout << test::emit_aarch64_native_asm(*module);
    return 0;
}
