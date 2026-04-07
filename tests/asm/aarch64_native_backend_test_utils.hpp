#pragma once

#include <cassert>
#include <memory>
#include <string>

#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/backend_options.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc::test {

inline BackendOptions make_default_aarch64_backend_options() {
    BackendOptions options;
    options.set_backend_kind(BackendKind::AArch64Native);
    options.set_target_triple("aarch64-unknown-linux-gnu");
    return options;
}

inline std::string emit_aarch64_native_asm(const CoreIrModule &module) {
    DiagnosticEngine diagnostics;
    AArch64AsmBackend backend;
    BackendOptions options = make_default_aarch64_backend_options();
    std::unique_ptr<AsmResult> result =
        backend.Generate(module, options, diagnostics);
    assert(result != nullptr);
    assert(!diagnostics.has_error());
    assert(result->get_target_kind() == AsmTargetKind::AArch64);
    return result->get_text();
}

inline void assert_contains(const std::string &text, const std::string &needle) {
    assert(text.find(needle) != std::string::npos);
}

inline std::size_t count_occurrences(const std::string &text,
                                     const std::string &needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

} // namespace sysycc::test
