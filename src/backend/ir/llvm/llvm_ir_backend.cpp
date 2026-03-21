#include "backend/ir/llvm/llvm_ir_backend.hpp"

#include <string>
#include <utility>
#include <vector>

#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc {

namespace {

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

std::size_t get_type_size(const SemanticType *type);
std::size_t get_type_alignment(const SemanticType *type);

bool is_floating_llvm_type_name(const std::string &type_name) {
    return type_name == "half" || type_name == "float" ||
           type_name == "double" || type_name == "fp128";
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
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return "void";
    }

    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto *builtin_type = static_cast<const BuiltinSemanticType *>(type);
        if (builtin_type->get_name() == "int") {
            return "i32";
        }
        if (builtin_type->get_name() == "ptrdiff_t") {
            return "i64";
        }
        if (builtin_type->get_name() == "unsigned int") {
            return "i32";
        }
        if (builtin_type->get_name() == "char" ||
            builtin_type->get_name() == "signed char" ||
            builtin_type->get_name() == "unsigned char") {
            return "i8";
        }
        if (builtin_type->get_name() == "short" ||
            builtin_type->get_name() == "unsigned short") {
            return "i16";
        }
        if (builtin_type->get_name() == "long int" ||
            builtin_type->get_name() == "long long int" ||
            builtin_type->get_name() == "unsigned long long") {
            return "i64";
        }
        if (builtin_type->get_name() == "void") {
            return "void";
        }
        if (builtin_type->get_name() == "float") {
            return "float";
        }
        if (builtin_type->get_name() == "double") {
            return "double";
        }
        if (builtin_type->get_name() == "_Float16") {
            return "half";
        }
        if (builtin_type->get_name() == "long double") {
            return "fp128";
        }
    }

    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return "ptr";
    }

    if (type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        std::string result = "{ ";
        for (std::size_t index = 0; index < struct_type->get_fields().size();
             ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += get_llvm_type_name(struct_type->get_fields()[index].get_type());
        }
        result += " }";
        return result;
    }

    if (type->get_kind() == SemanticTypeKind::Union) {
        return "[" +
               std::to_string(get_type_size(type)) +
               " x i8]";
    }

    return "void";
}

std::size_t get_type_alignment(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return 1;
    }
    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
        if (name == "char" || name == "signed char" || name == "unsigned char") {
            return 1;
        }
        if (name == "short" || name == "unsigned short") {
            return 2;
        }
        if (name == "int" || name == "unsigned int" || name == "float") {
            return 4;
        }
        if (name == "double" || name == "ptrdiff_t" || name == "long int" ||
            name == "long long int" || name == "unsigned long long") {
            return 8;
        }
        if (name == "_Float16") {
            return 2;
        }
        if (name == "long double") {
            return 16;
        }
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return 8;
    }
    if (type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        std::size_t max_alignment = 1;
        for (const auto &field : struct_type->get_fields()) {
            max_alignment = std::max(max_alignment,
                                     get_type_alignment(field.get_type()));
        }
        return max_alignment;
    }
    if (type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        std::size_t max_alignment = 1;
        for (const auto &field : union_type->get_fields()) {
            max_alignment = std::max(max_alignment,
                                     get_type_alignment(field.get_type()));
        }
        return max_alignment;
    }
    return 1;
}

std::size_t get_type_size(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return 0;
    }
    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
        if (name == "char" || name == "signed char" || name == "unsigned char") {
            return 1;
        }
        if (name == "short" || name == "unsigned short") {
            return 2;
        }
        if (name == "int" || name == "unsigned int" || name == "float") {
            return 4;
        }
        if (name == "double" || name == "ptrdiff_t" || name == "long int" ||
            name == "long long int" || name == "unsigned long long") {
            return 8;
        }
        if (name == "_Float16") {
            return 2;
        }
        if (name == "long double") {
            return 16;
        }
        return 0;
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return 8;
    }
    if (type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        std::size_t total_size = 0;
        std::size_t max_alignment = 1;
        for (const auto &field : struct_type->get_fields()) {
            const std::size_t alignment = get_type_alignment(field.get_type());
            const std::size_t size = get_type_size(field.get_type());
            max_alignment = std::max(max_alignment, alignment);
            if (alignment > 0 && total_size % alignment != 0) {
                total_size += alignment - (total_size % alignment);
            }
            total_size += size;
        }
        if (max_alignment > 0 && total_size % max_alignment != 0) {
            total_size += max_alignment - (total_size % max_alignment);
        }
        return total_size;
    }
    if (type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        std::size_t max_size = 0;
        std::size_t max_alignment = 1;
        for (const auto &field : union_type->get_fields()) {
            max_size = std::max(max_size, get_type_size(field.get_type()));
            max_alignment = std::max(max_alignment,
                                     get_type_alignment(field.get_type()));
        }
        if (max_alignment > 0 && max_size % max_alignment != 0) {
            max_size += max_alignment - (max_size % max_alignment);
        }
        return max_size;
    }
    return 0;
}

} // namespace

IrKind LlvmIrBackend::get_kind() const noexcept { return IrKind::LLVM; }

void LlvmIrBackend::begin_module() {
    declarations_.str("");
    declarations_.clear();
    output_.str("");
    output_.clear();
    ir_context_.reset();
    address_counts_.clear();
    declared_function_signatures_.clear();
    declared_globals_.clear();
    defined_globals_.clear();
}

void LlvmIrBackend::end_module() {}

void LlvmIrBackend::declare_global(const std::string &name,
                                   const SemanticType *type) {
    if (defined_globals_.find(name) != defined_globals_.end() ||
        declared_globals_.find(name) != declared_globals_.end()) {
        return;
    }
    declared_globals_.insert(name);
    declarations_ << "@" << name << " = external global "
                  << get_llvm_type_name(type) << "\n";
}

void LlvmIrBackend::define_global(const std::string &name,
                                  const SemanticType *type,
                                  const std::string &initializer_text) {
    if (defined_globals_.find(name) != defined_globals_.end()) {
        return;
    }
    defined_globals_.insert(name);
    declared_globals_.erase(name);
    declarations_ << "@" << name << " = global " << get_llvm_type_name(type)
                  << " " << initializer_text << "\n";
}

void LlvmIrBackend::declare_function(
    const std::string &name, const SemanticType *return_type,
    const std::vector<const SemanticType *> &parameter_types) {
    std::ostringstream signature;
    signature << name << ":" << get_llvm_type_name(return_type) << "(";
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
        if (index > 0) {
            signature << ",";
        }
        signature << get_llvm_type_name(parameter_types[index]);
    }
    signature << ")";

    const std::string signature_text = signature.str();
    if (declared_function_signatures_.find(signature_text) !=
        declared_function_signatures_.end()) {
        return;
    }

    declared_function_signatures_.insert(signature_text);
    declarations_ << "declare " << get_llvm_type_name(return_type) << " @"
                  << name << "(";
    for (std::size_t index = 0; index < parameter_types.size(); ++index) {
        if (index > 0) {
            declarations_ << ", ";
        }
        declarations_ << get_llvm_type_name(parameter_types[index]);
    }
    declarations_ << ")\n";
}

void LlvmIrBackend::begin_function(const std::string &name,
                                   const SemanticType *return_type,
                                   const std::vector<IRFunctionParameter> &parameters,
                                   const std::vector<IRFunctionAttribute>
                                       &attributes) {
    output_ << "define " << get_llvm_type_name(return_type) << " @" << name
            << "(";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) {
            output_ << ", ";
        }
        output_ << get_llvm_type_name(parameters[index].type) << " %"
                << parameters[index].name;
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
            output_ << "  " << lowered_condition << " = fcmp one "
                    << get_llvm_type_name(condition.type) << " "
                    << condition.text << ", 0.0\n";
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
    const std::string result = ir_context_.get_temp_name();
    if (owner_type != nullptr && owner_type->get_kind() == SemanticTypeKind::Struct) {
        output_ << "  " << result << " = getelementptr inbounds "
                << get_llvm_type_name(owner_type) << ", ptr " << base_address
                << ", i32 0, i32 " << field_index << "\n";
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
    if (element_type == nullptr) {
        return "";
    }

    const std::string result = ir_context_.get_temp_name();
    output_ << "  " << result << " = getelementptr inbounds "
            << get_llvm_type_name(element_type) << ", ptr " << base_address
            << ", i32 " << index_value.text << "\n";
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
    const bool is_floating_result =
        is_floating_llvm_type_name(get_llvm_type_name(result_type));
    const bool is_floating_operand =
        is_floating_llvm_type_name(get_llvm_type_name(lhs.type));
    if (op == "+") {
        llvm_opcode = is_floating_result ? "fadd" : "add";
    } else if (op == "-") {
        llvm_opcode = is_floating_result ? "fsub" : "sub";
    } else if (op == "*") {
        llvm_opcode = is_floating_result ? "fmul" : "mul";
    } else if (op == "/") {
        llvm_opcode = is_floating_result ? "fdiv" : "sdiv";
    } else if (op == "%") {
        llvm_opcode = "srem";
    } else if (op == "<") {
        llvm_opcode = is_floating_operand ? "fcmp olt" : "icmp slt";
        is_comparison = true;
    } else if (op == "<=") {
        llvm_opcode = is_floating_operand ? "fcmp ole" : "icmp sle";
        is_comparison = true;
    } else if (op == ">") {
        llvm_opcode = is_floating_operand ? "fcmp ogt" : "icmp sgt";
        is_comparison = true;
    } else if (op == ">=") {
        llvm_opcode = is_floating_operand ? "fcmp oge" : "icmp sge";
        is_comparison = true;
    } else if (op == "==") {
        llvm_opcode = is_floating_operand ? "fcmp oeq" : "icmp eq";
        is_comparison = true;
    } else if (op == "!=") {
        llvm_opcode = is_floating_operand ? "fcmp one" : "icmp ne";
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
    output_ << "  " << comparison << " = " << llvm_opcode << " "
            << get_llvm_type_name(lhs.type) << " " << lhs.text << ", "
            << rhs.text << "\n";
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
                                 const SemanticType *return_type) {
    const std::string return_type_name = get_llvm_type_name(return_type);
    if (return_type_name == "void") {
        output_ << "  call void " << callee << "(";
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
    output_ << "  " << result << " = call " << return_type_name << " "
            << callee << "(";
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
    if (declarations_.str().empty()) {
        return output_.str();
    }
    return declarations_.str() + "\n" + output_.str();
}

} // namespace sysycc
