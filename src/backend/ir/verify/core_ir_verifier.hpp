#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/core/ir_value.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

enum class CoreIrVerifyIssueKind : unsigned char {
    ModuleOwnership,
    FunctionOwnership,
    BlockOwnership,
    InstructionOwnership,
    TerminatorLayout,
    PhiLayout,
    PhiIncomingMismatch,
    UseDefMismatch,
    InvalidReference,
};

struct CoreIrVerifyIssue {
    CoreIrVerifyIssueKind kind = CoreIrVerifyIssueKind::InvalidReference;
    std::string message;
    const CoreIrFunction *function = nullptr;
    const CoreIrBasicBlock *block = nullptr;
    const CoreIrInstruction *instruction = nullptr;
};

struct CoreIrVerifyResult {
    bool ok = true;
    std::vector<CoreIrVerifyIssue> issues;

    void add_issue(CoreIrVerifyIssue issue) {
        ok = false;
        issues.push_back(std::move(issue));
    }
};

class CoreIrVerifier {
  public:
    CoreIrVerifyResult verify_module(const CoreIrModule &module) const;
    CoreIrVerifyResult verify_function(
        const CoreIrFunction &function,
        const CoreIrCfgAnalysisResult *cfg_analysis = nullptr) const;
};

bool emit_core_ir_verify_result(CompilerContext &context,
                                const CoreIrVerifyResult &verify_result,
                                const char *pass_name);

} // namespace sysycc
