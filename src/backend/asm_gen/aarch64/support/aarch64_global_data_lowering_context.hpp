#pragma once

#include <string>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class AArch64GlobalDataLoweringContext {
  public:
    virtual ~AArch64GlobalDataLoweringContext() = default;

    virtual void record_symbol_definition(const std::string &name,
                                          AArch64SymbolKind kind,
                                          AArch64SectionKind section_kind,
                                          bool is_global_symbol) = 0;
    virtual void record_symbol_reference(const std::string &name,
                                         AArch64SymbolKind kind) = 0;
    virtual void report_error(const std::string &message) = 0;
};

} // namespace sysycc
