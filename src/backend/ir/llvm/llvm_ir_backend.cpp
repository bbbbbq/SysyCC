#include "backend/ir/llvm/llvm_ir_backend.hpp"

#include <string>
#include <utility>
#include <vector>

#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc {

namespace {

std::string get_llvm_type_name(const SemanticType *type) {
    if (type == nullptr) {
        return "void";
    }

    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto *builtin_type = static_cast<const BuiltinSemanticType *>(type);
        if (builtin_type->get_name() == "int") {
            return "i32";
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
    }

    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return "ptr";
    }

    return "void";
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
}

void LlvmIrBackend::end_module() {}

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
        if (get_llvm_type_name(condition.type) == "double") {
            output_ << "  " << lowered_condition << " = fcmp one double "
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
    if (op == "+") {
        llvm_opcode = get_llvm_type_name(result_type) == "double" ? "fadd" : "add";
    } else if (op == "-") {
        llvm_opcode = get_llvm_type_name(result_type) == "double" ? "fsub" : "sub";
    } else if (op == "*") {
        llvm_opcode = get_llvm_type_name(result_type) == "double" ? "fmul" : "mul";
    } else if (op == "/") {
        llvm_opcode = get_llvm_type_name(result_type) == "double" ? "fdiv" : "sdiv";
    } else if (op == "%") {
        llvm_opcode = "srem";
    } else if (op == "<") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp olt"
                                                                : "icmp slt";
        is_comparison = true;
    } else if (op == "<=") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp ole"
                                                                : "icmp sle";
        is_comparison = true;
    } else if (op == ">") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp ogt"
                                                                : "icmp sgt";
        is_comparison = true;
    } else if (op == ">=") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp oge"
                                                                : "icmp sge";
        is_comparison = true;
    } else if (op == "==") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp oeq"
                                                                : "icmp eq";
        is_comparison = true;
    } else if (op == "!=") {
        llvm_opcode = get_llvm_type_name(lhs.type) == "double" ? "fcmp one"
                                                                : "icmp ne";
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
