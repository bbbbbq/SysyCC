#pragma once

#include <cstdint>

namespace sysycc {

enum class BackendKind : std::uint8_t;
class PassManager;

void append_default_core_ir_pipeline(PassManager &pass_manager);
void append_default_core_ir_pipeline(PassManager &pass_manager,
                                     BackendKind backend_kind);

} // namespace sysycc
