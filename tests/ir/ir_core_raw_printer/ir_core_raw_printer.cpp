#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"

using namespace sysycc;

int main() {
    CoreIrContext context;

    const auto *void_type = context.create_type<CoreIrVoidType>();
    const auto *i8_type = context.create_type<CoreIrIntegerType>(8);
    const auto *i32_type = context.create_type<CoreIrIntegerType>(32);
    const auto *string_type = context.create_type<CoreIrArrayType>(i8_type, 3);
    const auto *add_type = context.create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type, i32_type}, false);

    auto *module = context.create_module<CoreIrModule>("demo");
    const auto *string_value = context.create_constant<CoreIrConstantByteString>(
        string_type, std::vector<std::uint8_t>{'h', 'i', 0});
    module->create_global<CoreIrGlobal>(".str0", string_type, string_value, true,
                                        true);

    auto *function =
        module->create_function<CoreIrFunction>("add", add_type, false);
    auto *lhs = function->create_parameter<CoreIrParameter>(i32_type, "lhs");
    auto *rhs = function->create_parameter<CoreIrParameter>(i32_type, "rhs");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", lhs, rhs);
    entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    assert(lhs->get_uses().size() == 1);
    assert(rhs->get_uses().size() == 1);
    assert(entry->get_has_terminator());

    CoreIrRawPrinter printer;
    const std::string actual = printer.print_module(*module);
    const std::string expected =
        "module demo\n"
        "\n"
        "const @.str0 : [3 x i8] = c\"hi\\00\" internal\n"
        "\n"
        "func @add(i32 %lhs, i32 %rhs) -> i32 {\n"
        "entry:\n"
        "  %sum = add i32 %lhs, %rhs\n"
        "  ret i32 %sum\n"
        "}\n";
    assert(actual == expected);
    return 0;
}
