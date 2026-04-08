#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sysycc {

enum class AArch64SectionKind : unsigned char {
    Text,
    Data,
    ReadOnlyData,
    Bss,
    EhFrame,
    DebugFrame,
    DebugLine,
};

enum class AArch64CfiDirectiveKind : unsigned char {
    StartProcedure,
    EndProcedure,
    DefCfa,
    DefCfaRegister,
    DefCfaOffset,
    Offset,
    Restore,
};

struct AArch64CfiDirective {
    AArch64CfiDirectiveKind kind = AArch64CfiDirectiveKind::StartProcedure;
    std::size_t code_offset = 0;
    unsigned reg = 0;
    long long offset = 0;
};

struct AArch64DebugLocation {
    unsigned file_id = 0;
    int line = 0;
    int column = 0;
};

class AArch64FrameRecord {
  private:
    std::size_t stack_frame_size_ = 0;
    std::vector<AArch64CfiDirective> cfi_directives_;

  public:
    void set_stack_frame_size(std::size_t stack_frame_size) noexcept {
        stack_frame_size_ = stack_frame_size;
    }
    std::size_t get_stack_frame_size() const noexcept { return stack_frame_size_; }

    void append_cfi_directive(AArch64CfiDirective directive) {
        cfi_directives_.push_back(std::move(directive));
    }
    void clear_cfi_directives() noexcept { cfi_directives_.clear(); }
    const std::vector<AArch64CfiDirective> &get_cfi_directives() const noexcept {
        return cfi_directives_;
    }
};

struct AArch64DebugFileEntry {
    unsigned index = 0;
    std::string path;
};

} // namespace sysycc
