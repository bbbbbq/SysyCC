#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
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

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "  [FAIL] %s\n", msg);                      \
            ++g_failed;                                                       \
            return 1;                                                         \
        }                                                                     \
        std::printf("  [PASS] %s\n", msg);                                    \
        ++g_passed;                                                           \
    } while (0)

static int test_true_branch(CoreIrContext *context, CoreIrModule *module) {
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i32_func_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    auto *func = module->create_function<CoreIrFunction>("test_true_branch",
                                                         i32_func_type, false);
    auto *entry = func->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block =
        func->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        func->create_basic_block<CoreIrBasicBlock>("false_block");
    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, true_block,
                                                  false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, seven);
    false_block->create_instruction<CoreIrReturnInst>(void_type, nine);
    return 0;
}

static int test_false_branch(CoreIrContext *context, CoreIrModule *module) {
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i32_func_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *zero = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    auto *func = module->create_function<CoreIrFunction>("test_false_branch",
                                                         i32_func_type, false);
    auto *entry = func->create_basic_block<CoreIrBasicBlock>("entry2");
    auto *true_block =
        func->create_basic_block<CoreIrBasicBlock>("true2");
    auto *false_block =
        func->create_basic_block<CoreIrBasicBlock>("false2");
    entry->create_instruction<CoreIrCondJumpInst>(void_type, zero, true_block,
                                                  false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, seven);
    false_block->create_instruction<CoreIrReturnInst>(void_type, nine);
    return 0;
}

static CoreIrPhiInst *
add_phi_with_cleanup_test(CoreIrContext *context, CoreIrModule *module) {
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i32_func_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *forty_two = context->create_constant<CoreIrConstantInt>(i32_type, 42);

    auto *func = module->create_function<CoreIrFunction>("test_phi_true_cleanup",
                                                         i32_func_type, false);
    auto *entry = func->create_basic_block<CoreIrBasicBlock>("entry3");
    auto *merge = func->create_basic_block<CoreIrBasicBlock>("merge3");
    auto *cleanup = func->create_basic_block<CoreIrBasicBlock>("cleanup3");
    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, merge,
                                                  cleanup);

    auto *merge_phi =
        merge->create_instruction<CoreIrPhiInst>(i32_type, "merge_val");
    merge_phi->add_incoming(entry, seven);
    merge->create_instruction<CoreIrReturnInst>(void_type, merge_phi);

    auto *cleanup_phi =
        cleanup->create_instruction<CoreIrPhiInst>(i32_type, "cleanup_val");
    cleanup_phi->add_incoming(entry, forty_two);
    cleanup->create_instruction<CoreIrReturnInst>(void_type, cleanup_phi);
    return cleanup_phi;
}

static CoreIrPhiInst *
add_false_phi_cleanup_test(CoreIrContext *context, CoreIrModule *module) {
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i32_func_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *zero = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *forty_two = context->create_constant<CoreIrConstantInt>(i32_type, 42);

    auto *func = module->create_function<CoreIrFunction>(
        "test_phi_false_cleanup", i32_func_type, false);
    auto *entry = func->create_basic_block<CoreIrBasicBlock>("entry4");
    auto *merge = func->create_basic_block<CoreIrBasicBlock>("merge4");
    auto *cleanup = func->create_basic_block<CoreIrBasicBlock>("cleanup4");
    entry->create_instruction<CoreIrCondJumpInst>(void_type, zero, cleanup,
                                                  merge);

    auto *merge_phi =
        merge->create_instruction<CoreIrPhiInst>(i32_type, "merge_val");
    merge_phi->add_incoming(entry, seven);
    merge->create_instruction<CoreIrReturnInst>(void_type, merge_phi);

    auto *cleanup_phi =
        cleanup->create_instruction<CoreIrPhiInst>(i32_type, "cleanup_val");
    cleanup_phi->add_incoming(entry, forty_two);
    cleanup->create_instruction<CoreIrReturnInst>(void_type, cleanup_phi);
    return cleanup_phi;
}

int main() {
    std::printf("CoreIrConstFoldPass regression tests\n");
    std::printf("====================================\n");

    // ── Build IR ────────────────────────────────────────────────────
    auto context = std::make_unique<CoreIrContext>();
    auto *module =
        context->create_module<CoreIrModule>("ir_core_const_fold_pass");

    test_true_branch(context.get(), module);
    test_false_branch(context.get(), module);
    CoreIrPhiInst *cleanup3_phi = add_phi_with_cleanup_test(context.get(), module);
    CoreIrPhiInst *cleanup4_phi =
        add_false_phi_cleanup_test(context.get(), module);

    // ── Run pass ────────────────────────────────────────────────────
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrConstFoldPass pass;
    assert(pass.Run(compiler_context).ok);

    // ── Verify ──────────────────────────────────────────────────────
    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);

    std::printf("\n--- Results ---\n");

    // Test 1: true branch folds condjump → jmp %true_block
    {
        bool ok = true;
        ok = ok && (text.find("condjump") == std::string::npos);
        ok = ok && (text.find("jmp %true_block") != std::string::npos);
        CHECK(ok, "true branch: condjump folds to jmp %true_block");
    }

    // Test 2: false branch folds condjump → jmp %false2
    {
        bool ok = (text.find("jmp %false2") != std::string::npos);
        CHECK(ok, "false branch: condjump folds to jmp %false2");
    }

    // Test 3: phi incoming cleanup (true branch removed successor)
    {
        bool ok = (cleanup3_phi != nullptr &&
                   cleanup3_phi->get_incoming_count() == 0);
        CHECK(ok, "phi cleanup (true): non-taken successor phi has no incoming");
    }

    // Test 4: phi incoming cleanup (false branch removed successor)
    {
        bool ok = (cleanup4_phi != nullptr &&
                   cleanup4_phi->get_incoming_count() == 0);
        CHECK(ok, "phi cleanup (false): non-taken successor phi has no incoming");
    }

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
