#include "backend/ir/dce/core_ir_dce_pass.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

std::unordered_set<CoreIrBasicBlock *> collect_reachable_blocks(
    CoreIrFunction &function) {
    std::unordered_set<CoreIrBasicBlock *> reachable;
    if (function.get_basic_blocks().empty()) {
        return reachable;
    }

    std::vector<CoreIrBasicBlock *> worklist{
        function.get_basic_blocks().front().get()};
    while (!worklist.empty()) {
        CoreIrBasicBlock *block = worklist.back();
        worklist.pop_back();
        if (block == nullptr || !reachable.insert(block).second) {
            continue;
        }

        const auto &instructions = block->get_instructions();
        if (instructions.empty()) {
            continue;
        }

        if (const auto *jump =
                dynamic_cast<const CoreIrJumpInst *>(instructions.back().get());
            jump != nullptr) {
            worklist.push_back(jump->get_target_block());
            continue;
        }

        if (const auto *cond_jump = dynamic_cast<const CoreIrCondJumpInst *>(
                instructions.back().get());
            cond_jump != nullptr) {
            worklist.push_back(cond_jump->get_true_block());
            worklist.push_back(cond_jump->get_false_block());
        }
    }

    return reachable;
}

void detach_block(CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        instruction->detach_operands();
    }
}

bool remove_unreachable_blocks(CoreIrFunction &function) {
    auto &blocks = function.get_basic_blocks();
    const std::unordered_set<CoreIrBasicBlock *> reachable =
        collect_reachable_blocks(function);
    const std::size_t old_size = blocks.size();

    blocks.erase(
        std::remove_if(blocks.begin(), blocks.end(),
                       [&reachable](const std::unique_ptr<CoreIrBasicBlock> &block) {
                           if (block == nullptr) {
                               return true;
                           }
                           if (reachable.find(block.get()) != reachable.end()) {
                               return false;
                           }
                           detach_block(*block);
                           return true;
                       }),
        blocks.end());

    return blocks.size() != old_size;
}

bool remove_dead_instructions(CoreIrFunction &function) {
    bool changed = false;
    for (const auto &block : function.get_basic_blocks()) {
        auto &instructions = block->get_instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            CoreIrInstruction *instruction = it->get();
            if (instruction == nullptr) {
                it = instructions.erase(it);
                changed = true;
                continue;
            }
            if (!instruction->get_has_side_effect() &&
                instruction->get_uses().empty()) {
                instruction->detach_operands();
                it = instructions.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }
    }
    return changed;
}

} // namespace

PassKind CoreIrDcePass::Kind() const { return PassKind::CoreIrDce; }

const char *CoreIrDcePass::Name() const { return "CoreIrDcePass"; }

PassResult CoreIrDcePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr || build_result->get_module() == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &function : build_result->get_module()->get_functions()) {
            changed = remove_unreachable_blocks(*function) || changed;
            changed = remove_dead_instructions(*function) || changed;
        }
    }

    context.set_core_ir_dump_file_path("");
    if (context.get_dump_core_ir()) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);
        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".core-ir.txt");
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure("failed to open core ir dump file");
        }
        CoreIrRawPrinter printer;
        ofs << printer.print_module(*build_result->get_module());
        context.set_core_ir_dump_file_path(output_file.string());
    }
    return PassResult::Success();
}

} // namespace sysycc
