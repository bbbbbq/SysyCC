#pragma once

#include <optional>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_model.hpp"

namespace sysycc {

enum class AArch64LlvmImportValueKind : unsigned char {
    Unknown,
    Local,
    Global,
    Constant,
};

struct AArch64LlvmImportTypedValue {
    std::string type_text;
    AArch64LlvmImportType type;
    std::string value_text;
    AArch64LlvmImportValueKind kind = AArch64LlvmImportValueKind::Unknown;
    std::string local_name;
    std::string global_name;
    AArch64LlvmImportConstant constant;

    bool is_valid() const {
        return kind != AArch64LlvmImportValueKind::Unknown;
    }
};

struct AArch64LlvmImportBinarySpec {
    std::string type_text;
    AArch64LlvmImportType type;
    AArch64LlvmImportTypedValue lhs;
    AArch64LlvmImportTypedValue rhs;
};

struct AArch64LlvmImportUnarySpec {
    std::string type_text;
    AArch64LlvmImportType type;
    AArch64LlvmImportTypedValue operand;
};

struct AArch64LlvmImportCastSpec {
    std::string source_type_text;
    AArch64LlvmImportType source_type;
    AArch64LlvmImportTypedValue source_value;
    std::string target_type_text;
    AArch64LlvmImportType target_type;
};

struct AArch64LlvmImportAllocaSpec {
    std::string allocated_type_text;
    AArch64LlvmImportType allocated_type;
    std::size_t alignment = 0;
};

struct AArch64LlvmImportLoadSpec {
    std::string load_type_text;
    AArch64LlvmImportType load_type;
    AArch64LlvmImportTypedValue address;
    std::size_t alignment = 0;
};

struct AArch64LlvmImportStoreSpec {
    AArch64LlvmImportTypedValue value;
    AArch64LlvmImportTypedValue address;
    std::size_t alignment = 0;
};

struct AArch64LlvmImportGetElementPtrSpec {
    bool is_inbounds = false;
    std::string source_type_text;
    AArch64LlvmImportType source_type;
    AArch64LlvmImportTypedValue base;
    std::vector<AArch64LlvmImportTypedValue> indices;
};

struct AArch64LlvmImportCallSpec {
    std::string return_type_text;
    AArch64LlvmImportType return_type;
    AArch64LlvmImportTypedValue callee;
    std::vector<AArch64LlvmImportTypedValue> arguments;
};

struct AArch64LlvmImportExtractElementSpec {
    AArch64LlvmImportTypedValue vector_value;
    AArch64LlvmImportTypedValue index_value;
};

struct AArch64LlvmImportInsertElementSpec {
    AArch64LlvmImportTypedValue vector_value;
    AArch64LlvmImportTypedValue element_value;
    AArch64LlvmImportTypedValue index_value;
};

struct AArch64LlvmImportShuffleVectorSpec {
    AArch64LlvmImportTypedValue lhs_value;
    AArch64LlvmImportTypedValue rhs_value;
    AArch64LlvmImportTypedValue mask_value;
};

struct AArch64LlvmImportVectorReduceAddSpec {
    std::string return_type_text;
    AArch64LlvmImportType return_type;
    AArch64LlvmImportTypedValue vector_value;
};

struct AArch64LlvmImportCompareSpec {
    bool is_float_compare = false;
    std::string predicate_text;
    AArch64LlvmImportTypedValue lhs;
    AArch64LlvmImportTypedValue rhs;
};

struct AArch64LlvmImportSelectSpec {
    AArch64LlvmImportTypedValue condition;
    AArch64LlvmImportTypedValue true_value;
    AArch64LlvmImportTypedValue false_value;
};

struct AArch64LlvmImportPhiIncoming {
    AArch64LlvmImportTypedValue value;
    std::string block_label;
};

struct AArch64LlvmImportPhiSpec {
    std::string type_text;
    AArch64LlvmImportType type;
    std::vector<AArch64LlvmImportPhiIncoming> incoming_values;
};

struct AArch64LlvmImportBranchSpec {
    bool is_conditional = false;
    AArch64LlvmImportTypedValue condition;
    std::string true_target_label;
    std::string false_target_label;
};

struct AArch64LlvmImportIndirectBranchSpec {
    AArch64LlvmImportTypedValue address;
    std::vector<std::string> target_labels;
};

struct AArch64LlvmImportSwitchCase {
    std::uint64_t value = 0;
    std::string target_label;
};

struct AArch64LlvmImportSwitchSpec {
    AArch64LlvmImportTypedValue selector;
    std::string default_target_label;
    std::vector<AArch64LlvmImportSwitchCase> cases;
};

struct AArch64LlvmImportReturnSpec {
    bool is_void = false;
    AArch64LlvmImportTypedValue value;
};

std::optional<AArch64LlvmImportCompareSpec>
parse_llvm_import_compare_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportBinarySpec>
parse_llvm_import_binary_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportUnarySpec>
parse_llvm_import_unary_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportCastSpec>
parse_llvm_import_cast_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportAllocaSpec>
parse_llvm_import_alloca_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportLoadSpec>
parse_llvm_import_load_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportStoreSpec>
parse_llvm_import_store_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportGetElementPtrSpec>
parse_llvm_import_gep_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportCallSpec>
parse_llvm_import_call_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportExtractElementSpec>
parse_llvm_import_extractelement_spec(
    const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportInsertElementSpec>
parse_llvm_import_insertelement_spec(
    const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportShuffleVectorSpec>
parse_llvm_import_shufflevector_spec(
    const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportVectorReduceAddSpec>
parse_llvm_import_vector_reduce_add_spec(
    const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportSelectSpec>
parse_llvm_import_select_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportPhiSpec>
parse_llvm_import_phi_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportBranchSpec>
parse_llvm_import_branch_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportIndirectBranchSpec>
parse_llvm_import_indirect_branch_spec(
    const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportSwitchSpec>
parse_llvm_import_switch_spec(const AArch64LlvmImportInstruction &instruction);

std::optional<AArch64LlvmImportReturnSpec>
parse_llvm_import_return_spec(const AArch64LlvmImportInstruction &instruction);

} // namespace sysycc
