#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

namespace sysycc {

enum class AArch64LlvmImportTypeKind : unsigned char {
    Unknown,
    Void,
    Pointer,
    Float16,
    Float32,
    Float64,
    Float128,
    Integer,
    Array,
    Struct,
    Named,
};

struct AArch64LlvmImportType {
    AArch64LlvmImportTypeKind kind = AArch64LlvmImportTypeKind::Unknown;
    std::size_t integer_bit_width = 0;
    std::size_t array_element_count = 0;
    bool array_uses_vector_syntax = false;
    std::string named_type_name;
    std::vector<AArch64LlvmImportType> element_types;

    bool is_valid() const {
        return kind != AArch64LlvmImportTypeKind::Unknown;
    }
};

enum class AArch64LlvmImportConstantKind : unsigned char {
    Invalid,
    Integer,
    Float,
    NullPointer,
    ZeroInitializer,
    SymbolReference,
    Bitcast,
    IntToPtr,
    PtrToInt,
    GetElementPtr,
    Aggregate,
};

struct AArch64LlvmImportConstant {
    AArch64LlvmImportConstantKind kind =
        AArch64LlvmImportConstantKind::Invalid;
    std::uint64_t integer_value = 0;
    std::string float_text;
    std::string symbol_name;
    std::string cast_source_type_text;
    AArch64LlvmImportType cast_source_type;
    std::string cast_target_type_text;
    std::shared_ptr<AArch64LlvmImportConstant> cast_operand;
    bool gep_is_inbounds = false;
    std::string gep_source_type_text;
    AArch64LlvmImportType gep_source_type;
    std::shared_ptr<AArch64LlvmImportConstant> gep_base;
    std::vector<std::string> gep_index_type_texts;
    std::vector<AArch64LlvmImportType> gep_index_types;
    std::vector<AArch64LlvmImportConstant> gep_indices;
    std::vector<AArch64LlvmImportConstant> elements;

    bool is_valid() const {
        return kind != AArch64LlvmImportConstantKind::Invalid;
    }
};

struct AArch64LlvmImportNamedType {
    std::string name;
    std::string body_text;
    AArch64LlvmImportType body_type;
    bool is_opaque = false;
    int line = 0;
};

struct AArch64LlvmImportGlobal {
    std::string name;
    std::string type_text;
    AArch64LlvmImportType type;
    std::string initializer_text;
    AArch64LlvmImportConstant initializer;
    bool is_internal_linkage = false;
    bool is_constant = false;
    int line = 0;
};

struct AArch64LlvmImportAlias {
    std::string name;
    std::string target_type_text;
    AArch64LlvmImportType target_type;
    std::string target_text;
    AArch64LlvmImportConstant target;
    int line = 0;
};

struct AArch64LlvmImportParameter {
    std::string type_text;
    AArch64LlvmImportType type;
    std::string name;
};

enum class AArch64LlvmImportInstructionKind : unsigned char {
    Unknown,
    Binary,
    Unary,
    Compare,
    Cast,
    Alloca,
    Load,
    Store,
    GetElementPtr,
    Call,
    Select,
    Phi,
    Branch,
    CondBranch,
    Return,
    ExtractElement,
    InsertElement,
    ShuffleVector,
    VectorReduceAdd,
};

struct AArch64LlvmImportInstruction {
    AArch64LlvmImportInstructionKind kind =
        AArch64LlvmImportInstructionKind::Unknown;
    std::string result_name;
    std::string opcode_text;
    std::string canonical_text;
    int line = 0;
};

struct AArch64LlvmImportBasicBlock {
    std::string label;
    std::vector<AArch64LlvmImportInstruction> instructions;
};

struct AArch64LlvmImportFunction {
    std::string name;
    std::string return_type_text;
    AArch64LlvmImportType return_type;
    std::vector<AArch64LlvmImportParameter> parameters;
    bool is_internal_linkage = false;
    bool is_variadic = false;
    bool is_definition = false;
    std::vector<AArch64LlvmImportBasicBlock> basic_blocks;
    int line = 0;
};

struct AArch64LlvmImportModule {
    std::string source_name;
    std::string source_target_triple;
    std::vector<std::string> module_asm_lines;
    std::vector<AArch64LlvmImportNamedType> named_types;
    std::vector<AArch64LlvmImportGlobal> globals;
    std::vector<AArch64LlvmImportAlias> aliases;
    std::vector<AArch64LlvmImportFunction> functions;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

} // namespace sysycc
