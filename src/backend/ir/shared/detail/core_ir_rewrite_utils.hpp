#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrInstruction;
class CoreIrValue;
class CoreIrType;
class CoreIrIntegerType;
class CoreIrConstantInt;
class CoreIrGetElementPtrInst;
enum class CoreIrCastKind : unsigned char;

namespace detail {

const CoreIrIntegerType *as_integer_type(const CoreIrType *type);
const CoreIrConstantInt *as_integer_constant(const CoreIrValue *value);
bool is_zero_integer_constant(const CoreIrValue *value);
bool is_one_integer_constant(const CoreIrValue *value);
bool is_all_ones_integer_constant(const CoreIrValue *value);
std::optional<std::size_t> get_integer_bit_width(const CoreIrType *type);

const CoreIrType *get_pointer_pointee_type(const CoreIrValue *value);
const CoreIrType *get_gep_result_pointee_type(const CoreIrGetElementPtrInst &gep);
bool is_trivial_zero_index_gep(const CoreIrGetElementPtrInst &gep);
CoreIrValue *unwrap_trivial_zero_index_geps(CoreIrValue *value);
const CoreIrType *get_selected_gep_pointee_type(const CoreIrGetElementPtrInst &gep);
bool can_flatten_structural_gep(const CoreIrGetElementPtrInst &gep);
bool collect_structural_gep_chain(const CoreIrGetElementPtrInst &gep,
                                  CoreIrValue *&root_base,
                                  std::vector<CoreIrValue *> &indices);

bool is_supported_integer_cast_kind(CoreIrCastKind cast_kind);
bool preserves_integer_truthiness(CoreIrCastKind cast_kind);
bool is_pointer_type(const CoreIrType *type);
bool is_float_type(const CoreIrType *type);

CoreIrInstruction *insert_instruction_before(
    CoreIrBasicBlock &block, CoreIrInstruction *anchor,
    std::unique_ptr<CoreIrInstruction> instruction);
bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction);
CoreIrInstruction *replace_instruction(CoreIrBasicBlock &block,
                                       CoreIrInstruction *instruction,
                                       std::unique_ptr<CoreIrInstruction> replacement);

} // namespace detail
} // namespace sysycc
