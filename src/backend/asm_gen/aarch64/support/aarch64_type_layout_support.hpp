#pragma once

#include <cstddef>

#include "backend/asm_gen/aarch64/model/aarch64_register_info.hpp"

namespace sysycc {

class CoreIrType;
class CoreIrStructType;
class CoreIrIntegerType;
class CoreIrFloatType;
enum class CoreIrFloatKind : unsigned char;

bool is_integer_type(const CoreIrType *type);
bool is_pointer_type(const CoreIrType *type);
bool is_float_type(const CoreIrType *type);
bool is_vector_type(const CoreIrType *type);
bool is_void_type(const CoreIrType *type);
const CoreIrIntegerType *as_integer_type(const CoreIrType *type);
const CoreIrFloatType *as_float_type(const CoreIrType *type);
bool is_aggregate_type(const CoreIrType *type);
bool type_contains_float_kind(const CoreIrType *type, CoreIrFloatKind float_kind);

AArch64VirtualRegKind classify_float_reg_kind(CoreIrFloatKind float_kind);
AArch64VirtualRegKind classify_virtual_reg_kind(const CoreIrType *type);
bool uses_general_64bit_register(const CoreIrType *type);
bool uses_64bit_register(const CoreIrType *type);
std::size_t virtual_reg_size(AArch64VirtualRegKind kind);
bool is_live_across_call_callee_saved_capable(AArch64VirtualRegKind kind);
bool is_narrow_integer_type(const CoreIrType *type);

bool is_supported_scalar_storage_type(const CoreIrType *type);
bool is_supported_object_type(const CoreIrType *type);
bool is_supported_value_type(const CoreIrType *type);

std::size_t get_storage_size(const CoreIrType *type);
std::size_t get_storage_alignment(const CoreIrType *type);
std::size_t get_type_size(const CoreIrType *type);
std::size_t get_type_alignment(const CoreIrType *type);
std::size_t get_struct_member_offset(const CoreIrStructType *struct_type,
                                     std::size_t index);

} // namespace sysycc
