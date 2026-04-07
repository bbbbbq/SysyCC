#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrFunction;

std::string sanitize_aarch64_label_fragment(const std::string &text);
std::string make_aarch64_function_epilogue_label(const std::string &function_name);
std::string make_aarch64_function_block_label(const std::string &function_name,
                                              const CoreIrBasicBlock &block);
std::unordered_map<const CoreIrBasicBlock *, std::string>
build_aarch64_function_block_labels(const CoreIrFunction &function,
                                    const std::string &function_name);
void initialize_aarch64_function_frame_record(AArch64MachineFunction &function,
                                              std::size_t frame_size);
void append_aarch64_standard_prologue(AArch64MachineBlock &block,
                                      std::size_t frame_size);
void append_aarch64_standard_epilogue(AArch64MachineBlock &block,
                                      std::size_t frame_size);

} // namespace sysycc
