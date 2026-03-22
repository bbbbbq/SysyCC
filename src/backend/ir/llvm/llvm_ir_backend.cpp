#include "backend/ir/llvm/llvm_ir_backend.hpp"

#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "backend/ir/detail/aggregate_layout.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc {

namespace {

const SemanticType *strip_qualifiers(const SemanticType *type) {
    return ::sysycc::detail::strip_qualifiers(type);
}

std::size_t get_type_size(const SemanticType *type);
std::size_t get_type_alignment(const SemanticType *type);

bool is_floating_llvm_type_name(const std::string &type_name) {
    return type_name == "half" || type_name == "float" ||
           type_name == "double" || type_name == "fp128";
}

bool is_pointer_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    return type != nullptr && type->get_kind() == SemanticTypeKind::Pointer;
}

bool is_builtin_type_named(const SemanticType *type, const char *name) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    return static_cast<const BuiltinSemanticType *>(type)->get_name() == name;
}

std::string get_default_target_triple() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple-macosx15.0.0";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-macosx10.15.0";
#elif defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

std::string get_default_target_datalayout() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "e-m:o-i64:64-i128:128-n32:64-S128-Fn32";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "e-m:o-i64:64-f80:128-n8:16:32:64-S128";
#elif defined(__aarch64__)
    return "e-m:e-i64:64-i128:128-n32:64-S128";
#elif defined(__x86_64__)
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
#else
    return "";
#endif
}

std::string normalize_floating_literal(std::string value_text) {
    while (!value_text.empty()) {
        const char last = value_text.back();
        if (last == 'f' || last == 'F' || last == 'l' || last == 'L') {
            value_text.pop_back();
            continue;
        }
        break;
    }

    if (value_text.empty()) {
        return "0.0";
    }

    const bool is_hex_float =
        value_text.find("0x") != std::string::npos ||
        value_text.find("0X") != std::string::npos ||
        value_text.find('p') != std::string::npos ||
        value_text.find('P') != std::string::npos;
    if (is_hex_float) {
        try {
            const long double parsed_value = std::stold(value_text);
            std::ostringstream stream;
            stream << std::scientific << std::setprecision(17)
                   << static_cast<double>(parsed_value);
            return stream.str();
        } catch (...) {
            return "0.0";
        }
    }

    const bool has_decimal_point = value_text.find('.') != std::string::npos;
    const bool has_exponent = value_text.find('e') != std::string::npos ||
                              value_text.find('E') != std::string::npos;
    if (!value_text.empty() && value_text.front() == '.') {
        value_text.insert(value_text.begin(), '0');
    }
    if (!has_decimal_point && !has_exponent) {
        value_text += ".0";
    }
    return value_text;
}

std::string normalize_float_literal(const std::string &value_text) {
    try {
        const long double parsed_value = std::stold(value_text);
        std::ostringstream stream;
        stream << std::scientific << std::setprecision(16)
               << static_cast<double>(static_cast<float>(parsed_value));
        return stream.str();
    } catch (...) {
        return normalize_floating_literal(value_text);
    }
}

std::string decode_string_literal_token(std::string token_text) {
    if (token_text.size() >= 2 && token_text.front() == '"' &&
        token_text.back() == '"') {
        token_text = token_text.substr(1, token_text.size() - 2);
    }

    std::string decoded;
    decoded.reserve(token_text.size());
    for (std::size_t index = 0; index < token_text.size(); ++index) {
        char ch = token_text[index];
        if (ch == '\\' && index + 1 < token_text.size()) {
            const char escaped = token_text[++index];
            switch (escaped) {
            case '\\':
            case '"':
            case '\'':
                decoded.push_back(escaped);
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case '0':
                decoded.push_back('\0');
                break;
            default:
                decoded.push_back(escaped);
                break;
            }
            continue;
        }
        decoded.push_back(ch);
    }
    return decoded;
}

std::string encode_llvm_string_bytes(const std::string &decoded_text) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex << std::setfill('0');
    for (const unsigned char ch : decoded_text) {
        if (std::isprint(ch) != 0 && ch != '\\' && ch != '"') {
            encoded << static_cast<char>(ch);
            continue;
        }
        encoded << '\\' << std::setw(2) << static_cast<int>(ch);
    }
    encoded << "\\00";
    return encoded.str();
}

std::optional<int> get_floating_type_rank(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return std::nullopt;
    }

    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    if (name == "_Float16") {
        return 1;
    }
    if (name == "float") {
        return 2;
    }
    if (name == "double") {
        return 3;
    }
    if (name == "long double") {
        return 4;
    }
    return std::nullopt;
}

std::string get_llvm_type_name(const SemanticType *type) {
    return ::sysycc::detail::get_llvm_type_name(type);
}

std::size_t get_type_alignment(const SemanticType *type) {
    return ::sysycc::detail::get_type_alignment(type);
}

std::size_t get_type_size(const SemanticType *type) {
    return ::sysycc::detail::get_type_size(type);
}

} // namespace

IrKind LlvmIrBackend::get_kind() const noexcept { return IrKind::LLVM; }

void LlvmIrBackend::begin_module() {
    module_header_.str("");
    module_header_.clear();
    declarations_.str("");
    declarations_.clear();
    output_.str("");
    output_.clear();
    ir_context_.reset();
    address_counts_.clear();
    declared_function_signatures_.clear();
    declared_globals_.clear();
    defined_globals_.clear();
    string_literal_globals_.clear();

    const std::string target_triple = get_default_target_triple();
    const std::string target_datalayout = get_default_target_datalayout();
    if (!target_datalayout.empty()) {
        module_header_ << "target datalayout = \"" << target_datalayout
                       << "\"\n";
    }
    if (!target_triple.empty()) {
        module_header_ << "target triple = \"" << target_triple << "\"\n";
    }
    if (!target_datalayout.empty() || !target_triple.empty()) {
        module_header_ << "\n";
    }
}

void LlvmIrBackend::end_module() {}

void LlvmIrBackend::declare_global(const std::string &name,
                                   const SemanticType *type,
                                   bool is_internal_linkage) {
    if (defined_globals_.find(name) != defined_globals_.end() ||
        declared_globals_.find(name) != declared_globals_.end()) {
        return;
    }
    declared_globals_.insert(name);
    declarations_ << "@" << name << " = "
                  << (is_internal_linkage ? "internal " : "external ")
                  << "global "
                  << get_llvm_type_name(type) << "\n";
}

void LlvmIrBackend::define_global(const std::string &name,
                                  const SemanticType *type,
                                  const std::string &initializer_text,
                                  bool is_internal_linkage) {
    if (defined_globals_.find(name) != defined_globals_.end()) {
        return;
    }
    defined_globals_.insert(name);
    declared_globals_.erase(name);
    declarations_ << "@" << name << " = "
                  << (is_internal_linkage ? "internal " : "") << "global "
                  << get_llvm_type_name(type)
                  << " " << initializer_text << "\n";
}

void LlvmIrBackend::declare_function(
    const std::string &name, const SemanticType *return_type,
    const std::vector<const SemanticType *> &parameter_types,
    bool is_variadic, bool is_internal_linkage) {
    std::ostringstream signature;
    signature << name << ":" << get_llvm_type_name(return_type) << "(";
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
        if (index > 0) {
            signature << ",";
        }
        signature << get_llvm_type_name(parameter_types[index]);
    }
    if (is_variadic) {
        if (!parameter_types.empty()) {
            signature << ",";
        }
        signature << "...";
    }
    signature << ")";

    const std::string signature_text = signature.str();
    if (declared_function_signatures_.find(signature_text) !=
        declared_function_signatures_.end()) {
        return;
    }

    declared_function_signatures_.insert(signature_text);
    declarations_ << "declare "
                  << (is_internal_linkage ? "internal " : "")
                  << get_llvm_type_name(return_type) << " @"
                  << name << "(";
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
        if (index > 0) {
            declarations_ << ", ";
        }
        declarations_ << get_llvm_type_name(parameter_types[index]);
    }
    if (is_variadic) {
        if (!parameter_types.empty()) {
            declarations_ << ", ";
        }
        declarations_ << "...";
    }
    declarations_ << ")\n";
}

void LlvmIrBackend::begin_function(const std::string &name,
                                   const SemanticType *return_type,
                                   const std::vector<IRFunctionParameter> &parameters,
                                   bool is_variadic,
                                   const std::vector<IRFunctionAttribute>
                                       &attributes,
                                   bool is_internal_linkage) {
    output_ << "define " << (is_internal_linkage ? "internal " : "")
            << get_llvm_type_name(return_type) << " @" << name
            << "(";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) {
            output_ << ", ";
        }
        output_ << get_llvm_type_name(parameters[index].type) << " %"
                << parameters[index].name;
    }
    if (is_variadic) {
        if (!parameters.empty()) {
            output_ << ", ";
        }
        output_ << "...";
    }
    output_ << ")";
    for (const IRFunctionAttribute attribute : attributes) {
        if (attribute == IRFunctionAttribute::AlwaysInline) {
            output_ << " alwaysinline";
        }
    }
    output_ << " {\n";
    output_ << "entry:\n";
}

void LlvmIrBackend::end_function() { output_ << "}\n"; }

std::string LlvmIrBackend::create_label(const std::string &hint) {
    const std::string base = hint.empty() ? "label" : hint;
    return base + std::to_string(ir_context_.allocate_label_id());
}

void LlvmIrBackend::emit_label(const std::string &label) {
    output_ << label << ":\n";
}

void LlvmIrBackend::emit_branch(const std::string &target_label) {
    output_ << "  br label %" << target_label << "\n";
}

void LlvmIrBackend::emit_cond_branch(const IRValue &condition,
                                     const std::string &true_label,
                                     const std::string &false_label) {
    std::string condition_text = condition.text;
    if (condition.type != nullptr && get_llvm_type_name(condition.type) != "i1") {
        const std::string lowered_condition = ir_context_.get_temp_name();
        if (is_floating_llvm_type_name(get_llvm_type_name(condition.type))) {
            output_ << "  " << lowered_condition << " = fcmp une "
                    << get_llvm_type_name(condition.type) << " "
                    << condition.text << ", 0.0\n";
        } else if (is_pointer_type(condition.type)) {
            output_ << "  " << lowered_condition << " = icmp ne ptr "
                    << condition.text << ", null\n";
        } else {
            output_ << "  " << lowered_condition << " = icmp ne "
                    << get_llvm_type_name(condition.type) << " "
                    << condition.text << ", 0\n";
        }
        condition_text = lowered_condition;
    }

    output_ << "  br i1 " << condition_text << ", label %" << true_label
            << ", label %" << false_label << "\n";
}

IRValue LlvmIrBackend::emit_integer_literal(int value) {
    static BuiltinSemanticType int_type("int");
    return {std::to_string(value), &int_type};
}

IRValue LlvmIrBackend::emit_floating_literal(const std::string &value_text,
                                             const SemanticType *type) {
    if (is_builtin_type_named(type, "float")) {
        return {normalize_float_literal(value_text), type};
    }
    return {normalize_floating_literal(value_text), type};
}

IRValue LlvmIrBackend::emit_string_literal(const std::string &value_text,
                                           const SemanticType *type) {
    const std::string decoded_text = decode_string_literal_token(value_text);
    std::string global_name;

    const auto existing = string_literal_globals_.find(decoded_text);
    if (existing != string_literal_globals_.end()) {
        global_name = existing->second;
    } else {
        global_name =
            ".str." + std::to_string(string_literal_globals_.size());
        string_literal_globals_.emplace(decoded_text, global_name);
        declarations_ << "@" << global_name
                      << " = private unnamed_addr constant ["
                      << (decoded_text.size() + 1) << " x i8] c\""
                      << encode_llvm_string_bytes(decoded_text) << "\"\n";
    }

    const std::string result = ir_context_.get_temp_name();
    output_ << "  " << result << " = getelementptr inbounds ["
            << (decoded_text.size() + 1) << " x i8], ptr @" << global_name
            << ", i32 0, i32 0\n";
    return {result, type};
}

std::string LlvmIrBackend::emit_alloca(const std::string &name,
                                       const SemanticType *type) {
    std::string sanitized_name = name.empty() ? "addr" : name;
    int &count = address_counts_[sanitized_name];
    const std::string suffix = count == 0 ? "" : std::to_string(count);
    ++count;

    const std::string address = "%" + sanitized_name + ".addr" + suffix;
    output_ << "  " << address << " = alloca " << get_llvm_type_name(type)
            << "\n";
    return address;
}

std::string LlvmIrBackend::emit_member_address(const std::string &base_address,
                                               const SemanticType *owner_type,
                                               std::size_t field_index,
                                               const SemanticType *field_type) {
    owner_type = strip_qualifiers(owner_type);
    const auto field_layout =
        ::sysycc::detail::get_aggregate_field_layout(owner_type, field_index);
    if (!field_layout.has_value()) {
        return "";
    }
    const std::string result = ir_context_.get_temp_name();
    if (owner_type != nullptr && owner_type->get_kind() == SemanticTypeKind::Struct) {
        output_ << "  " << result << " = getelementptr inbounds "
                << get_llvm_type_name(owner_type) << ", ptr " << base_address
                << ", i32 0, i32 " << field_layout->llvm_element_index << "\n";
        return result;
    }

    if (owner_type != nullptr && owner_type->get_kind() == SemanticTypeKind::Union) {
        output_ << "  " << result << " = bitcast ptr " << base_address
                << " to ptr\n";
        return result;
    }

    return "";
}

std::string LlvmIrBackend::emit_element_address(const std::string &base_address,
                                                const SemanticType *element_type,
                                                const IRValue &index_value) {
    if (element_type == nullptr || index_value.type == nullptr) {
        return "";
    }

    const std::string result = ir_context_.get_temp_name();
    output_ << "  " << result << " = getelementptr inbounds "
            << get_llvm_type_name(element_type) << ", ptr " << base_address
            << ", " << get_llvm_type_name(index_value.type) << " "
            << index_value.text << "\n";
    return result;
}

IRValue LlvmIrBackend::emit_pointer_difference(const IRValue &lhs_pointer,
                                               const IRValue &rhs_pointer,
                                               const SemanticType *pointee_type,
                                               const SemanticType *result_type) {
    if (pointee_type == nullptr || result_type == nullptr) {
        return {"", result_type};
    }

    const std::size_t pointee_size = get_type_size(pointee_type);
    if (pointee_size == 0) {
        return {"", result_type};
    }

    const std::string lhs_int = ir_context_.get_temp_name();
    const std::string rhs_int = ir_context_.get_temp_name();
    const std::string diff_int = ir_context_.get_temp_name();
    const std::string elems_int = ir_context_.get_temp_name();

    output_ << "  " << lhs_int << " = ptrtoint ptr " << lhs_pointer.text
            << " to i64\n";
    output_ << "  " << rhs_int << " = ptrtoint ptr " << rhs_pointer.text
            << " to i64\n";
    output_ << "  " << diff_int << " = sub i64 " << lhs_int << ", " << rhs_int
            << "\n";
    output_ << "  " << elems_int << " = sdiv i64 " << diff_int << ", "
            << pointee_size << "\n";

    const std::string result_type_name = get_llvm_type_name(result_type);
    if (result_type_name == "i64") {
        return {elems_int, result_type};
    }
    if (result_type_name == "i32") {
        const std::string truncated = ir_context_.get_temp_name();
        output_ << "  " << truncated << " = trunc i64 " << elems_int
                << " to i32\n";
        return {truncated, result_type};
    }
    return {"", result_type};
}

void LlvmIrBackend::emit_store(const std::string &address,
                               const IRValue &value) {
    output_ << "  store " << get_llvm_type_name(value.type) << " "
            << value.text << ", ptr " << address << "\n";
}

IRValue LlvmIrBackend::emit_load(const std::string &address,
                                 const SemanticType *type) {
    const std::string result = ir_context_.get_temp_name();
    output_ << "  " << result << " = load " << get_llvm_type_name(type)
            << ", ptr " << address << "\n";
    return {result, type};
}

IRValue LlvmIrBackend::emit_binary(const std::string &op, const IRValue &lhs,
                                   const IRValue &rhs,
                                   const SemanticType *result_type) {
    std::string llvm_opcode;
    bool is_comparison = false;
    detail::IntegerConversionService integer_conversion_service;
    const bool is_floating_result =
        is_floating_llvm_type_name(get_llvm_type_name(result_type));
    const bool is_floating_operand =
        is_floating_llvm_type_name(get_llvm_type_name(lhs.type));
    const auto lhs_integer_info =
        integer_conversion_service.get_integer_type_info(lhs.type);
    const bool is_unsigned_integer =
        lhs_integer_info.has_value() && !lhs_integer_info->get_is_signed();
    if (op == "+") {
        llvm_opcode = is_floating_result ? "fadd" : "add";
    } else if (op == "-") {
        llvm_opcode = is_floating_result ? "fsub" : "sub";
    } else if (op == "*") {
        llvm_opcode = is_floating_result ? "fmul" : "mul";
    } else if (op == "/") {
        llvm_opcode = is_floating_result ? "fdiv" :
                      (is_unsigned_integer ? "udiv" : "sdiv");
    } else if (op == "%") {
        llvm_opcode = is_unsigned_integer ? "urem" : "srem";
    } else if (op == "<<") {
        llvm_opcode = "shl";
    } else if (op == ">>") {
        llvm_opcode = is_unsigned_integer ? "lshr" : "ashr";
    } else if (op == "&") {
        llvm_opcode = "and";
    } else if (op == "|") {
        llvm_opcode = "or";
    } else if (op == "^") {
        llvm_opcode = "xor";
    } else if (op == "<") {
        llvm_opcode = is_floating_operand ? "fcmp olt" :
                      (is_unsigned_integer ? "icmp ult" : "icmp slt");
        is_comparison = true;
    } else if (op == "<=") {
        llvm_opcode = is_floating_operand ? "fcmp ole" :
                      (is_unsigned_integer ? "icmp ule" : "icmp sle");
        is_comparison = true;
    } else if (op == ">") {
        llvm_opcode = is_floating_operand ? "fcmp ogt" :
                      (is_unsigned_integer ? "icmp ugt" : "icmp sgt");
        is_comparison = true;
    } else if (op == ">=") {
        llvm_opcode = is_floating_operand ? "fcmp oge" :
                      (is_unsigned_integer ? "icmp uge" : "icmp sge");
        is_comparison = true;
    } else if (op == "==") {
        llvm_opcode = is_floating_operand ? "fcmp oeq" : "icmp eq";
        is_comparison = true;
    } else if (op == "!=") {
        llvm_opcode = is_floating_operand ? "fcmp une" : "icmp ne";
        is_comparison = true;
    } else {
        return {"", result_type};
    }

    const std::string result = ir_context_.get_temp_name();
    if (!is_comparison) {
        output_ << "  " << result << " = " << llvm_opcode << " "
                << get_llvm_type_name(result_type) << " " << lhs.text << ", "
                << rhs.text << "\n";
        return {result, result_type};
    }

    const std::string comparison = ir_context_.get_temp_name();
    std::string comparison_type_name = get_llvm_type_name(lhs.type);
    std::string lhs_operand = lhs.text;
    std::string rhs_operand = rhs.text;
    if (is_pointer_type(lhs.type) || is_pointer_type(rhs.type)) {
        comparison_type_name = "ptr";
        if (!is_pointer_type(lhs.type) && lhs.text == "0") {
            lhs_operand = "null";
        }
        if (!is_pointer_type(rhs.type) && rhs.text == "0") {
            rhs_operand = "null";
        }
    }
    output_ << "  " << comparison << " = " << llvm_opcode << " "
            << comparison_type_name << " " << lhs_operand << ", "
            << rhs_operand << "\n";
    output_ << "  " << result << " = zext i1 " << comparison << " to "
            << get_llvm_type_name(result_type) << "\n";
    return {result, result_type};
}

IRValue LlvmIrBackend::emit_cast(const IRValue &value,
                                 const SemanticType *target_type) {
    const std::string source_type_name = get_llvm_type_name(value.type);
    const std::string target_type_name = get_llvm_type_name(target_type);
    if (source_type_name == target_type_name) {
        return {value.text, target_type};
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto integer_conversion_plan =
        integer_conversion_service.get_integer_conversion_plan(value.type,
                                                               target_type);
    if (integer_conversion_plan.get_kind() !=
        detail::IntegerConversionKind::Unsupported) {
        return emit_integer_conversion(value, integer_conversion_plan.get_kind(),
                                       target_type);
    }

    const std::string result = ir_context_.get_temp_name();
    const auto source_float_rank = get_floating_type_rank(value.type);
    const auto target_float_rank = get_floating_type_rank(target_type);
    const auto source_integer_info =
        integer_conversion_service.get_integer_type_info(value.type);
    const auto target_integer_info =
        integer_conversion_service.get_integer_type_info(target_type);
    const SemanticType *source_unqualified = strip_qualifiers(value.type);
    const SemanticType *target_unqualified = strip_qualifiers(target_type);

    if (source_unqualified != nullptr && target_integer_info.has_value() &&
        source_unqualified->get_kind() == SemanticTypeKind::Pointer) {
        output_ << "  " << result << " = ptrtoint ptr " << value.text << " to "
                << target_type_name << "\n";
        return {result, target_type};
    }

    if (source_integer_info.has_value() && target_unqualified != nullptr &&
        target_unqualified->get_kind() == SemanticTypeKind::Pointer) {
        output_ << "  " << result << " = inttoptr " << source_type_name << " "
                << value.text << " to ptr\n";
        return {result, target_type};
    }

    if (source_float_rank.has_value() && target_float_rank.has_value()) {
        if (*source_float_rank < *target_float_rank) {
            output_ << "  " << result << " = fpext " << source_type_name << " "
                    << value.text << " to " << target_type_name << "\n";
            return {result, target_type};
        }
        output_ << "  " << result << " = fptrunc " << source_type_name << " "
                << value.text << " to " << target_type_name << "\n";
        return {result, target_type};
    }

    if (source_integer_info.has_value() && target_float_rank.has_value()) {
        const char *opcode = source_integer_info->get_is_signed() ? "sitofp"
                                                                 : "uitofp";
        output_ << "  " << result << " = " << opcode << " "
                << source_type_name << " " << value.text << " to "
                << target_type_name << "\n";
        return {result, target_type};
    }

    if (source_float_rank.has_value() && target_integer_info.has_value()) {
        const char *opcode = target_integer_info->get_is_signed() ? "fptosi"
                                                                 : "fptoui";
        output_ << "  " << result << " = " << opcode << " "
                << source_type_name << " " << value.text << " to "
                << target_type_name << "\n";
        return {result, target_type};
    }

    return {"", target_type};
}

IRValue LlvmIrBackend::emit_integer_conversion(
    const IRValue &value, detail::IntegerConversionKind conversion_kind,
    const SemanticType *target_type) {
    const std::string source_type_name = get_llvm_type_name(value.type);
    const std::string target_type_name = get_llvm_type_name(target_type);
    if (source_type_name == target_type_name ||
        conversion_kind == detail::IntegerConversionKind::None) {
        return {value.text, target_type};
    }

    const std::string result = ir_context_.get_temp_name();
    if (conversion_kind == detail::IntegerConversionKind::Truncate) {
        output_ << "  " << result << " = trunc " << source_type_name << " "
                << value.text << " to " << target_type_name << "\n";
        return {result, target_type};
    }
    if (conversion_kind == detail::IntegerConversionKind::SignExtend) {
        output_ << "  " << result << " = sext " << source_type_name << " "
                << value.text << " to " << target_type_name << "\n";
        return {result, target_type};
    }
    if (conversion_kind == detail::IntegerConversionKind::ZeroExtend) {
        output_ << "  " << result << " = zext " << source_type_name << " "
                << value.text << " to " << target_type_name << "\n";
        return {result, target_type};
    }

    return {"", target_type};
}

IRValue LlvmIrBackend::emit_call(const std::string &callee,
                                 const std::vector<IRValue> &arguments,
                                 const SemanticType *return_type,
                                 const std::vector<const SemanticType *>
                                     &parameter_types,
                                 bool is_variadic) {
    const std::string return_type_name = get_llvm_type_name(return_type);
    std::ostringstream callee_signature;
    if (is_variadic) {
        callee_signature << return_type_name << " (";
        for (std::size_t index = 0; index < parameter_types.size(); ++index) {
            if (index > 0) {
                callee_signature << ", ";
            }
            callee_signature << get_llvm_type_name(parameter_types[index]);
        }
        if (!parameter_types.empty()) {
            callee_signature << ", ";
        }
        callee_signature << "...) " << callee;
    } else {
        callee_signature << return_type_name << " " << callee;
    }

    if (return_type_name == "void") {
        output_ << "  call " << callee_signature.str() << "(";
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index > 0) {
                output_ << ", ";
            }
            output_ << get_llvm_type_name(arguments[index].type) << " "
                    << arguments[index].text;
        }
        output_ << ")\n";
        return {"", return_type};
    }

    const std::string result = ir_context_.get_temp_name();
    output_ << "  " << result << " = call " << callee_signature.str() << "(";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index > 0) {
            output_ << ", ";
        }
        output_ << get_llvm_type_name(arguments[index].type) << " "
                << arguments[index].text;
    }
    output_ << ")\n";
    return {result, return_type};
}

void LlvmIrBackend::emit_return(const IRValue &value) {
    output_ << "  ret " << get_llvm_type_name(value.type) << " " << value.text
            << "\n";
}

void LlvmIrBackend::emit_return_void() { output_ << "  ret void\n"; }

std::string LlvmIrBackend::get_output_text() const {
    const std::string header_text = module_header_.str();
    const std::string declarations_text = declarations_.str();
    const std::string body_text = output_.str();
    if (declarations_text.empty()) {
        return header_text + body_text;
    }
    return header_text + declarations_text + "\n" + body_text;
}

} // namespace sysycc
