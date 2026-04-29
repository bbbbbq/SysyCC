#include "backend/ir/shared/core/core_ir_builder.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/aggregate_layout.hpp"
#include "common/string_literal.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc {

namespace {

const TranslationUnit *get_translation_unit(const CompilerContext &context) {
    return dynamic_cast<const TranslationUnit *>(context.get_ast_root());
}

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

bool is_builtin_type_named(const SemanticType *type, const char *name) {
    const SemanticType *unqualified = strip_qualifiers(type);
    return unqualified != nullptr &&
           unqualified->get_kind() == SemanticTypeKind::Builtin &&
           static_cast<const BuiltinSemanticType *>(unqualified)->get_name() ==
               name;
}

bool is_integer_semantic_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const std::string &name =
        static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "char" || name == "signed char" || name == "unsigned char" ||
           name == "short" || name == "unsigned short" || name == "int" ||
           name == "unsigned int" || name == "long int" ||
           name == "unsigned long" || name == "long long int" ||
           name == "unsigned long long" || name == "ptrdiff_t" ||
           name == "size_t";
}

bool is_float_semantic_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const std::string &name =
        static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "_Float16" || name == "float" || name == "double" ||
           name == "long double";
}

bool is_scalar_semantic_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return false;
    }
    return type->get_kind() == SemanticTypeKind::Builtin ||
           type->get_kind() == SemanticTypeKind::Pointer ||
           type->get_kind() == SemanticTypeKind::Enum;
}

bool is_character_semantic_type(const SemanticType *type);

bool is_zero_float_literal_text(std::string value_text) {
    while (!value_text.empty()) {
        const char last = value_text.back();
        if (last == 'f' || last == 'F' || last == 'l' || last == 'L') {
            value_text.pop_back();
            continue;
        }
        break;
    }
    if (value_text.empty()) {
        return false;
    }
    try {
        return std::stold(value_text) == 0.0L;
    } catch (...) {
        return false;
    }
}

bool consumes_array_subinitializer_directly(const Expr *expr,
                                            const ArraySemanticType *array_type) {
    if (expr == nullptr || array_type == nullptr) {
        return false;
    }
    if (expr->get_kind() == AstKind::InitListExpr) {
        return true;
    }
    return expr->get_kind() == AstKind::StringLiteralExpr &&
           is_character_semantic_type(array_type->get_element_type());
}

bool expr_is_obviously_nonzero_constant(const Expr *expr) {
    if (expr == nullptr) {
        return false;
    }
    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return static_cast<const IntegerLiteralExpr *>(expr)->get_value_text() != "0";
    case AstKind::CharLiteralExpr:
        return static_cast<const CharLiteralExpr *>(expr)->get_value_text() != "'\\0'";
    default:
        return false;
    }
}

bool stmt_contains_break(const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }
    switch (stmt->get_kind()) {
    case AstKind::BreakStmt:
        return true;
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            if (stmt_contains_break(child.get())) {
                return true;
            }
        }
        return false;
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        return stmt_contains_break(if_stmt->get_then_branch()) ||
               stmt_contains_break(if_stmt->get_else_branch());
    }
    case AstKind::WhileStmt:
    case AstKind::DoWhileStmt:
    case AstKind::ForStmt:
    case AstKind::SwitchStmt:
    case AstKind::DeclStmt:
    case AstKind::ExprStmt:
    case AstKind::ReturnStmt:
    case AstKind::ContinueStmt:
    case AstKind::GotoStmt:
    case AstKind::CaseStmt:
    case AstKind::DefaultStmt:
    case AstKind::UnknownStmt:
        return false;
    case AstKind::LabelStmt:
        return stmt_contains_break(static_cast<const LabelStmt *>(stmt)->get_body());
    default:
        return false;
    }
}

bool stmt_contains_label(const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }
    if (stmt->get_kind() == AstKind::LabelStmt) {
        return true;
    }
    if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
        block_stmt != nullptr) {
        for (const auto &statement : block_stmt->get_statements()) {
            if (stmt_contains_label(statement.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto *if_stmt = dynamic_cast<const IfStmt *>(stmt);
        if_stmt != nullptr) {
        return stmt_contains_label(if_stmt->get_then_branch()) ||
               stmt_contains_label(if_stmt->get_else_branch());
    }
    if (const auto *switch_stmt = dynamic_cast<const SwitchStmt *>(stmt);
        switch_stmt != nullptr) {
        return stmt_contains_label(switch_stmt->get_body());
    }
    if (const auto *while_stmt = dynamic_cast<const WhileStmt *>(stmt);
        while_stmt != nullptr) {
        return stmt_contains_label(while_stmt->get_body());
    }
    if (const auto *do_while_stmt = dynamic_cast<const DoWhileStmt *>(stmt);
        do_while_stmt != nullptr) {
        return stmt_contains_label(do_while_stmt->get_body());
    }
    if (const auto *for_stmt = dynamic_cast<const ForStmt *>(stmt);
        for_stmt != nullptr) {
        return stmt_contains_label(for_stmt->get_body());
    }
    return false;
}

bool is_unsigned_integer_semantic_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return false;
    }
    return is_builtin_type_named(type, "unsigned char") ||
           is_builtin_type_named(type, "unsigned short") ||
           is_builtin_type_named(type, "unsigned int") ||
           is_builtin_type_named(type, "unsigned long") ||
           is_builtin_type_named(type, "unsigned long long") ||
           is_builtin_type_named(type, "size_t");
}

bool is_character_semantic_type(const SemanticType *type) {
    return is_builtin_type_named(type, "char") ||
           is_builtin_type_named(type, "signed char") ||
           is_builtin_type_named(type, "unsigned char");
}

const SemanticType *get_static_builtin_semantic_type(std::string_view name) {
    static const BuiltinSemanticType int_type("int");
    static const BuiltinSemanticType double_type("double");
    static const BuiltinSemanticType unsigned_char_type("unsigned char");
    if (name == "int") {
        return &int_type;
    }
    if (name == "double") {
        return &double_type;
    }
    if (name == "unsigned char") {
        return &unsigned_char_type;
    }
    return nullptr;
}

std::uint64_t get_low_bit_mask(std::size_t bit_width) {
    if (bit_width == 0) {
        return 0;
    }
    if (bit_width >= 64) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << bit_width) - 1;
}

const SemanticType *get_union_carrier_semantic_type(const UnionSemanticType *type) {
    if (type == nullptr || type->get_fields().empty()) {
        return nullptr;
    }
    const SemanticType *best_type = strip_qualifiers(type->get_fields().front().get_type());
    std::size_t best_alignment = detail::get_type_alignment(best_type);
    std::size_t best_size = detail::get_type_size(best_type);
    for (const auto &field : type->get_fields()) {
        const SemanticType *field_type = strip_qualifiers(field.get_type());
        const std::size_t field_alignment = detail::get_type_alignment(field_type);
        const std::size_t field_size = detail::get_type_size(field_type);
        if (field_alignment > best_alignment ||
            (field_alignment == best_alignment && field_size > best_size)) {
            best_type = field_type;
            best_alignment = field_alignment;
            best_size = field_size;
        }
    }
    return best_type;
}

std::size_t get_core_ir_default_alignment(const CoreIrType *type) {
    if (type == nullptr) {
        return 1;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Integer: {
        const auto bit_width =
            static_cast<const CoreIrIntegerType *>(type)->get_bit_width();
        if (bit_width <= 8) {
            return 1;
        }
        if (bit_width <= 16) {
            return 2;
        }
        if (bit_width <= 32) {
            return 4;
        }
        return 8;
    }
    case CoreIrTypeKind::Float:
        switch (static_cast<const CoreIrFloatType *>(type)->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return 2;
        case CoreIrFloatKind::Float32:
            return 4;
        case CoreIrFloatKind::Float64:
        case CoreIrFloatKind::Float128:
            return 8;
        }
        return 8;
    case CoreIrTypeKind::Pointer:
        return 8;
    case CoreIrTypeKind::Array:
        return get_core_ir_default_alignment(
            static_cast<const CoreIrArrayType *>(type)->get_element_type());
    case CoreIrTypeKind::Struct: {
        const auto *struct_type = static_cast<const CoreIrStructType *>(type);
        if (struct_type->get_is_packed()) {
            return 1;
        }
        std::size_t alignment = 1;
        const auto &element_types = struct_type->get_element_types();
        for (const CoreIrType *element_type : element_types) {
            alignment =
                std::max(alignment, get_core_ir_default_alignment(element_type));
        }
        return alignment;
    }
    case CoreIrTypeKind::Function:
    case CoreIrTypeKind::Void:
        return 8;
    }
    return 8;
}

std::size_t get_core_ir_type_size(const CoreIrType *type) {
    if (type == nullptr) {
        return 0;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Void:
        return 0;
    case CoreIrTypeKind::Integer:
        return (static_cast<const CoreIrIntegerType *>(type)->get_bit_width() + 7) / 8;
    case CoreIrTypeKind::Float:
        switch (static_cast<const CoreIrFloatType *>(type)->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return 2;
        case CoreIrFloatKind::Float32:
            return 4;
        case CoreIrFloatKind::Float64:
            return 8;
        case CoreIrFloatKind::Float128:
            return 16;
        }
        return 0;
    case CoreIrTypeKind::Pointer:
        return 8;
    case CoreIrTypeKind::Array: {
        const auto *array_type = static_cast<const CoreIrArrayType *>(type);
        return get_core_ir_type_size(array_type->get_element_type()) *
               array_type->get_element_count();
    }
    case CoreIrTypeKind::Struct: {
        const auto *struct_type = static_cast<const CoreIrStructType *>(type);
        std::size_t total_size = 0;
        std::size_t max_alignment = 1;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            const std::size_t element_alignment =
                get_core_ir_default_alignment(element_type);
            max_alignment = std::max(max_alignment, element_alignment);
            if (!struct_type->get_is_packed()) {
                const std::size_t remainder = total_size % element_alignment;
                if (remainder != 0) {
                    total_size += element_alignment - remainder;
                }
            }
            total_size += get_core_ir_type_size(element_type);
        }
        if (!struct_type->get_is_packed() && max_alignment > 1) {
            const std::size_t remainder = total_size % max_alignment;
            if (remainder != 0) {
                total_size += max_alignment - remainder;
            }
        }
        return total_size;
    }
    case CoreIrTypeKind::Function:
        return 0;
    }
    return 0;
}

class CoreIrBuildSession {
  private:
    struct ValueBinding {
        CoreIrValue *value = nullptr;
        CoreIrStackSlot *stack_slot = nullptr;
        CoreIrGlobal *global = nullptr;
    };

    struct LoopFrame {
        CoreIrBasicBlock *break_block = nullptr;
        CoreIrBasicBlock *continue_block = nullptr;
    };

    struct BitFieldLValue {
        CoreIrValue *storage_address = nullptr;
        detail::AggregateFieldLayout layout;
        const SemanticType *field_semantic_type = nullptr;
    };

    CompilerContext &compiler_context_;
    SemanticModel &semantic_model_;
    detail::ConstantEvaluator constant_evaluator_;
    std::unique_ptr<CoreIrContext> core_ir_context_;
    CoreIrModule *module_ = nullptr;
    const CoreIrType *void_type_ = nullptr;
    CoreIrFunction *current_function_ = nullptr;
    CoreIrBasicBlock *current_block_ = nullptr;
    const CoreIrType *current_function_return_type_ = nullptr;
    const SemanticType *current_function_return_semantic_type_ = nullptr;
    std::unordered_map<const SemanticType *, const CoreIrType *> type_cache_;
    std::unordered_map<const SemanticSymbol *, CoreIrFunction *> function_bindings_;
    std::unordered_map<const SemanticSymbol *, ValueBinding> global_bindings_;
    std::unordered_map<const SemanticSymbol *, ValueBinding> local_bindings_;
    std::unordered_map<std::string, const SemanticType *> named_struct_types_;
    std::unordered_map<std::string, const SemanticType *> named_union_types_;
    std::unordered_map<std::string, CoreIrGlobal *> string_literal_globals_;
    std::unordered_map<std::string, CoreIrBasicBlock *> label_blocks_;
    std::unordered_map<std::string, std::size_t> local_stack_slot_name_counts_;
    std::unordered_map<std::string, std::size_t> local_static_global_name_counts_;
    std::vector<CoreIrBasicBlock *> address_taken_label_blocks_;
    std::vector<LoopFrame> loop_frames_;
    std::vector<CoreIrBasicBlock *> break_blocks_;
    std::size_t next_temp_id_ = 0;
    std::size_t next_if_id_ = 0;
    std::size_t next_loop_id_ = 0;
    std::size_t next_switch_id_ = 0;
    std::size_t next_conditional_id_ = 0;
    std::size_t next_string_literal_id_ = 0;

    std::size_t get_default_alignment(const CoreIrType *type) const {
        if (type == nullptr) {
            return 1;
        }
        switch (type->get_kind()) {
        case CoreIrTypeKind::Integer: {
            const auto bit_width =
                static_cast<const CoreIrIntegerType *>(type)->get_bit_width();
            if (bit_width <= 8) {
                return 1;
            }
            if (bit_width <= 16) {
                return 2;
            }
            if (bit_width <= 32) {
                return 4;
            }
            return 8;
        }
        case CoreIrTypeKind::Float: {
            const auto float_kind =
                static_cast<const CoreIrFloatType *>(type)->get_float_kind();
            switch (float_kind) {
            case CoreIrFloatKind::Float16:
                return 2;
            case CoreIrFloatKind::Float32:
                return 4;
            case CoreIrFloatKind::Float64:
                return 8;
            case CoreIrFloatKind::Float128:
                return 16;
            }
            return 8;
        }
        case CoreIrTypeKind::Pointer:
            return 8;
        case CoreIrTypeKind::Array:
            return get_default_alignment(
                static_cast<const CoreIrArrayType *>(type)->get_element_type());
        case CoreIrTypeKind::Struct: {
            const auto *struct_type = static_cast<const CoreIrStructType *>(type);
            if (struct_type->get_is_packed()) {
                return 1;
            }
            std::size_t alignment = 1;
            const auto &element_types = struct_type->get_element_types();
            for (const CoreIrType *element_type : element_types) {
                alignment = std::max(alignment, get_default_alignment(element_type));
            }
            return alignment;
        }
        case CoreIrTypeKind::Function:
        case CoreIrTypeKind::Void:
            return 8;
        }
        return 8;
    }

    void add_error(std::string message, SourceSpan source_span = {}) {
        compiler_context_.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler, std::move(message), source_span);
    }

    std::string next_temp_name() {
        return "t" + std::to_string(next_temp_id_++);
    }

    std::string next_if_suffix() { return std::to_string(next_if_id_++); }

    std::string next_loop_suffix() { return std::to_string(next_loop_id_++); }

    std::string next_switch_suffix() {
        return std::to_string(next_switch_id_++);
    }

    std::string next_conditional_suffix() {
        return std::to_string(next_conditional_id_++);
    }

    std::string next_string_literal_name() {
        return ".str" + std::to_string(next_string_literal_id_++);
    }

    std::string next_stack_slot_name(const std::string &base_name) {
        auto [it, inserted] =
            local_stack_slot_name_counts_.emplace(base_name, 0);
        const std::size_t suffix = it->second++;
        if (suffix == 0) {
            return base_name;
        }
        return base_name + std::to_string(suffix);
    }

    std::string next_local_static_global_name(const std::string &symbol_name) {
        const std::string function_name =
            current_function_ == nullptr ? "file" : current_function_->get_name();
        const std::string base_name =
            function_name + "." + symbol_name + ".static";
        auto [it, inserted] =
            local_static_global_name_counts_.emplace(base_name, 0);
        for (;;) {
            const std::size_t suffix = it->second++;
            std::string candidate = base_name;
            if (suffix != 0) {
                candidate += std::to_string(suffix);
            }
            if (module_ == nullptr || module_->find_global(candidate) == nullptr) {
                return candidate;
            }
        }
    }

    CoreIrBasicBlock *get_or_create_label_block(const std::string &label_name) {
        const auto it = label_blocks_.find(label_name);
        if (it != label_blocks_.end()) {
            return it->second;
        }
        CoreIrBasicBlock *label_block =
            current_function_->create_basic_block<CoreIrBasicBlock>("goto." +
                                                                    label_name);
        label_blocks_.emplace(label_name, label_block);
        return label_block;
    }

    CoreIrBasicBlock *record_address_taken_label(const std::string &label_name) {
        CoreIrBasicBlock *label_block = get_or_create_label_block(label_name);
        if (label_block == nullptr) {
            return nullptr;
        }
        if (std::find(address_taken_label_blocks_.begin(),
                      address_taken_label_blocks_.end(),
                      label_block) == address_taken_label_blocks_.end()) {
            address_taken_label_blocks_.push_back(label_block);
        }
        return label_block;
    }

    const SemanticType *get_node_type(const AstNode *node) const {
        return semantic_model_.get_node_type(node);
    }

    const SemanticSymbol *get_symbol_binding(const AstNode *node) const {
        return semantic_model_.get_symbol_binding(node);
    }

    ValueBinding *find_value_binding(const SemanticSymbol *symbol) {
        if (symbol == nullptr) {
            return nullptr;
        }
        const auto local_it = local_bindings_.find(symbol);
        if (local_it != local_bindings_.end()) {
            return &local_it->second;
        }
        const auto global_it = global_bindings_.find(symbol);
        if (global_it != global_bindings_.end()) {
            return &global_it->second;
        }
        return nullptr;
    }

    bool should_materialize_global_const(const SemanticSymbol *symbol) const {
        return symbol != nullptr;
    }

    CoreIrFunction *find_function_binding(const SemanticSymbol *symbol) const {
        if (symbol == nullptr) {
            return nullptr;
        }
        const auto function_it = function_bindings_.find(symbol);
        if (function_it == function_bindings_.end()) {
            return nullptr;
        }
        return function_it->second;
    }

    CoreIrFunction *ensure_function_binding(const SemanticSymbol *symbol,
                                           SourceSpan source_span) {
        if (symbol == nullptr) {
            return nullptr;
        }
        if (CoreIrFunction *function = find_function_binding(symbol);
            function != nullptr) {
            return function;
        }
        if (symbol->get_type() == nullptr ||
            strip_qualifiers(symbol->get_type()) == nullptr ||
            strip_qualifiers(symbol->get_type())->get_kind() !=
                SemanticTypeKind::Function) {
            add_error("core ir generation expected a resolved function symbol",
                      source_span);
            return nullptr;
        }

        const CoreIrType *function_type_base = get_or_create_type(symbol->get_type());
        const auto *function_type =
            dynamic_cast<const CoreIrFunctionType *>(function_type_base);
        if (function_type == nullptr) {
            add_error("core ir generation could not lower function symbol type",
                      source_span);
            return nullptr;
        }

        std::string function_name = symbol->get_name();
        bool is_internal_linkage = false;
        if (const auto *function_decl =
                dynamic_cast<const FunctionDecl *>(symbol->get_decl_node());
            function_decl != nullptr) {
            is_internal_linkage = function_decl->get_is_static();
            if (!function_decl->get_asm_label().empty()) {
                function_name = function_decl->get_asm_label();
            }
        }

        if (CoreIrFunction *existing_function =
                module_->find_function(function_name);
            existing_function != nullptr) {
            function_bindings_[symbol] = existing_function;
            return existing_function;
        }

        auto *function = module_->create_function<CoreIrFunction>(
            function_name, function_type, is_internal_linkage);
        function_bindings_[symbol] = function;
        return function;
    }

    std::optional<long long> get_integer_constant_value(const AstNode *node) const {
        return constant_evaluator_.get_integer_constant_value(node, semantic_model_);
    }

    std::optional<long double>
    get_scalar_numeric_constant_value(const Expr *expr) const {
        return constant_evaluator_.get_scalar_numeric_constant_value(
            expr, semantic_model_);
    }

    const FunctionSemanticType *
    get_function_semantic_type(const SemanticType *type) const {
        const SemanticType *unqualified = strip_qualifiers(type);
        if (unqualified == nullptr) {
            return nullptr;
        }
        if (unqualified->get_kind() == SemanticTypeKind::Function) {
            return static_cast<const FunctionSemanticType *>(unqualified);
        }
        if (unqualified->get_kind() != SemanticTypeKind::Pointer) {
            return nullptr;
        }
        const auto *pointer_type =
            static_cast<const PointerSemanticType *>(unqualified);
        const SemanticType *pointee_type =
            strip_qualifiers(pointer_type->get_pointee_type());
        if (pointee_type == nullptr ||
            pointee_type->get_kind() != SemanticTypeKind::Function) {
            return nullptr;
        }
        return static_cast<const FunctionSemanticType *>(pointee_type);
    }

    const CoreIrType *get_or_create_builtin_type(const std::string &name,
                                                 const SemanticType *cache_key) {
        if (name == "void") {
            type_cache_[cache_key] = void_type_;
            return void_type_;
        }
        if (name == "char" || name == "signed char") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(8, true);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "unsigned char") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(8, false);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "short") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(16, true);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "unsigned short") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(16, false);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "int") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(32, true);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "unsigned int") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(32, false);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "long int" || name == "long long int" ||
            name == "ptrdiff_t") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(64, true);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "unsigned long" || name == "unsigned long long" ||
            name == "size_t") {
            const auto *type =
                core_ir_context_->create_type<CoreIrIntegerType>(64, false);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "_Float16") {
            const auto *type =
                core_ir_context_->create_type<CoreIrFloatType>(
                    CoreIrFloatKind::Float16);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "float") {
            const auto *type =
                core_ir_context_->create_type<CoreIrFloatType>(
                    CoreIrFloatKind::Float32);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "double") {
            const auto *type =
                core_ir_context_->create_type<CoreIrFloatType>(
                    CoreIrFloatKind::Float64);
            type_cache_[cache_key] = type;
            return type;
        }
        if (name == "long double") {
            const auto *type =
                core_ir_context_->create_type<CoreIrFloatType>(
                    CoreIrFloatKind::Float128);
            type_cache_[cache_key] = type;
            return type;
        }
        return nullptr;
    }

    const CoreIrType *get_or_create_type(const SemanticType *type) {
        if (type == nullptr) {
            return nullptr;
        }

        const auto cache_it = type_cache_.find(type);
        if (cache_it != type_cache_.end()) {
            return cache_it->second;
        }

        const SemanticType *unqualified = strip_qualifiers(type);
        if (unqualified != nullptr && unqualified != type) {
            const CoreIrType *core_type = get_or_create_type(unqualified);
            if (core_type != nullptr) {
                type_cache_[type] = core_type;
            }
            return core_type;
        }
        unqualified = get_canonical_aggregate_type(unqualified);

        switch (unqualified->get_kind()) {
        case SemanticTypeKind::Builtin:
            return get_or_create_builtin_type(
                static_cast<const BuiltinSemanticType *>(unqualified)->get_name(),
                type);
        case SemanticTypeKind::Pointer: {
            const auto *pointer_type =
                static_cast<const PointerSemanticType *>(unqualified);
            const CoreIrType *pointee_type =
                get_or_create_type(pointer_type->get_pointee_type());
            if (pointee_type == nullptr) {
                return nullptr;
            }
            const auto *core_type =
                core_ir_context_->create_type<CoreIrPointerType>(pointee_type);
            type_cache_[type] = core_type;
            return core_type;
        }
        case SemanticTypeKind::Array: {
            const auto *array_type =
                static_cast<const ArraySemanticType *>(unqualified);
            const CoreIrType *element_type =
                get_or_create_type(array_type->get_element_type());
            if (element_type == nullptr) {
                return nullptr;
            }
            const auto &dimensions = array_type->get_dimensions();
            if (dimensions.empty()) {
                add_error("core ir generation does not support unsized arrays");
                return nullptr;
            }
            const CoreIrType *current_type = element_type;
            for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
                if (*it < 0) {
                    add_error("core ir generation does not support unsized arrays");
                    return nullptr;
                }
                current_type = core_ir_context_->create_type<CoreIrArrayType>(
                    current_type, static_cast<std::size_t>(*it));
            }
            type_cache_[type] = current_type;
            return current_type;
        }
        case SemanticTypeKind::Function: {
            const auto *function_type =
                static_cast<const FunctionSemanticType *>(unqualified);
            const CoreIrType *return_type =
                get_or_create_type(function_type->get_return_type());
            if (return_type == nullptr) {
                return nullptr;
            }
            std::vector<const CoreIrType *> parameter_types;
            parameter_types.reserve(function_type->get_parameter_types().size());
            for (const SemanticType *parameter_type :
                 function_type->get_parameter_types()) {
                const CoreIrType *core_parameter_type =
                    get_or_create_type(
                        adjust_function_parameter_semantic_type(parameter_type));
                if (core_parameter_type == nullptr) {
                    return nullptr;
                }
                parameter_types.push_back(core_parameter_type);
            }
            const auto *core_type =
                core_ir_context_->create_type<CoreIrFunctionType>(
                    return_type, std::move(parameter_types),
                    function_type->get_is_variadic());
            type_cache_[type] = core_type;
            return core_type;
        }
        case SemanticTypeKind::Enum: {
            const auto *core_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32, true);
            type_cache_[type] = core_type;
            return core_type;
        }
        case SemanticTypeKind::Qualified:
            break;
        case SemanticTypeKind::Struct: {
            auto *struct_type = core_ir_context_->create_type<CoreIrStructType>();
            type_cache_[type] = struct_type;

            const detail::AggregateLayoutInfo layout =
                detail::compute_aggregate_layout(unqualified);
            std::vector<const CoreIrType *> element_types;
            element_types.reserve(layout.elements.size());
            bool requires_packed_storage = false;
            const auto *byte_type =
                core_ir_context_->create_type<CoreIrIntegerType>(8);
            for (const auto &element : layout.elements) {
                if (element.kind == detail::AggregateLayoutElementKind::Padding) {
                    element_types.push_back(
                        core_ir_context_->create_type<CoreIrArrayType>(
                            byte_type, element.padding_size));
                    continue;
                }
                if (element.kind ==
                    detail::AggregateLayoutElementKind::BitFieldStorage) {
                    requires_packed_storage = true;
                    element_types.push_back(
                        core_ir_context_->create_type<CoreIrIntegerType>(
                            element.bit_width, element.is_signed));
                    continue;
                }
                const CoreIrType *element_type = get_or_create_type(element.type);
                if (element_type == nullptr) {
                    return nullptr;
                }
                element_types.push_back(element_type);
            }
            struct_type->set_element_types(std::move(element_types));
            struct_type->set_is_packed(requires_packed_storage);
            return struct_type;
        }
        case SemanticTypeKind::Union: {
            auto *union_type = core_ir_context_->create_type<CoreIrStructType>();
            type_cache_[type] = union_type;

            const auto *union_semantic_type =
                static_cast<const UnionSemanticType *>(unqualified);
            const detail::AggregateLayoutInfo layout =
                detail::compute_aggregate_layout(unqualified);
            const SemanticType *carrier_semantic_type =
                get_union_carrier_semantic_type(union_semantic_type);
            if (carrier_semantic_type == nullptr || layout.size == 0) {
                add_error("core ir generation could not resolve union carrier type");
                return nullptr;
            }

            const CoreIrType *carrier_type =
                get_or_create_type(carrier_semantic_type);
            if (carrier_type == nullptr) {
                return nullptr;
            }

            std::vector<const CoreIrType *> element_types;
            element_types.push_back(carrier_type);
            const std::size_t carrier_size = detail::get_type_size(carrier_semantic_type);
            if (layout.size > carrier_size) {
                const auto *byte_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(8);
                element_types.push_back(
                    core_ir_context_->create_type<CoreIrArrayType>(
                        byte_type, layout.size - carrier_size));
            }
            union_type->set_element_types(std::move(element_types));
            return union_type;
        }
        }

        add_error("core ir generation encountered an unsupported semantic type");
        return nullptr;
    }

    const Expr *get_statement_expr_result_expr(const Stmt *stmt) const {
        if (stmt == nullptr) {
            return nullptr;
        }
        if (const auto *expr_stmt = dynamic_cast<const ExprStmt *>(stmt);
            expr_stmt != nullptr) {
            return expr_stmt->get_expression();
        }
        if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
            block_stmt != nullptr) {
            for (auto it = block_stmt->get_statements().rbegin();
                 it != block_stmt->get_statements().rend(); ++it) {
                if (*it == nullptr) {
                    continue;
                }
                return get_statement_expr_result_expr(it->get());
            }
        }
        return nullptr;
    }

    bool emit_statement_expr_prefix(const Stmt *stmt, const Expr *result_expr) {
        if (stmt == nullptr) {
            return true;
        }
        if (const auto *expr_stmt = dynamic_cast<const ExprStmt *>(stmt);
            expr_stmt != nullptr && expr_stmt->get_expression() == result_expr) {
            return true;
        }
        if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
            block_stmt != nullptr) {
            const auto &statements = block_stmt->get_statements();
            for (std::size_t index = 0; index < statements.size(); ++index) {
                const Stmt *statement = statements[index].get();
                const bool is_last = index + 1 == statements.size();
                if (is_last) {
                    if (!emit_statement_expr_prefix(statement, result_expr)) {
                        return false;
                    }
                    continue;
                }
                if (!emit_stmt(statement)) {
                    return false;
                }
            }
            return true;
        }
        return emit_stmt(stmt);
    }

    CoreIrValue *build_statement_expr(const StatementExpr &expr) {
        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve statement expression "
                      "result type",
                      expr.get_source_span());
            return nullptr;
        }
        if (result_type == void_type_) {
            if (!emit_stmt(expr.get_body())) {
                return nullptr;
            }
            return create_i32_constant(0, expr.get_source_span());
        }

        const Expr *result_expr = get_statement_expr_result_expr(expr.get_body());
        if (result_expr == nullptr) {
            add_error("core ir generation expected a value-producing final "
                      "expression in GNU statement expression",
                      expr.get_source_span());
            return nullptr;
        }
        if (!emit_statement_expr_prefix(expr.get_body(), result_expr)) {
            return nullptr;
        }
        return build_expr(result_expr);
    }

    CoreIrValue *build_expr(const Expr *expr) {
        if (expr == nullptr) {
            return nullptr;
        }
        if (current_block_ == nullptr) {
            add_error("core ir generation reached an expression in a terminated "
                      "control-flow path",
                      expr->get_source_span());
            return nullptr;
        }

        if (const auto *integer_literal =
                dynamic_cast<const IntegerLiteralExpr *>(expr);
            integer_literal != nullptr) {
            return build_integer_literal(*integer_literal);
        }
        if (const auto *char_literal = dynamic_cast<const CharLiteralExpr *>(expr);
            char_literal != nullptr) {
            return build_char_literal(*char_literal);
        }
        if (const auto *float_literal = dynamic_cast<const FloatLiteralExpr *>(expr);
            float_literal != nullptr) {
            return build_float_literal(*float_literal);
        }
        if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(expr);
            identifier != nullptr) {
            return build_identifier(*identifier);
        }
        if (const auto *sizeof_type_expr = dynamic_cast<const SizeofTypeExpr *>(expr);
            sizeof_type_expr != nullptr) {
            return build_integer_constant_expr(*sizeof_type_expr);
        }
        if (const auto *string_literal = dynamic_cast<const StringLiteralExpr *>(expr);
            string_literal != nullptr) {
            return build_string_literal_expr(*string_literal);
        }
        if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
            cast_expr != nullptr) {
            return build_cast_expr(*cast_expr);
        }
        if (const auto *conditional_expr = dynamic_cast<const ConditionalExpr *>(expr);
            conditional_expr != nullptr) {
            return build_conditional_expr(*conditional_expr);
        }
        if (const auto *index_expr = dynamic_cast<const IndexExpr *>(expr);
            index_expr != nullptr) {
            CoreIrValue *address = build_index_address(*index_expr);
            if (address == nullptr) {
                return nullptr;
            }
            return build_value_from_lvalue(*index_expr, address);
        }
        if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
            member_expr != nullptr) {
            if (const std::optional<BitFieldLValue> bit_field_lvalue =
                    get_bit_field_lvalue(member_expr);
                bit_field_lvalue.has_value()) {
                return build_bit_field_value(*bit_field_lvalue,
                                             member_expr->get_source_span());
            }
            CoreIrValue *address = build_member_address(*member_expr);
            if (address == nullptr) {
                return nullptr;
            }
            return build_value_from_lvalue(*member_expr, address);
        }
        if (const auto *unary_expr = dynamic_cast<const UnaryExpr *>(expr);
            unary_expr != nullptr) {
            return build_unary_expr(*unary_expr);
        }
        if (const auto *prefix_expr = dynamic_cast<const PrefixExpr *>(expr);
            prefix_expr != nullptr) {
            return build_prefix_expr(*prefix_expr);
        }
        if (const auto *postfix_expr = dynamic_cast<const PostfixExpr *>(expr);
            postfix_expr != nullptr) {
            return build_postfix_expr(*postfix_expr);
        }
        if (const auto *assign_expr = dynamic_cast<const AssignExpr *>(expr);
            assign_expr != nullptr) {
            return build_assign_expr(*assign_expr);
        }
        if (const auto *call_expr = dynamic_cast<const CallExpr *>(expr);
            call_expr != nullptr) {
            return build_call_expr(*call_expr);
        }
        if (const auto *va_arg_expr = dynamic_cast<const BuiltinVaArgExpr *>(expr);
            va_arg_expr != nullptr) {
            return build_builtin_va_arg_expr(*va_arg_expr);
        }
        if (const auto *binary_expr = dynamic_cast<const BinaryExpr *>(expr);
            binary_expr != nullptr) {
            return build_binary_expr(*binary_expr);
        }
        if (const auto *statement_expr = dynamic_cast<const StatementExpr *>(expr);
            statement_expr != nullptr) {
            return build_statement_expr(*statement_expr);
        }

        add_error("core ir generation does not support this expression yet",
                  expr->get_source_span());
        return nullptr;
    }

    CoreIrValue *build_integer_literal(const IntegerLiteralExpr &expr) {
        return build_integer_constant_expr(expr);
    }

    CoreIrValue *build_char_literal(const CharLiteralExpr &expr) {
        return build_integer_constant_expr(expr);
    }

    CoreIrValue *build_float_literal(const FloatLiteralExpr &expr) {
        const SemanticType *semantic_type = get_node_type(&expr);
        const CoreIrType *core_type = get_or_create_type(semantic_type);
        if (core_type == nullptr) {
            add_error("core ir generation could not resolve floating literal type",
                      expr.get_source_span());
            return nullptr;
        }

        if (is_builtin_type_named(semantic_type, "long double")) {
            const SemanticType *double_semantic_type =
                get_static_builtin_semantic_type("double");
            const CoreIrType *double_type =
                get_or_create_type(double_semantic_type);
            if (double_type == nullptr) {
                add_error("core ir generation could not resolve double literal "
                          "storage type",
                          expr.get_source_span());
                return nullptr;
            }
            auto *double_constant =
                core_ir_context_->create_constant<CoreIrConstantFloat>(
                    double_type, expr.get_value_text());
            double_constant->set_source_span(expr.get_source_span());
            return build_converted_value(double_constant, double_semantic_type,
                                         semantic_type, expr.get_source_span());
        }

        auto *constant = core_ir_context_->create_constant<CoreIrConstantFloat>(
            core_type, expr.get_value_text());
        constant->set_source_span(expr.get_source_span());
        return constant;
    }

    CoreIrValue *build_integer_constant_expr(const Expr &expr) {
        const SemanticType *semantic_type = get_node_type(&expr);
        const CoreIrType *core_type = get_or_create_type(semantic_type);
        if (core_type == nullptr) {
            add_error("core ir generation could not resolve integer constant "
                      "expression type",
                      expr.get_source_span());
            return nullptr;
        }
        const std::optional<long long> value = get_integer_constant_value(&expr);
        if (!value.has_value()) {
            add_error("core ir generation expected an integer constant expression "
                      "value",
                      expr.get_source_span());
            return nullptr;
        }
        auto *constant = core_ir_context_->create_constant<CoreIrConstantInt>(
            core_type, static_cast<std::uint64_t>(*value));
        constant->set_source_span(expr.get_source_span());
        return constant;
    }

    CoreIrValue *build_global_address(CoreIrGlobal &global, SourceSpan source_span) {
        const auto *pointer_type =
            core_ir_context_->create_type<CoreIrPointerType>(global.get_type());
        auto *address_instruction =
            current_block_->create_instruction<CoreIrAddressOfGlobalInst>(
                pointer_type, next_temp_name(), &global);
        address_instruction->set_source_span(source_span);
        return address_instruction;
    }

    CoreIrValue *build_function_address(CoreIrFunction &function,
                                        SourceSpan source_span) {
        const auto *pointer_type =
            core_ir_context_->create_type<CoreIrPointerType>(
                function.get_function_type());
        auto *address_instruction =
            current_block_->create_instruction<CoreIrAddressOfFunctionInst>(
                pointer_type, next_temp_name(), &function);
        address_instruction->set_source_span(source_span);
        return address_instruction;
    }

    CoreIrValue *build_stack_slot_address(CoreIrStackSlot &stack_slot,
                                          SourceSpan source_span) {
        const auto *pointer_type =
            core_ir_context_->create_type<CoreIrPointerType>(
                stack_slot.get_allocated_type());
        auto *address_instruction =
            current_block_->create_instruction<CoreIrAddressOfStackSlotInst>(
                pointer_type, next_temp_name(), &stack_slot);
        address_instruction->set_source_span(source_span);
        return address_instruction;
    }

    CoreIrValue *build_label_address_expr(const UnaryExpr &expr) {
        if (current_function_ == nullptr) {
            add_error("core ir generation encountered a label address outside a "
                      "function",
                      expr.get_source_span());
            return nullptr;
        }
        const auto *label_expr =
            dynamic_cast<const IdentifierExpr *>(expr.get_operand());
        if (label_expr == nullptr) {
            add_error("core ir generation expected a label name after '&&'",
                      expr.get_source_span());
            return nullptr;
        }
        CoreIrBasicBlock *label_block =
            record_address_taken_label(label_expr->get_name());
        if (label_block == nullptr) {
            add_error("core ir generation could not allocate a label block for '" +
                          label_expr->get_name() + "'",
                      expr.get_source_span());
            return nullptr;
        }
        const CoreIrType *pointer_type = get_or_create_type(get_node_type(&expr));
        if (pointer_type == nullptr ||
            pointer_type->get_kind() != CoreIrTypeKind::Pointer) {
            pointer_type =
                core_ir_context_->create_type<CoreIrPointerType>(void_type_);
        }
        auto *constant = core_ir_context_->create_constant<CoreIrConstantBlockAddress>(
            pointer_type, current_function_->get_name(), label_block->get_name());
        constant->set_source_span(expr.get_source_span());
        return constant;
    }

    CoreIrValue *create_i32_constant(long long value, SourceSpan source_span) {
        const auto *i32_type = core_ir_context_->create_type<CoreIrIntegerType>(32);
        auto *constant = core_ir_context_->create_constant<CoreIrConstantInt>(
            i32_type, static_cast<std::uint64_t>(value));
        constant->set_source_span(source_span);
        return constant;
    }

    CoreIrValue *build_integer_constant(const CoreIrType *type, std::uint64_t value,
                                        SourceSpan source_span) {
        auto *constant =
            core_ir_context_->create_constant<CoreIrConstantInt>(type, value);
        constant->set_source_span(source_span);
        return constant;
    }

    CoreIrValue *apply_default_argument_promotion(CoreIrValue *value,
                                                  const SemanticType *value_type,
                                                  SourceSpan source_span) {
        if (value == nullptr || value_type == nullptr) {
            return nullptr;
        }
        if (is_integer_semantic_type(value_type)) {
            const SemanticType *promoted_type =
                get_integer_promotion_type(value_type, source_span);
            if (promoted_type == nullptr) {
                return nullptr;
            }
            return build_converted_value(value, value_type, promoted_type,
                                         source_span);
        }
        if (is_builtin_type_named(value_type, "float")) {
            return build_converted_value(value, value_type,
                                         get_static_builtin_semantic_type("double"),
                                         source_span);
        }
        return value;
    }

    const SemanticType *get_integer_promotion_type(const SemanticType *type,
                                                   SourceSpan source_span) {
        detail::IntegerConversionService integer_conversion_service;
        const SemanticType *promoted_type =
            integer_conversion_service.get_integer_promotion_type(
                type, semantic_model_);
        if (promoted_type == nullptr) {
            add_error("core ir generation could not resolve integer promotion type",
                      source_span);
        }
        return promoted_type;
    }

    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs_type, const SemanticType *rhs_type,
        SourceSpan source_span) {
        detail::IntegerConversionService integer_conversion_service;
        const SemanticType *conversion_type =
            integer_conversion_service.get_usual_arithmetic_conversion_type(
                lhs_type, rhs_type, semantic_model_);
        if (conversion_type == nullptr) {
            add_error("core ir generation could not resolve usual arithmetic "
                      "conversion type",
                      source_span);
        }
        return conversion_type;
    }

    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs_type, std::optional<int> lhs_bit_field_width,
        const SemanticType *rhs_type, std::optional<int> rhs_bit_field_width,
        SourceSpan source_span) {
        detail::IntegerConversionService integer_conversion_service;
        const SemanticType *conversion_type =
            integer_conversion_service.get_usual_arithmetic_conversion_type(
                lhs_type, lhs_bit_field_width, rhs_type, rhs_bit_field_width,
                semantic_model_);
        if (conversion_type == nullptr) {
            add_error("core ir generation could not resolve usual arithmetic "
                      "conversion type",
                      source_span);
        }
        return conversion_type;
    }

    bool is_shift_operator(const std::string &operator_text) const {
        return operator_text == "<<" || operator_text == ">>";
    }

    bool is_pointer_semantic_type(const SemanticType *type) const {
        type = strip_qualifiers(type);
        return type != nullptr && type->get_kind() == SemanticTypeKind::Pointer;
    }

    const SemanticType *
    get_pointer_union_parameter_carrier_type(const SemanticType *type) const {
        const SemanticType *unqualified = strip_qualifiers(type);
        if (unqualified == nullptr ||
            unqualified->get_kind() != SemanticTypeKind::Union) {
            return nullptr;
        }
        const auto *union_type = static_cast<const UnionSemanticType *>(unqualified);
        if (union_type->get_fields().empty()) {
            return nullptr;
        }
        for (const auto &field : union_type->get_fields()) {
            if (!is_pointer_semantic_type(field.get_type())) {
                return nullptr;
            }
        }
        return union_type->get_fields().front().get_type();
    }

    const SemanticType *
    adjust_function_parameter_semantic_type(const SemanticType *type) {
        const SemanticType *unqualified = strip_qualifiers(type);
        if (unqualified == nullptr) {
            return type;
        }
        if (unqualified->get_kind() == SemanticTypeKind::Array) {
            const auto *array_type =
                static_cast<const ArraySemanticType *>(unqualified);
            return semantic_model_.own_type(std::make_unique<PointerSemanticType>(
                array_type->get_element_type()));
        }
        if (unqualified->get_kind() == SemanticTypeKind::Function) {
            return semantic_model_.own_type(
                std::make_unique<PointerSemanticType>(unqualified));
        }
        if (const SemanticType *carrier_type =
                get_pointer_union_parameter_carrier_type(unqualified);
            carrier_type != nullptr) {
            return carrier_type;
        }
        return type;
    }

    const SemanticType *get_pointer_arithmetic_semantic_type(
        const SemanticType *type) const {
        type = strip_qualifiers(type);
        if (type == nullptr) {
            return nullptr;
        }
        if (type->get_kind() == SemanticTypeKind::Pointer) {
            return type;
        }
        if (type->get_kind() == SemanticTypeKind::Array) {
            const auto *array_type = static_cast<const ArraySemanticType *>(type);
            return semantic_model_.own_type(
                std::make_unique<PointerSemanticType>(
                    array_type->get_element_type()));
        }
        return nullptr;
    }

    CoreIrValue *build_gep(CoreIrValue *base, const CoreIrType *result_pointee_type,
                           std::vector<CoreIrValue *> indices,
                           SourceSpan source_span,
                           const CoreIrType *source_pointee_type = nullptr) {
        if (base == nullptr || result_pointee_type == nullptr) {
            return nullptr;
        }
        const auto *pointer_type =
            core_ir_context_->create_type<CoreIrPointerType>(result_pointee_type);
        auto *instruction =
            current_block_->create_instruction<CoreIrGetElementPtrInst>(
                pointer_type, next_temp_name(), base, std::move(indices),
                source_pointee_type);
        instruction->set_source_span(source_span);
        return instruction;
    }

    CoreIrValue *retag_pointer_for_gep(CoreIrValue *base,
                                       const CoreIrType *pointee_type,
                                       SourceSpan source_span) {
        if (base == nullptr || pointee_type == nullptr) {
            return base;
        }
        const auto *base_pointer_type =
            dynamic_cast<const CoreIrPointerType *>(base->get_type());
        if (base_pointer_type == nullptr ||
            base_pointer_type->get_pointee_type() == pointee_type) {
            return base;
        }

        return build_gep(base, pointee_type,
                         {create_i32_constant(0, source_span)}, source_span,
                         pointee_type);
    }

    CoreIrValue *
    build_local_array_element_address(CoreIrValue *base,
                                      const CoreIrType *element_type,
                                      const std::vector<std::size_t> &element_path,
                                      SourceSpan source_span) {
        if (base == nullptr || element_type == nullptr || element_path.empty()) {
            return nullptr;
        }

        std::vector<CoreIrValue *> indices;
        indices.reserve(element_path.size() + 1);
        indices.push_back(create_i32_constant(0, source_span));
        for (std::size_t index : element_path) {
            indices.push_back(
                create_i32_constant(static_cast<long long>(index), source_span));
        }
        return build_gep(base, element_type, std::move(indices), source_span);
    }

    CoreIrValue *build_array_decay_from_address(CoreIrValue *address,
                                                const SemanticType *array_type,
                                                SourceSpan source_span) {
        const auto *unqualified_array_type = strip_qualifiers(array_type);
        if (unqualified_array_type == nullptr ||
            unqualified_array_type->get_kind() != SemanticTypeKind::Array) {
            return address;
        }

        const auto *array_semantic_type =
            static_cast<const ArraySemanticType *>(unqualified_array_type);
        const CoreIrType *element_type =
            get_or_create_type(array_semantic_type->get_element_type());
        if (element_type == nullptr) {
            add_error("core ir generation could not resolve array element type",
                      source_span);
            return nullptr;
        }
        return build_gep(address, element_type,
                         {create_i32_constant(0, source_span),
                          create_i32_constant(0, source_span)},
                         source_span);
    }

    CoreIrValue *build_value_from_lvalue(const Expr &expr, CoreIrValue *address) {
        if (address == nullptr) {
            return nullptr;
        }
        const SemanticType *expr_type = get_node_type(&expr);
        const SemanticType *unqualified_expr_type = strip_qualifiers(expr_type);
        if (unqualified_expr_type != nullptr &&
            unqualified_expr_type->get_kind() == SemanticTypeKind::Function) {
            return address;
        }
        if (unqualified_expr_type != nullptr &&
            unqualified_expr_type->get_kind() == SemanticTypeKind::Array) {
            return build_array_decay_from_address(address, unqualified_expr_type,
                                                  expr.get_source_span());
        }

        const CoreIrType *load_type = get_or_create_type(expr_type);
        if (load_type == nullptr) {
            add_error("core ir generation could not resolve lvalue result type",
                      expr.get_source_span());
            return nullptr;
        }
        auto *load = current_block_->create_instruction<CoreIrLoadInst>(
            load_type, next_temp_name(), address);
        load->set_source_span(expr.get_source_span());
        return load;
    }

    std::optional<int> get_bit_field_width_for_expr(const Expr *expr) const {
        if (expr == nullptr) {
            return std::nullopt;
        }
        if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
            member_expr != nullptr) {
            const SemanticType *owner_type = get_member_owner_type(
                get_node_type(member_expr->get_base()),
                member_expr->get_operator_text());
            if (owner_type == nullptr) {
                return std::nullopt;
            }
            const std::optional<std::size_t> field_index =
                find_aggregate_field_index(owner_type,
                                           member_expr->get_member_name());
            if (!field_index.has_value()) {
                return std::nullopt;
            }
            const std::optional<detail::AggregateFieldLayout> field_layout =
                detail::get_aggregate_field_layout(owner_type, *field_index);
            if (!field_layout.has_value() || !field_layout->is_bit_field) {
                return std::nullopt;
            }
            return static_cast<int>(field_layout->bit_width);
        }
        if (const auto *binary_expr = dynamic_cast<const BinaryExpr *>(expr);
            binary_expr != nullptr && binary_expr->get_operator_text() == ",") {
            return get_bit_field_width_for_expr(binary_expr->get_rhs());
        }
        if (const auto *assign_expr = dynamic_cast<const AssignExpr *>(expr);
            assign_expr != nullptr) {
            return get_bit_field_width_for_expr(assign_expr->get_target());
        }
        if (const auto *prefix_expr = dynamic_cast<const PrefixExpr *>(expr);
            prefix_expr != nullptr) {
            return get_bit_field_width_for_expr(prefix_expr->get_operand());
        }
        if (const auto *postfix_expr = dynamic_cast<const PostfixExpr *>(expr);
            postfix_expr != nullptr) {
            return get_bit_field_width_for_expr(postfix_expr->get_operand());
        }
        if (const auto *conditional_expr = dynamic_cast<const ConditionalExpr *>(expr);
            conditional_expr != nullptr) {
            const auto true_width =
                get_bit_field_width_for_expr(conditional_expr->get_true_expr());
            const auto false_width =
                get_bit_field_width_for_expr(conditional_expr->get_false_expr());
            if (true_width.has_value() && false_width.has_value() &&
                *true_width == *false_width) {
                return true_width;
            }
        }
        return std::nullopt;
    }

    bool is_null_pointer_constant_expr(const Expr *expr) const {
        if (expr == nullptr) {
            return false;
        }
        const std::optional<long long> constant_value =
            get_integer_constant_value(expr);
        if (constant_value.has_value() && *constant_value == 0) {
            return true;
        }
        const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
        return cast_expr != nullptr &&
               is_null_pointer_constant_expr(cast_expr->get_operand());
    }

    bool is_zero_initializer_expr(const Expr *expr) const {
        if (expr == nullptr) {
            return true;
        }

        if (is_null_pointer_constant_expr(expr)) {
            return true;
        }

        if (const std::optional<long long> constant_value =
                get_integer_constant_value(expr);
            constant_value.has_value()) {
            return *constant_value == 0;
        }

        if (const auto *float_literal = dynamic_cast<const FloatLiteralExpr *>(expr);
            float_literal != nullptr) {
            return is_zero_float_literal_text(float_literal->get_value_text());
        }

        if (const auto *init_list = dynamic_cast<const InitListExpr *>(expr);
            init_list != nullptr) {
            return init_list->get_elements().empty() ||
                   (init_list->get_elements().size() == 1 &&
                    is_zero_initializer_expr(
                        init_list->get_elements().front().get()));
        }

        return false;
    }

    std::string format_scalar_float_literal(long double value) const {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<long double>::max_digits10)
               << value;
        std::string text = stream.str();
        if (text.find_first_of(".eE") == std::string::npos) {
            text += ".0";
        }
        return text;
    }

    const CoreIrConstant *build_float_constant_from_bytes(
        const CoreIrFloatType *float_type, const std::vector<std::uint8_t> &bytes,
        SourceSpan source_span) {
        if (float_type == nullptr) {
            return nullptr;
        }
        const std::size_t byte_count = get_core_ir_type_size(float_type);
        if (bytes.size() < byte_count) {
            add_error("core ir generation encountered truncated bytes while "
                      "materializing a floating constant",
                      source_span);
            return nullptr;
        }

        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            if (std::all_of(bytes.begin(), bytes.begin() + byte_count,
                            [](std::uint8_t byte) { return byte == 0; })) {
                return core_ir_context_->create_constant<CoreIrConstantFloat>(
                    float_type, "0.0");
            }
            add_error("core ir generation does not yet support non-zero f16 byte "
                      "materialization",
                      source_span);
            return nullptr;
        case CoreIrFloatKind::Float32: {
            float value = 0.0f;
            std::memcpy(&value, bytes.data(), sizeof(value));
            return core_ir_context_->create_constant<CoreIrConstantFloat>(
                float_type, format_scalar_float_literal(value));
        }
        case CoreIrFloatKind::Float64: {
            double value = 0.0;
            std::memcpy(&value, bytes.data(), sizeof(value));
            return core_ir_context_->create_constant<CoreIrConstantFloat>(
                float_type, format_scalar_float_literal(value));
        }
        case CoreIrFloatKind::Float128: {
            long double value = 0.0L;
            const std::size_t copy_size = std::min(sizeof(value), byte_count);
            std::memcpy(&value, bytes.data(), copy_size);
            return core_ir_context_->create_constant<CoreIrConstantFloat>(
                float_type, format_scalar_float_literal(value));
        }
        }
        return nullptr;
    }

    const SemanticType *get_member_owner_type(const SemanticType *base_type,
                                              const std::string &operator_text) const {
        const SemanticType *unqualified_base_type = strip_qualifiers(base_type);
        if (unqualified_base_type == nullptr) {
            return nullptr;
        }
        if (operator_text == "->") {
            if (unqualified_base_type->get_kind() == SemanticTypeKind::Pointer) {
                const auto *pointer_type =
                    static_cast<const PointerSemanticType *>(unqualified_base_type);
                return get_canonical_aggregate_type(
                    strip_qualifiers(pointer_type->get_pointee_type()));
            }
            if (unqualified_base_type->get_kind() == SemanticTypeKind::Array) {
                const auto *array_type =
                    static_cast<const ArraySemanticType *>(unqualified_base_type);
                const SemanticType *element_type =
                    strip_qualifiers(array_type->get_element_type());
                if (element_type != nullptr &&
                    (element_type->get_kind() == SemanticTypeKind::Struct ||
                     element_type->get_kind() == SemanticTypeKind::Union)) {
                    return get_canonical_aggregate_type(element_type);
                }
            }
            return nullptr;
        }
        if (operator_text == ".") {
            return get_canonical_aggregate_type(unqualified_base_type);
        }
        return nullptr;
    }

    const SemanticType *
    get_canonical_aggregate_type(const SemanticType *type) const {
        type = strip_qualifiers(type);
        if (type == nullptr) {
            return nullptr;
        }
        if (type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_type = static_cast<const StructSemanticType *>(type);
            if (struct_type->get_fields().empty()) {
                const auto it = named_struct_types_.find(struct_type->get_name());
                if (it != named_struct_types_.end()) {
                    return it->second;
                }
            }
        } else if (type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_type = static_cast<const UnionSemanticType *>(type);
            if (union_type->get_fields().empty()) {
                const auto it = named_union_types_.find(union_type->get_name());
                if (it != named_union_types_.end()) {
                    return it->second;
                }
            }
        }
        return type;
    }

    std::optional<std::size_t>
    find_aggregate_field_index(const SemanticType *owner_type,
                               const std::string &field_name) const {
        owner_type = get_canonical_aggregate_type(owner_type);
        if (owner_type == nullptr) {
            return std::nullopt;
        }
        if (owner_type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_type =
                static_cast<const StructSemanticType *>(owner_type);
            const auto &fields = struct_type->get_fields();
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (fields[index].get_name() == field_name) {
                    return index;
                }
            }
        }
        if (owner_type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_type =
                static_cast<const UnionSemanticType *>(owner_type);
            const auto &fields = union_type->get_fields();
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (fields[index].get_name() == field_name) {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    CoreIrValue *build_index_address(const IndexExpr &expr) {
        const SemanticType *base_type = strip_qualifiers(get_node_type(expr.get_base()));
        if (base_type == nullptr) {
            add_error("core ir generation could not resolve index base type",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *base_address = nullptr;
        std::vector<CoreIrValue *> indices;
        const SemanticType *element_semantic_type = nullptr;
        const CoreIrType *source_pointee_type = nullptr;
        if (base_type->get_kind() == SemanticTypeKind::Array) {
            const auto *array_type = static_cast<const ArraySemanticType *>(base_type);
            base_address = build_lvalue_address(expr.get_base());
            if (base_address == nullptr) {
                return nullptr;
            }
            indices.push_back(create_i32_constant(0, expr.get_source_span()));
            element_semantic_type = array_type->get_element_type();
            source_pointee_type = get_or_create_type(base_type);
        } else if (base_type->get_kind() == SemanticTypeKind::Pointer) {
            const auto *pointer_type =
                static_cast<const PointerSemanticType *>(base_type);
            base_address = build_expr(expr.get_base());
            if (base_address == nullptr) {
                return nullptr;
            }
            element_semantic_type = pointer_type->get_pointee_type();
        } else {
            add_error("core ir generation expected array or pointer base for index "
                      "expression",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *index_value = build_expr(expr.get_index());
        if (index_value == nullptr) {
            return nullptr;
        }
        const SemanticType *index_semantic_type = get_node_type(expr.get_index());
        if (is_integer_semantic_type(index_semantic_type)) {
            const SemanticType *promoted_index_type =
                get_integer_promotion_type(index_semantic_type,
                                           expr.get_index()->get_source_span());
            if (promoted_index_type == nullptr) {
                return nullptr;
            }
            index_value = build_converted_value(
                index_value, index_semantic_type, promoted_index_type,
                expr.get_index()->get_source_span());
            if (index_value == nullptr) {
                return nullptr;
            }
        }
        indices.push_back(index_value);

        const CoreIrType *element_type = get_or_create_type(element_semantic_type);
        if (element_type == nullptr) {
            add_error("core ir generation could not resolve indexed element type",
                      expr.get_source_span());
            return nullptr;
        }
        if (source_pointee_type == nullptr) {
            source_pointee_type = element_type;
        }
        return build_gep(base_address, element_type, std::move(indices),
                         expr.get_source_span(), source_pointee_type);
    }

    std::optional<BitFieldLValue> get_bit_field_lvalue(const Expr *expr) {
        const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
        if (member_expr == nullptr) {
            return std::nullopt;
        }

        const SemanticType *owner_type = get_member_owner_type(
            get_node_type(member_expr->get_base()),
            member_expr->get_operator_text());
        owner_type = get_canonical_aggregate_type(owner_type);
        const SemanticType *unqualified_owner_type = strip_qualifiers(owner_type);
        if (owner_type == nullptr ||
            (unqualified_owner_type->get_kind() != SemanticTypeKind::Struct &&
             unqualified_owner_type->get_kind() != SemanticTypeKind::Union)) {
            return std::nullopt;
        }

        const std::optional<std::size_t> field_index =
            find_aggregate_field_index(owner_type, member_expr->get_member_name());
        if (!field_index.has_value()) {
            return std::nullopt;
        }
        const std::optional<detail::AggregateFieldLayout> field_layout =
            detail::get_aggregate_field_layout(owner_type, *field_index);
        if (!field_layout.has_value() || !field_layout->is_bit_field) {
            return std::nullopt;
        }

        CoreIrValue *base_address = nullptr;
        if (member_expr->get_operator_text() == ".") {
            base_address = build_lvalue_address(member_expr->get_base());
        } else if (member_expr->get_operator_text() == "->") {
            base_address = build_expr(member_expr->get_base());
        }
        if (base_address == nullptr) {
            return std::nullopt;
        }

        const CoreIrType *storage_type =
            get_or_create_type(field_layout->storage_type);
        if (storage_type == nullptr) {
            add_error("core ir generation could not resolve bit-field storage type",
                      member_expr->get_source_span());
            return std::nullopt;
        }
        const CoreIrType *owner_core_type = get_or_create_type(owner_type);
        if (owner_core_type == nullptr) {
            add_error("core ir generation could not resolve bit-field owner type",
                      member_expr->get_source_span());
            return std::nullopt;
        }

        CoreIrValue *storage_address =
            build_gep(base_address, storage_type,
                      {create_i32_constant(0, member_expr->get_source_span()),
                       create_i32_constant(
                           static_cast<long long>(
                               unqualified_owner_type->get_kind() ==
                                       SemanticTypeKind::Union
                                   ? 0
                                   : field_layout->llvm_element_index),
                           member_expr->get_source_span())},
                      member_expr->get_source_span(),
                      unqualified_owner_type->get_kind() == SemanticTypeKind::Union
                          ? storage_type
                          : owner_core_type);
        if (storage_address == nullptr) {
            return std::nullopt;
        }

        return BitFieldLValue{storage_address, *field_layout,
                              get_node_type(member_expr)};
    }

    CoreIrValue *build_bit_field_value(const BitFieldLValue &lvalue,
                                       SourceSpan source_span) {
        if (lvalue.storage_address == nullptr ||
            lvalue.layout.storage_type == nullptr ||
            lvalue.field_semantic_type == nullptr ||
            lvalue.layout.bit_width == 0) {
            add_error("core ir generation encountered an incomplete bit-field "
                      "lvalue",
                      source_span);
            return nullptr;
        }

        const CoreIrType *storage_type =
            lvalue.layout.storage_bit_width == 0
                ? get_or_create_type(lvalue.layout.storage_type)
                : core_ir_context_->create_type<CoreIrIntegerType>(
                      lvalue.layout.storage_bit_width,
                      lvalue.layout.storage_is_signed);
        if (storage_type == nullptr) {
            add_error("core ir generation could not resolve bit-field storage type",
                      source_span);
            return nullptr;
        }

        auto *storage_load = current_block_->create_instruction<CoreIrLoadInst>(
            storage_type, next_temp_name(), lvalue.storage_address);
        storage_load->set_source_span(source_span);

        CoreIrValue *shifted_value = storage_load;
        if (lvalue.layout.bit_offset > 0) {
            auto *shift_instruction =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::LShr, storage_type, next_temp_name(),
                    shifted_value,
                    build_integer_constant(storage_type, lvalue.layout.bit_offset,
                                           source_span));
            shift_instruction->set_source_span(source_span);
            shifted_value = shift_instruction;
        }

        const std::uint64_t field_mask =
            get_low_bit_mask(lvalue.layout.bit_width);
        auto *masked_value =
            current_block_->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, storage_type, next_temp_name(),
                shifted_value,
                build_integer_constant(storage_type, field_mask, source_span));
        masked_value->set_source_span(source_span);

        CoreIrValue *field_value = masked_value;
        detail::IntegerConversionService integer_conversion_service;
        const auto storage_info =
            integer_conversion_service.get_integer_type_info(
                lvalue.layout.storage_type);
        if (storage_info.has_value() && storage_info->get_is_signed()) {
            const std::size_t storage_bits =
                static_cast<std::size_t>(storage_info->get_bit_width());
            if (lvalue.layout.bit_width < storage_bits) {
                const std::size_t sign_shift =
                    storage_bits - lvalue.layout.bit_width;
                auto *left_shift =
                    current_block_->create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Shl, storage_type, next_temp_name(),
                        field_value,
                        build_integer_constant(storage_type, sign_shift,
                                               source_span));
                left_shift->set_source_span(source_span);
                auto *right_shift =
                    current_block_->create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::AShr, storage_type, next_temp_name(),
                        left_shift,
                        build_integer_constant(storage_type, sign_shift,
                                               source_span));
                right_shift->set_source_span(source_span);
                field_value = right_shift;
            }
        }

        return build_converted_value(field_value, lvalue.layout.storage_type,
                                     lvalue.field_semantic_type, source_span);
    }

    CoreIrValue *store_bit_field_value(const BitFieldLValue &lvalue,
                                       CoreIrValue *value,
                                       const SemanticType *value_semantic_type,
                                       SourceSpan source_span) {
        if (value == nullptr || lvalue.storage_address == nullptr ||
            lvalue.layout.storage_type == nullptr ||
            lvalue.field_semantic_type == nullptr ||
            lvalue.layout.bit_width == 0) {
            add_error("core ir generation encountered an incomplete bit-field "
                      "store",
                      source_span);
            return nullptr;
        }

        const CoreIrType *storage_type =
            lvalue.layout.storage_bit_width == 0
                ? get_or_create_type(lvalue.layout.storage_type)
                : core_ir_context_->create_type<CoreIrIntegerType>(
                      lvalue.layout.storage_bit_width,
                      lvalue.layout.storage_is_signed);
        if (storage_type == nullptr) {
            add_error("core ir generation could not resolve bit-field storage type",
                      source_span);
            return nullptr;
        }

        CoreIrValue *field_value = build_converted_value(
            value, value_semantic_type, lvalue.field_semantic_type, source_span);
        if (field_value == nullptr) {
            return nullptr;
        }
        field_value = build_converted_value(field_value, lvalue.field_semantic_type,
                                            lvalue.layout.storage_type, source_span);
        if (field_value == nullptr) {
            return nullptr;
        }
        if (!are_equivalent_types(field_value->get_type(), storage_type)) {
            const auto *source_integer_type =
                dynamic_cast<const CoreIrIntegerType *>(field_value->get_type());
            const auto *target_integer_type =
                dynamic_cast<const CoreIrIntegerType *>(storage_type);
            if (source_integer_type == nullptr || target_integer_type == nullptr) {
                add_error("core ir generation expected bit-field storage integer "
                          "types",
                          source_span);
                return nullptr;
            }
            CoreIrCastKind cast_kind = CoreIrCastKind::Truncate;
            if (source_integer_type->get_bit_width() <
                target_integer_type->get_bit_width()) {
                cast_kind = lvalue.layout.storage_is_signed
                                ? CoreIrCastKind::SignExtend
                                : CoreIrCastKind::ZeroExtend;
            } else if (source_integer_type->get_bit_width() ==
                       target_integer_type->get_bit_width()) {
                cast_kind = CoreIrCastKind::Truncate;
            }
            if (source_integer_type->get_bit_width() !=
                target_integer_type->get_bit_width()) {
                auto *storage_cast =
                    current_block_->create_instruction<CoreIrCastInst>(
                        cast_kind, storage_type, next_temp_name(), field_value);
                storage_cast->set_source_span(source_span);
                field_value = storage_cast;
            }
        }

        const std::uint64_t field_mask =
            get_low_bit_mask(lvalue.layout.bit_width);
        auto *masked_field =
            current_block_->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, storage_type, next_temp_name(),
                field_value,
                build_integer_constant(storage_type, field_mask, source_span));
        masked_field->set_source_span(source_span);

        CoreIrValue *shifted_field = masked_field;
        if (lvalue.layout.bit_offset > 0) {
            auto *shift_instruction =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Shl, storage_type, next_temp_name(),
                    shifted_field,
                    build_integer_constant(storage_type, lvalue.layout.bit_offset,
                                           source_span));
            shift_instruction->set_source_span(source_span);
            shifted_field = shift_instruction;
        }

        auto *storage_load = current_block_->create_instruction<CoreIrLoadInst>(
            storage_type, next_temp_name(), lvalue.storage_address);
        storage_load->set_source_span(source_span);

        const std::uint64_t shifted_field_mask =
            field_mask << lvalue.layout.bit_offset;
        auto *preserved_bits =
            current_block_->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::And, storage_type, next_temp_name(),
                storage_load,
                build_integer_constant(storage_type, ~shifted_field_mask,
                                       source_span));
        preserved_bits->set_source_span(source_span);

        auto *merged_storage =
            current_block_->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Or, storage_type, next_temp_name(),
                preserved_bits, shifted_field);
        merged_storage->set_source_span(source_span);

        auto *store_instruction =
            current_block_->create_instruction<CoreIrStoreInst>(
                void_type_, merged_storage, lvalue.storage_address);
        store_instruction->set_source_span(source_span);

        return build_bit_field_value(lvalue, source_span);
    }

    CoreIrValue *build_member_address(const MemberExpr &expr) {
        const SemanticType *owner_type = get_member_owner_type(
            get_node_type(expr.get_base()), expr.get_operator_text());
        if (owner_type == nullptr) {
            add_error("core ir generation could not resolve member owner type",
                      expr.get_source_span());
            return nullptr;
        }
        owner_type = get_canonical_aggregate_type(owner_type);
        const SemanticType *unqualified_owner_type = strip_qualifiers(owner_type);
        if (unqualified_owner_type->get_kind() != SemanticTypeKind::Struct &&
            unqualified_owner_type->get_kind() != SemanticTypeKind::Union) {
            add_error("core ir generation currently supports only struct/union "
                      "member access in staged Core IR",
                      expr.get_source_span());
            return nullptr;
        }

        const std::optional<std::size_t> field_index =
            find_aggregate_field_index(owner_type, expr.get_member_name());
        if (!field_index.has_value()) {
            add_error("core ir generation could not resolve member '" +
                          expr.get_member_name() + "'",
                      expr.get_source_span());
            return nullptr;
        }

        const std::optional<detail::AggregateFieldLayout> field_layout =
            detail::get_aggregate_field_layout(owner_type, *field_index);
        if (!field_layout.has_value()) {
            add_error("core ir generation could not resolve member layout",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *base_address = nullptr;
        if (expr.get_operator_text() == ".") {
            base_address = build_lvalue_address(expr.get_base());
        } else if (expr.get_operator_text() == "->") {
            base_address = build_expr(expr.get_base());
        }
        if (base_address == nullptr) {
            return nullptr;
        }

        if (const auto cache_it = type_cache_.find(owner_type);
            cache_it != type_cache_.end()) {
            const auto *cached_struct_type =
                dynamic_cast<const CoreIrStructType *>(cache_it->second);
            if (cached_struct_type != nullptr &&
                cached_struct_type->get_element_types().empty()) {
                type_cache_.erase(cache_it);
            }
        }
        const CoreIrType *owner_core_type = get_or_create_type(owner_type);
        if (const auto *owner_struct_core_type =
                dynamic_cast<const CoreIrStructType *>(owner_core_type);
            owner_struct_core_type != nullptr &&
            owner_struct_core_type->get_element_types().empty()) {
            const detail::AggregateLayoutInfo layout =
                detail::compute_aggregate_layout(owner_type);
            if (!layout.elements.empty()) {
                auto *fresh_struct_type =
                    core_ir_context_->create_type<CoreIrStructType>();
                std::vector<const CoreIrType *> element_types;
                element_types.reserve(layout.elements.size());
                bool requires_packed_storage = false;
                const auto *byte_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(8);
                for (const auto &element : layout.elements) {
                    if (element.kind ==
                        detail::AggregateLayoutElementKind::Padding) {
                        element_types.push_back(
                            core_ir_context_->create_type<CoreIrArrayType>(
                                byte_type, element.padding_size));
                        continue;
                    }
                    if (element.kind ==
                        detail::AggregateLayoutElementKind::BitFieldStorage) {
                        requires_packed_storage = true;
                        element_types.push_back(
                            core_ir_context_->create_type<CoreIrIntegerType>(
                                element.bit_width, element.is_signed));
                        continue;
                    }
                    const CoreIrType *element_type =
                        get_or_create_type(element.type);
                    if (element_type == nullptr) {
                        return nullptr;
                    }
                    element_types.push_back(element_type);
                }
                fresh_struct_type->set_element_types(std::move(element_types));
                fresh_struct_type->set_is_packed(requires_packed_storage);
                owner_core_type = fresh_struct_type;
            }
        }
        if (owner_core_type == nullptr) {
            add_error("core ir generation could not resolve member owner core type",
                      expr.get_source_span());
            return nullptr;
        }
        base_address =
            retag_pointer_for_gep(base_address, owner_core_type,
                                  expr.get_source_span());
        if (base_address == nullptr) {
            return nullptr;
        }

        const CoreIrType *field_type =
            field_layout->is_bit_field && field_layout->storage_bit_width != 0
                ? core_ir_context_->create_type<CoreIrIntegerType>(
                      field_layout->storage_bit_width,
                      field_layout->storage_is_signed)
                : get_or_create_type(field_layout->is_bit_field
                                         ? field_layout->storage_type
                                         : field_layout->field_type);
        if (field_type == nullptr) {
            add_error("core ir generation could not resolve member field type",
                      expr.get_source_span());
            return nullptr;
        }

        if (unqualified_owner_type->get_kind() == SemanticTypeKind::Union) {
            return build_gep(base_address, field_type,
                             {create_i32_constant(0, expr.get_source_span())},
                             expr.get_source_span(), field_type);
        }

        return build_gep(base_address, field_type,
                         {create_i32_constant(0, expr.get_source_span()),
                          create_i32_constant(
                              static_cast<long long>(
                                  field_layout->llvm_element_index),
                              expr.get_source_span())},
                         expr.get_source_span(), owner_core_type);
    }

    CoreIrValue *build_lvalue_address(const Expr *expr) {
        if (expr == nullptr) {
            return nullptr;
        }
        if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(expr);
            identifier != nullptr) {
            const SemanticSymbol *symbol = get_symbol_binding(identifier);
            if (symbol == nullptr) {
                add_error("core ir generation could not resolve identifier: " +
                              identifier->get_name(),
                          identifier->get_source_span());
                return nullptr;
            }
            if (strip_qualifiers(symbol->get_type()) != nullptr &&
                strip_qualifiers(symbol->get_type())->get_kind() ==
                    SemanticTypeKind::Function) {
                CoreIrFunction *function =
                    ensure_function_binding(symbol, identifier->get_source_span());
                if (function == nullptr) {
                    return nullptr;
                }
                return build_function_address(*function,
                                              identifier->get_source_span());
            }
            ValueBinding *binding = find_value_binding(symbol);
            if (binding == nullptr) {
                add_error("core ir generation does not yet support identifier "
                          "storage: " + identifier->get_name(),
                          identifier->get_source_span());
                return nullptr;
            }
            if (binding->stack_slot != nullptr) {
                return build_stack_slot_address(*binding->stack_slot,
                                               identifier->get_source_span());
            }
            if (binding->global != nullptr) {
                return build_global_address(*binding->global,
                                            identifier->get_source_span());
            }
            add_error("core ir generation found an empty identifier binding: " +
                          identifier->get_name(),
                      identifier->get_source_span());
            return nullptr;
        }
        if (const auto *unary_expr = dynamic_cast<const UnaryExpr *>(expr);
            unary_expr != nullptr && unary_expr->get_operator_text() == "*") {
            return build_expr(unary_expr->get_operand());
        }
        if (const auto *string_literal =
                dynamic_cast<const StringLiteralExpr *>(expr);
            string_literal != nullptr) {
            return build_string_literal_array_address(*string_literal);
        }
        if (const auto *index_expr = dynamic_cast<const IndexExpr *>(expr);
            index_expr != nullptr) {
            return build_index_address(*index_expr);
        }
        if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
            member_expr != nullptr) {
            return build_member_address(*member_expr);
        }

        add_error("core ir generation does not support this lvalue/address "
                  "expression yet",
                  expr->get_source_span());
        return nullptr;
    }

    CoreIrValue *build_identifier(const IdentifierExpr &expr) {
        const SemanticSymbol *symbol = get_symbol_binding(&expr);
        if (symbol == nullptr) {
            add_error("core ir generation could not resolve identifier: " +
                          expr.get_name(),
                      expr.get_source_span());
            return nullptr;
        }
        if ((symbol->get_kind() == SymbolKind::Enumerator ||
             symbol->get_kind() == SymbolKind::Constant) &&
            get_integer_constant_value(&expr).has_value()) {
            const CoreIrType *core_type = get_or_create_type(get_node_type(&expr));
            if (core_type == nullptr) {
                add_error("core ir generation could not resolve constant identifier "
                          "type",
                          expr.get_source_span());
                return nullptr;
            }
            auto *constant = core_ir_context_->create_constant<CoreIrConstantInt>(
                core_type,
                static_cast<std::uint64_t>(*get_integer_constant_value(&expr)));
            constant->set_source_span(expr.get_source_span());
            return constant;
        }
        if (symbol->get_kind() == SymbolKind::Constant &&
            find_value_binding(symbol) == nullptr) {
            if (const auto *const_decl =
                    dynamic_cast<const ConstDecl *>(symbol->get_decl_node());
                const_decl != nullptr && is_scalar_semantic_type(symbol->get_type()) &&
                const_decl->get_initializer() != nullptr) {
                CoreIrValue *initializer_value =
                    build_expr(const_decl->get_initializer());
                if (initializer_value == nullptr) {
                    return nullptr;
                }
                return build_converted_value(
                    initializer_value, get_node_type(const_decl->get_initializer()),
                    symbol->get_type(), const_decl->get_source_span());
            }
        }
        if (strip_qualifiers(symbol->get_type()) != nullptr &&
            strip_qualifiers(symbol->get_type())->get_kind() ==
                SemanticTypeKind::Function) {
            CoreIrFunction *function =
                ensure_function_binding(symbol, expr.get_source_span());
            if (function == nullptr) {
                return nullptr;
            }
            return build_function_address(*function, expr.get_source_span());
        }

        ValueBinding *binding = find_value_binding(symbol);
        if (binding == nullptr) {
            add_error("core ir generation does not yet support identifier storage: " +
                          expr.get_name(),
                      expr.get_source_span());
            return nullptr;
        }
        if (binding->value != nullptr) {
            return binding->value;
        }

        CoreIrValue *address = build_lvalue_address(&expr);
        if (address == nullptr) {
            return nullptr;
        }
        return build_value_from_lvalue(expr, address);
    }

    CoreIrValue *build_string_literal_expr(const StringLiteralExpr &expr) {
        CoreIrValue *array_address = build_string_literal_array_address(expr);
        if (array_address == nullptr) {
            return nullptr;
        }

        const auto *char_type = core_ir_context_->create_type<CoreIrIntegerType>(8);
        return build_gep(array_address, char_type,
                         {create_i32_constant(0, expr.get_source_span()),
                          create_i32_constant(0, expr.get_source_span())},
                         expr.get_source_span());
    }

    CoreIrValue *build_string_literal_array_address(const StringLiteralExpr &expr) {
        const std::string decoded_text =
            decode_string_literal_token(expr.get_value_text());
        const std::string global_key = expr.get_value_text();
        CoreIrGlobal *global = nullptr;
        const auto global_it = string_literal_globals_.find(global_key);
        if (global_it != string_literal_globals_.end()) {
            global = global_it->second;
        } else {
            const auto *char_type = core_ir_context_->create_type<CoreIrIntegerType>(8);
            const auto *array_type = core_ir_context_->create_type<CoreIrArrayType>(
                char_type, decoded_text.size() + 1);
            std::vector<std::uint8_t> bytes(decoded_text.begin(), decoded_text.end());
            bytes.push_back(0);
            const auto *initializer =
                core_ir_context_->create_constant<CoreIrConstantByteString>(
                    array_type, std::move(bytes));
            global = module_->create_global<CoreIrGlobal>(
                next_string_literal_name(), array_type, initializer, true, true);
            string_literal_globals_[global_key] = global;
        }

        const auto *array_pointer_type =
            core_ir_context_->create_type<CoreIrPointerType>(global->get_type());
        auto *address_instruction =
            current_block_->create_instruction<CoreIrAddressOfGlobalInst>(
                array_pointer_type, next_temp_name(), global);
        address_instruction->set_source_span(expr.get_source_span());
        return address_instruction;
    }

    bool are_equivalent_types(const CoreIrType *lhs, const CoreIrType *rhs) const {
        if (lhs == rhs) {
            return true;
        }
        if (lhs == nullptr || rhs == nullptr || lhs->get_kind() != rhs->get_kind()) {
            return false;
        }
        switch (lhs->get_kind()) {
        case CoreIrTypeKind::Void:
            return true;
        case CoreIrTypeKind::Integer:
            return static_cast<const CoreIrIntegerType *>(lhs)->get_bit_width() ==
                       static_cast<const CoreIrIntegerType *>(rhs)->get_bit_width() &&
                   static_cast<const CoreIrIntegerType *>(lhs)->get_is_signed() ==
                       static_cast<const CoreIrIntegerType *>(rhs)->get_is_signed();
        case CoreIrTypeKind::Float:
            return static_cast<const CoreIrFloatType *>(lhs)->get_float_kind() ==
                   static_cast<const CoreIrFloatType *>(rhs)->get_float_kind();
        case CoreIrTypeKind::Pointer:
            return true;
        case CoreIrTypeKind::Array:
            return static_cast<const CoreIrArrayType *>(lhs)->get_element_count() ==
                       static_cast<const CoreIrArrayType *>(rhs)->get_element_count() &&
                   are_equivalent_types(
                       static_cast<const CoreIrArrayType *>(lhs)->get_element_type(),
                       static_cast<const CoreIrArrayType *>(rhs)->get_element_type());
        case CoreIrTypeKind::Struct: {
            const auto *lhs_struct = static_cast<const CoreIrStructType *>(lhs);
            const auto *rhs_struct = static_cast<const CoreIrStructType *>(rhs);
            if (lhs_struct->get_is_packed() != rhs_struct->get_is_packed()) {
                return false;
            }
            const auto &lhs_elements = lhs_struct->get_element_types();
            const auto &rhs_elements = rhs_struct->get_element_types();
            if (lhs_elements.size() != rhs_elements.size()) {
                return false;
            }
            for (std::size_t index = 0; index < lhs_elements.size(); ++index) {
                if (!are_equivalent_types(lhs_elements[index], rhs_elements[index])) {
                    return false;
                }
            }
            return true;
        }
        case CoreIrTypeKind::Function: {
            const auto *lhs_function =
                static_cast<const CoreIrFunctionType *>(lhs);
            const auto *rhs_function =
                static_cast<const CoreIrFunctionType *>(rhs);
            if (lhs_function->get_is_variadic() != rhs_function->get_is_variadic() ||
                !are_equivalent_types(lhs_function->get_return_type(),
                                      rhs_function->get_return_type())) {
                return false;
            }
            const auto &lhs_parameters = lhs_function->get_parameter_types();
            const auto &rhs_parameters = rhs_function->get_parameter_types();
            if (lhs_parameters.size() != rhs_parameters.size()) {
                return false;
            }
            for (std::size_t index = 0; index < lhs_parameters.size(); ++index) {
                if (!are_equivalent_types(lhs_parameters[index], rhs_parameters[index])) {
                    return false;
                }
            }
            return true;
        }
        }
        return false;
    }

    CoreIrValue *build_converted_value(CoreIrValue *value,
                                       const SemanticType *source_semantic_type,
                                       const SemanticType *target_semantic_type,
                                       SourceSpan source_span) {
        if (value == nullptr || target_semantic_type == nullptr) {
            return nullptr;
        }
        const CoreIrType *target_type = get_or_create_type(target_semantic_type);
        if (target_type == nullptr) {
            return nullptr;
        }
        if (target_type == void_type_) {
            return value;
        }
        if (are_equivalent_types(value->get_type(), target_type)) {
            return value;
        }

        CoreIrCastKind cast_kind;
        if (is_integer_semantic_type(source_semantic_type) &&
            is_integer_semantic_type(target_semantic_type)) {
            const auto *source_integer_type =
                dynamic_cast<const CoreIrIntegerType *>(value->get_type());
            const auto *target_integer_type =
                dynamic_cast<const CoreIrIntegerType *>(target_type);
            if (source_integer_type == nullptr || target_integer_type == nullptr) {
                add_error("core ir generation expected integer conversion types",
                          source_span);
                return nullptr;
            }
            if (source_integer_type->get_bit_width() >
                target_integer_type->get_bit_width()) {
                cast_kind = CoreIrCastKind::Truncate;
            } else if (source_integer_type->get_bit_width() <
                       target_integer_type->get_bit_width()) {
                cast_kind = is_unsigned_integer_semantic_type(source_semantic_type)
                                ? CoreIrCastKind::ZeroExtend
                                : CoreIrCastKind::SignExtend;
            } else {
                if (source_integer_type->get_is_signed() ==
                    target_integer_type->get_is_signed()) {
                    return value;
                }
                cast_kind = target_integer_type->get_is_signed()
                                ? CoreIrCastKind::SignExtend
                                : CoreIrCastKind::ZeroExtend;
            }
        } else if (is_integer_semantic_type(source_semantic_type) &&
                   is_float_semantic_type(target_semantic_type)) {
            cast_kind = is_unsigned_integer_semantic_type(source_semantic_type)
                            ? CoreIrCastKind::UnsignedIntToFloat
                            : CoreIrCastKind::SignedIntToFloat;
        } else if (is_float_semantic_type(source_semantic_type) &&
                   is_integer_semantic_type(target_semantic_type)) {
            cast_kind = is_unsigned_integer_semantic_type(target_semantic_type)
                            ? CoreIrCastKind::FloatToUnsignedInt
                            : CoreIrCastKind::FloatToSignedInt;
        } else if (is_float_semantic_type(source_semantic_type) &&
                   is_float_semantic_type(target_semantic_type)) {
            const auto *source_float_type =
                dynamic_cast<const CoreIrFloatType *>(value->get_type());
            const auto *target_float_type =
                dynamic_cast<const CoreIrFloatType *>(target_type);
            if (source_float_type == nullptr || target_float_type == nullptr) {
                add_error("core ir generation expected floating conversion types",
                          source_span);
                return nullptr;
            }
            cast_kind =
                static_cast<int>(source_float_type->get_float_kind()) <
                        static_cast<int>(target_float_type->get_float_kind())
                    ? CoreIrCastKind::FloatExtend
                    : CoreIrCastKind::FloatTruncate;
        } else if (value->get_type() != nullptr &&
                   value->get_type()->get_kind() == CoreIrTypeKind::Pointer &&
                   target_type->get_kind() == CoreIrTypeKind::Pointer) {
            const auto *target_pointer_type =
                dynamic_cast<const CoreIrPointerType *>(target_type);
            const CoreIrType *target_pointee_type =
                target_pointer_type == nullptr
                    ? nullptr
                    : target_pointer_type->get_pointee_type();
            if (target_pointee_type != nullptr &&
                target_pointee_type->get_kind() != CoreIrTypeKind::Void &&
                target_pointee_type->get_kind() != CoreIrTypeKind::Function) {
                return retag_pointer_for_gep(value, target_pointee_type,
                                             source_span);
            }
            return value;
        } else if (value->get_type() != nullptr &&
                   value->get_type()->get_kind() == CoreIrTypeKind::Pointer &&
                   is_integer_semantic_type(target_semantic_type)) {
            cast_kind = CoreIrCastKind::PtrToInt;
        } else if (is_integer_semantic_type(source_semantic_type) &&
                   target_type->get_kind() == CoreIrTypeKind::Pointer) {
            if (const auto *int_constant =
                    dynamic_cast<const CoreIrConstantInt *>(value);
                int_constant != nullptr && int_constant->get_value() == 0) {
                auto *null_constant =
                    core_ir_context_->create_constant<CoreIrConstantNull>(
                        target_type);
                null_constant->set_source_span(source_span);
                return null_constant;
            }
            cast_kind = CoreIrCastKind::IntToPtr;
        } else {
            add_error("core ir generation does not yet support this conversion",
                      source_span);
            return nullptr;
        }

        auto *cast = current_block_->create_instruction<CoreIrCastInst>(
            cast_kind, target_type, next_temp_name(), value);
        cast->set_source_span(source_span);
        return cast;
    }

    CoreIrValue *build_cast_expr(const CastExpr &expr) {
        CoreIrValue *operand = build_expr(expr.get_operand());
        if (operand == nullptr) {
            return nullptr;
        }
        return build_converted_value(operand, get_node_type(expr.get_operand()),
                                     get_node_type(&expr), expr.get_source_span());
    }

    std::optional<CoreIrUnaryOpcode>
    get_unary_opcode(const std::string &operator_text) const {
        if (operator_text == "-") {
            return CoreIrUnaryOpcode::Negate;
        }
        if (operator_text == "~") {
            return CoreIrUnaryOpcode::BitwiseNot;
        }
        if (operator_text == "!") {
            return CoreIrUnaryOpcode::LogicalNot;
        }
        return std::nullopt;
    }

    CoreIrValue *build_unary_expr(const UnaryExpr &expr) {
        if (expr.get_operator_text() == "&&") {
            return build_label_address_expr(expr);
        }
        if (expr.get_operator_text() == "sizeof") {
            return build_integer_constant_expr(expr);
        }
        if (expr.get_operator_text() == "+") {
            CoreIrValue *operand = build_expr(expr.get_operand());
            if (operand == nullptr) {
                return nullptr;
            }
            return build_converted_value(operand, get_node_type(expr.get_operand()),
                                         get_node_type(&expr),
                                         expr.get_source_span());
        }
        if (expr.get_operator_text() == "&") {
            return build_lvalue_address(expr.get_operand());
        }
        if (expr.get_operator_text() == "*") {
            CoreIrValue *address = build_expr(expr.get_operand());
            if (address == nullptr) {
                return nullptr;
            }
            return build_value_from_lvalue(expr, address);
        }

        const std::optional<CoreIrUnaryOpcode> unary_opcode =
            get_unary_opcode(expr.get_operator_text());
        if (!unary_opcode.has_value()) {
            add_error("core ir generation does not support unary operator '" +
                          expr.get_operator_text() + "' yet",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *operand = build_expr(expr.get_operand());
        if (operand == nullptr) {
            return nullptr;
        }
        const CoreIrType *result_type =
            get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve unary result type",
                      expr.get_source_span());
            return nullptr;
        }
        if (*unary_opcode != CoreIrUnaryOpcode::LogicalNot) {
            operand = build_converted_value(operand, get_node_type(expr.get_operand()),
                                            get_node_type(&expr),
                                            expr.get_source_span());
            if (operand == nullptr) {
                return nullptr;
            }
        }
        auto *instruction = current_block_->create_instruction<CoreIrUnaryInst>(
            *unary_opcode, result_type, next_temp_name(), operand);
        instruction->set_source_span(expr.get_source_span());
        return instruction;
    }

    CoreIrValue *build_pointer_step(CoreIrValue *pointer_value,
                                    const SemanticType *pointer_semantic_type,
                                    long long step,
                                    SourceSpan source_span) {
        const auto *unqualified_pointer_type =
            strip_qualifiers(pointer_semantic_type);
        if (unqualified_pointer_type == nullptr ||
            unqualified_pointer_type->get_kind() != SemanticTypeKind::Pointer) {
            add_error("core ir generation expected a pointer type for pointer "
                      "step expression",
                      source_span);
            return nullptr;
        }

        const auto *pointer_type =
            static_cast<const PointerSemanticType *>(unqualified_pointer_type);
        const CoreIrType *pointee_type =
            get_or_create_type(pointer_type->get_pointee_type());
        if (pointee_type == nullptr) {
            add_error("core ir generation could not resolve pointer pointee type",
                      source_span);
            return nullptr;
        }

        return build_gep(pointer_value, pointee_type,
                         {create_i32_constant(step, source_span)}, source_span,
                         pointee_type);
    }

    CoreIrValue *build_pointer_step(CoreIrValue *pointer_value,
                                    const SemanticType *pointer_semantic_type,
                                    CoreIrValue *step_value,
                                    SourceSpan source_span) {
        const auto *unqualified_pointer_type =
            strip_qualifiers(pointer_semantic_type);
        if (unqualified_pointer_type == nullptr ||
            unqualified_pointer_type->get_kind() != SemanticTypeKind::Pointer) {
            add_error("core ir generation expected a pointer type for pointer "
                      "step expression",
                      source_span);
            return nullptr;
        }
        if (step_value == nullptr) {
            add_error("core ir generation expected a step value for pointer step "
                      "expression",
                      source_span);
            return nullptr;
        }

        const auto *pointer_type =
            static_cast<const PointerSemanticType *>(unqualified_pointer_type);
        const CoreIrType *pointee_type =
            get_or_create_type(pointer_type->get_pointee_type());
        if (pointee_type == nullptr) {
            add_error("core ir generation could not resolve pointer pointee type",
                      source_span);
            return nullptr;
        }

        return build_gep(pointer_value, pointee_type, {step_value}, source_span,
                         pointee_type);
    }

    CoreIrValue *build_increment_result(CoreIrValue *current_value,
                                        const SemanticType *operand_semantic_type,
                                        bool is_increment,
                                        SourceSpan source_span) {
        if (current_value == nullptr) {
            return nullptr;
        }

        operand_semantic_type = strip_qualifiers(operand_semantic_type);
        if (operand_semantic_type == nullptr) {
            add_error("core ir generation could not resolve increment operand "
                      "type",
                      source_span);
            return nullptr;
        }

        if (operand_semantic_type->get_kind() == SemanticTypeKind::Pointer) {
            return build_pointer_step(current_value, operand_semantic_type,
                                      is_increment ? 1 : -1, source_span);
        }

        CoreIrValue *step_value =
            build_converted_value(create_i32_constant(1, source_span),
                                  get_static_builtin_semantic_type("int"),
                                  operand_semantic_type, source_span);
        if (step_value == nullptr) {
            return nullptr;
        }

        const CoreIrType *result_type = get_or_create_type(operand_semantic_type);
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve increment result type",
                      source_span);
            return nullptr;
        }

        auto *instruction = current_block_->create_instruction<CoreIrBinaryInst>(
            is_increment ? CoreIrBinaryOpcode::Add : CoreIrBinaryOpcode::Sub,
            result_type, next_temp_name(), current_value, step_value);
        instruction->set_source_span(source_span);
        return instruction;
    }

    CoreIrValue *build_increment_expr(const Expr *operand, bool is_increment,
                                      bool returns_updated_value,
                                      SourceSpan source_span) {
        CoreIrValue *address = build_lvalue_address(operand);
        if (address == nullptr) {
            return nullptr;
        }

        CoreIrValue *current_value =
            build_value_from_lvalue(*operand, address);
        if (current_value == nullptr) {
            return nullptr;
        }

        CoreIrValue *updated_value =
            build_increment_result(current_value, get_node_type(operand),
                                   is_increment, source_span);
        if (updated_value == nullptr) {
            return nullptr;
        }

        auto *store = current_block_->create_instruction<CoreIrStoreInst>(
            void_type_, updated_value, address);
        store->set_source_span(source_span);
        return returns_updated_value ? updated_value : current_value;
    }

    CoreIrValue *build_prefix_expr(const PrefixExpr &expr) {
        if (expr.get_operator_text() != "++" &&
            expr.get_operator_text() != "--") {
            add_error("core ir generation does not support prefix operator '" +
                          expr.get_operator_text() + "' yet",
                      expr.get_source_span());
            return nullptr;
        }

        return build_increment_expr(expr.get_operand(),
                                    expr.get_operator_text() == "++", true,
                                    expr.get_source_span());
    }

    CoreIrValue *build_postfix_expr(const PostfixExpr &expr) {
        if (expr.get_operator_text() != "++" &&
            expr.get_operator_text() != "--") {
            add_error("core ir generation does not support postfix operator '" +
                          expr.get_operator_text() + "' yet",
                      expr.get_source_span());
            return nullptr;
        }

        return build_increment_expr(expr.get_operand(),
                                    expr.get_operator_text() == "++", false,
                                    expr.get_source_span());
    }

    std::optional<CoreIrBinaryOpcode>
    get_binary_opcode(const std::string &operator_text) const {
        if (operator_text == "+") {
            return CoreIrBinaryOpcode::Add;
        }
        if (operator_text == "-") {
            return CoreIrBinaryOpcode::Sub;
        }
        if (operator_text == "*") {
            return CoreIrBinaryOpcode::Mul;
        }
        if (operator_text == "&") {
            return CoreIrBinaryOpcode::And;
        }
        if (operator_text == "|") {
            return CoreIrBinaryOpcode::Or;
        }
        if (operator_text == "^") {
            return CoreIrBinaryOpcode::Xor;
        }
        if (operator_text == "<<") {
            return CoreIrBinaryOpcode::Shl;
        }
        return std::nullopt;
    }

    CoreIrBinaryOpcode get_right_shift_binary_opcode(
        const SemanticType *shifted_semantic_type) const {
        return is_unsigned_integer_semantic_type(shifted_semantic_type)
                   ? CoreIrBinaryOpcode::LShr
                   : CoreIrBinaryOpcode::AShr;
    }

    CoreIrBinaryOpcode get_division_binary_opcode(
        const std::string &operator_text,
        const SemanticType *compute_semantic_type) const {
        const bool is_unsigned =
            is_unsigned_integer_semantic_type(compute_semantic_type);
        if (operator_text == "/") {
            return is_unsigned ? CoreIrBinaryOpcode::UDiv
                               : CoreIrBinaryOpcode::SDiv;
        }
        return is_unsigned ? CoreIrBinaryOpcode::URem
                           : CoreIrBinaryOpcode::SRem;
    }

    bool is_supported_compound_assignment_operator(
        const std::string &operator_text) const {
        return operator_text == "+=" || operator_text == "-=" ||
               operator_text == "*=" || operator_text == "/=" ||
               operator_text == "%=" || operator_text == "<<=" ||
               operator_text == ">>=" || operator_text == "&=" ||
               operator_text == "^=" || operator_text == "|=";
    }

    std::string get_compound_assignment_binary_operator(
        const std::string &operator_text) const {
        if (operator_text == "+=") {
            return "+";
        }
        if (operator_text == "-=") {
            return "-";
        }
        if (operator_text == "*=") {
            return "*";
        }
        if (operator_text == "/=") {
            return "/";
        }
        if (operator_text == "%=") {
            return "%";
        }
        if (operator_text == "<<=") {
            return "<<";
        }
        if (operator_text == ">>=") {
            return ">>";
        }
        if (operator_text == "&=") {
            return "&";
        }
        if (operator_text == "^=") {
            return "^";
        }
        if (operator_text == "|=") {
            return "|";
        }
        return {};
    }

    const SemanticType *get_compound_assignment_compute_type(
        const AssignExpr &expr) const {
        const SemanticType *target_type = get_node_type(expr.get_target());
        const SemanticType *value_type = get_node_type(expr.get_value());
        if (target_type == nullptr || value_type == nullptr) {
            return nullptr;
        }

        const std::string binary_operator =
            get_compound_assignment_binary_operator(expr.get_operator_text());
        if (binary_operator.empty()) {
            return nullptr;
        }

        if (binary_operator == "<<" || binary_operator == ">>") {
            return target_type;
        }
        if (strip_qualifiers(target_type) != nullptr &&
            strip_qualifiers(target_type)->get_kind() == SemanticTypeKind::Pointer &&
            (binary_operator == "+" || binary_operator == "-")) {
            return target_type;
        }

        detail::IntegerConversionService integer_conversion_service;
        return integer_conversion_service.get_usual_arithmetic_conversion_type(
            target_type, value_type, semantic_model_);
    }

    CoreIrValue *build_logical_short_circuit_expr(const BinaryExpr &expr) {
        const bool is_logical_and = expr.get_operator_text() == "&&";
        if (!is_logical_and && expr.get_operator_text() != "||") {
            return nullptr;
        }

        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve logical result type",
                      expr.get_source_span());
            return nullptr;
        }

        const std::string logic_suffix = next_conditional_suffix();
        auto *zero_value = core_ir_context_->create_constant<CoreIrConstantInt>(
            result_type, 0);
        zero_value->set_source_span(expr.get_source_span());
        auto *one_value = core_ir_context_->create_constant<CoreIrConstantInt>(
            result_type, 1);
        one_value->set_source_span(expr.get_source_span());
        CoreIrValue *lhs = build_expr(expr.get_lhs());
        if (lhs == nullptr) {
            return nullptr;
        }

        CoreIrBasicBlock *rhs_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "logic.rhs" + logic_suffix);
        CoreIrBasicBlock *true_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "logic.true" + logic_suffix);
        CoreIrBasicBlock *false_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "logic.false" + logic_suffix);
        CoreIrBasicBlock *end_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "logic.end" + logic_suffix);

        auto *lhs_branch = current_block_->create_instruction<CoreIrCondJumpInst>(
            void_type_, lhs, is_logical_and ? rhs_block : true_block,
            is_logical_and ? false_block : rhs_block);
        lhs_branch->set_source_span(expr.get_lhs()->get_source_span());

        current_block_ = rhs_block;
        CoreIrValue *rhs = build_expr(expr.get_rhs());
        if (rhs == nullptr) {
            return nullptr;
        }
        auto *rhs_branch = current_block_->create_instruction<CoreIrCondJumpInst>(
            void_type_, rhs, true_block, false_block);
        rhs_branch->set_source_span(expr.get_rhs()->get_source_span());

        current_block_ = true_block;
        emit_jump_to(end_block, expr.get_source_span());

        current_block_ = false_block;
        emit_jump_to(end_block, expr.get_source_span());

        current_block_ = end_block;
        auto *phi = current_block_->create_instruction<CoreIrPhiInst>(
            result_type, next_temp_name());
        phi->add_incoming(true_block, one_value);
        phi->add_incoming(false_block, zero_value);
        phi->set_source_span(expr.get_source_span());
        return phi;
    }

    CoreIrValue *build_binary_expr(const BinaryExpr &expr) {
        if (expr.get_operator_text() == "&&" || expr.get_operator_text() == "||") {
            return build_logical_short_circuit_expr(expr);
        }
        if (expr.get_operator_text() == ",") {
            CoreIrValue *lhs = build_expr(expr.get_lhs());
            if (lhs == nullptr) {
                return nullptr;
            }
            return build_expr(expr.get_rhs());
        }
        if (const std::optional<CoreIrComparePredicate> compare_predicate =
                get_compare_predicate(expr.get_operator_text(), get_node_type(expr.get_lhs()));
            compare_predicate.has_value()) {
            return build_compare_expr(expr, *compare_predicate);
        }

        const bool is_division_like =
            expr.get_operator_text() == "/" || expr.get_operator_text() == "%";
        const bool is_right_shift = expr.get_operator_text() == ">>";
        const std::optional<CoreIrBinaryOpcode> binary_opcode =
            (is_right_shift || is_division_like)
                ? std::optional<CoreIrBinaryOpcode>(CoreIrBinaryOpcode::Add)
                : get_binary_opcode(expr.get_operator_text());
        if (!binary_opcode.has_value()) {
            add_error("core ir generation does not support binary operator '" +
                          expr.get_operator_text() + "' yet",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *lhs = build_expr(expr.get_lhs());
        if (lhs == nullptr) {
            return nullptr;
        }
        CoreIrValue *rhs = build_expr(expr.get_rhs());
        if (rhs == nullptr) {
            return nullptr;
        }

        const std::optional<int> lhs_bit_field_width =
            get_bit_field_width_for_expr(expr.get_lhs());
        const std::optional<int> rhs_bit_field_width =
            get_bit_field_width_for_expr(expr.get_rhs());
        const SemanticType *lhs_semantic_type = get_node_type(expr.get_lhs());
        const SemanticType *rhs_semantic_type = get_node_type(expr.get_rhs());
        const SemanticType *lhs_pointer_semantic_type =
            get_pointer_arithmetic_semantic_type(lhs_semantic_type);
        const SemanticType *rhs_pointer_semantic_type =
            get_pointer_arithmetic_semantic_type(rhs_semantic_type);

        if ((expr.get_operator_text() == "+" || expr.get_operator_text() == "-") &&
            lhs_pointer_semantic_type != nullptr &&
            is_integer_semantic_type(rhs_semantic_type)) {
            const SemanticType *index_type =
                get_integer_promotion_type(rhs_semantic_type,
                                           expr.get_rhs()->get_source_span());
            if (index_type == nullptr) {
                return nullptr;
            }
            rhs = build_converted_value(rhs, rhs_semantic_type, index_type,
                                        expr.get_rhs()->get_source_span());
            if (rhs == nullptr) {
                return nullptr;
            }
            if (expr.get_operator_text() == "-") {
                CoreIrValue *zero =
                    build_converted_value(create_i32_constant(0, expr.get_source_span()),
                                          get_static_builtin_semantic_type("int"),
                                          index_type, expr.get_source_span());
                if (zero == nullptr) {
                    return nullptr;
                }
                auto *negated_offset =
                    current_block_->create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Sub, rhs->get_type(), next_temp_name(),
                        zero, rhs);
                negated_offset->set_source_span(expr.get_source_span());
                rhs = negated_offset;
            }
            return build_pointer_step(lhs, lhs_pointer_semantic_type, rhs,
                                      expr.get_source_span());
        }
        if (expr.get_operator_text() == "+" &&
            is_integer_semantic_type(lhs_semantic_type) &&
            rhs_pointer_semantic_type != nullptr) {
            const SemanticType *index_type =
                get_integer_promotion_type(lhs_semantic_type,
                                           expr.get_lhs()->get_source_span());
            if (index_type == nullptr) {
                return nullptr;
            }
            lhs = build_converted_value(lhs, lhs_semantic_type, index_type,
                                        expr.get_lhs()->get_source_span());
            if (lhs == nullptr) {
                return nullptr;
            }
            return build_pointer_step(rhs, rhs_pointer_semantic_type, lhs,
                                      expr.get_source_span());
        }
        if (expr.get_operator_text() == "-" &&
            lhs_pointer_semantic_type != nullptr &&
            rhs_pointer_semantic_type != nullptr) {
            const SemanticType *result_semantic_type = get_node_type(&expr);
            const CoreIrType *result_type = get_or_create_type(result_semantic_type);
            if (result_type == nullptr || result_type->get_kind() != CoreIrTypeKind::Integer) {
                add_error("core ir generation could not resolve binary result type",
                          expr.get_source_span());
                return nullptr;
            }
            CoreIrValue *lhs_int = build_converted_value(
                lhs, lhs_pointer_semantic_type, result_semantic_type,
                expr.get_lhs()->get_source_span());
            CoreIrValue *rhs_int = build_converted_value(
                rhs, rhs_pointer_semantic_type, result_semantic_type,
                expr.get_rhs()->get_source_span());
            if (lhs_int == nullptr || rhs_int == nullptr) {
                return nullptr;
            }
            auto *byte_difference =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Sub, result_type, next_temp_name(),
                    lhs_int, rhs_int);
            byte_difference->set_source_span(expr.get_source_span());

            const auto *lhs_pointer_type = static_cast<const PointerSemanticType *>(
                strip_qualifiers(lhs_pointer_semantic_type));
            const std::size_t element_size = detail::get_type_size(
                lhs_pointer_type->get_pointee_type());
            if (element_size <= 1) {
                return byte_difference;
            }
            auto *element_size_value =
                build_integer_constant(result_type, element_size,
                                       expr.get_source_span());
            auto *element_difference =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::SDiv, result_type, next_temp_name(),
                    byte_difference, element_size_value);
            element_difference->set_source_span(expr.get_source_span());
            return element_difference;
        }

        const SemanticType *compute_semantic_type = nullptr;
        if (is_shift_operator(expr.get_operator_text())) {
            const SemanticType *promoted_lhs_type =
                get_integer_promotion_type(lhs_semantic_type,
                                           expr.get_lhs()->get_source_span());
            const SemanticType *promoted_rhs_type =
                get_integer_promotion_type(rhs_semantic_type,
                                           expr.get_rhs()->get_source_span());
            if (promoted_lhs_type == nullptr || promoted_rhs_type == nullptr) {
                return nullptr;
            }
            lhs = build_converted_value(lhs, lhs_semantic_type, promoted_lhs_type,
                                        expr.get_lhs()->get_source_span());
            rhs = build_converted_value(rhs, rhs_semantic_type, promoted_rhs_type,
                                        expr.get_rhs()->get_source_span());
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }
            rhs = build_converted_value(rhs, promoted_rhs_type, promoted_lhs_type,
                                        expr.get_rhs()->get_source_span());
            if (rhs == nullptr) {
                return nullptr;
            }
            compute_semantic_type = promoted_lhs_type;
        } else if (!is_pointer_semantic_type(lhs_semantic_type) &&
                   !is_pointer_semantic_type(rhs_semantic_type)) {
            compute_semantic_type = get_usual_arithmetic_conversion_type(
                lhs_semantic_type, lhs_bit_field_width, rhs_semantic_type,
                rhs_bit_field_width, expr.get_source_span());
            if (compute_semantic_type == nullptr) {
                return nullptr;
            }
            lhs = build_converted_value(lhs, lhs_semantic_type, compute_semantic_type,
                                        expr.get_lhs()->get_source_span());
            rhs = build_converted_value(rhs, rhs_semantic_type, compute_semantic_type,
                                        expr.get_rhs()->get_source_span());
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }
        }

        const CoreIrType *result_type = nullptr;
        if (compute_semantic_type != nullptr &&
            !is_pointer_semantic_type(lhs_semantic_type) &&
            !is_pointer_semantic_type(rhs_semantic_type)) {
            result_type = get_or_create_type(compute_semantic_type);
        } else {
            result_type = get_or_create_type(get_node_type(&expr));
        }
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve binary result type",
                      expr.get_source_span());
            return nullptr;
        }

        const CoreIrBinaryOpcode resolved_binary_opcode =
            is_right_shift
                ? get_right_shift_binary_opcode(compute_semantic_type)
                : (is_division_like
                       ? get_division_binary_opcode(expr.get_operator_text(),
                                                    compute_semantic_type)
                       : *binary_opcode);
        auto *instruction = current_block_->create_instruction<CoreIrBinaryInst>(
            resolved_binary_opcode, result_type, next_temp_name(), lhs, rhs);
        instruction->set_source_span(expr.get_source_span());
        return instruction;
    }

    CoreIrValue *build_conditional_expr(const ConditionalExpr &expr) {
        if (current_function_ == nullptr || current_block_ == nullptr) {
            add_error("core ir generation reached a conditional expression outside "
                      "of an active function block",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *condition = build_expr(expr.get_condition());
        if (condition == nullptr) {
            return nullptr;
        }

        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve conditional result type",
                      expr.get_source_span());
            return nullptr;
        }

        const std::string conditional_suffix = next_conditional_suffix();
        CoreIrBasicBlock *true_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "cond.true" + conditional_suffix);
        CoreIrBasicBlock *false_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "cond.false" + conditional_suffix);
        CoreIrBasicBlock *end_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "cond.end" + conditional_suffix);

        auto *branch = current_block_->create_instruction<CoreIrCondJumpInst>(
            void_type_, condition, true_block, false_block);
        branch->set_source_span(expr.get_source_span());

        const bool can_use_phi =
            result_type != void_type_ &&
            is_scalar_semantic_type(get_node_type(&expr));
        CoreIrStackSlot *result_slot = nullptr;
        if (!can_use_phi && result_type != void_type_) {
            const std::string slot_name =
                conditional_suffix == "0" ? "addr.addr"
                                           : "addr.addr" + conditional_suffix;
            result_slot = current_function_->create_stack_slot<CoreIrStackSlot>(
                next_stack_slot_name(slot_name), result_type,
                get_default_alignment(result_type));
        }

        current_block_ = true_block;
        CoreIrValue *true_value = build_expr(expr.get_true_expr());
        if (true_value == nullptr) {
            return nullptr;
        }
        if (can_use_phi) {
            true_value = build_converted_value(true_value,
                                               get_node_type(expr.get_true_expr()),
                                               get_node_type(&expr),
                                               expr.get_true_expr()->get_source_span());
            if (true_value == nullptr) {
                return nullptr;
            }
        } else if (result_slot != nullptr) {
            true_value = build_converted_value(true_value,
                                               get_node_type(expr.get_true_expr()),
                                               get_node_type(&expr),
                                               expr.get_true_expr()->get_source_span());
            if (true_value == nullptr) {
                return nullptr;
            }
            auto *true_store = current_block_->create_instruction<CoreIrStoreInst>(
                void_type_, true_value, result_slot);
            true_store->set_source_span(expr.get_true_expr()->get_source_span());
        }
        CoreIrBasicBlock *true_incoming_block = current_block_;
        emit_jump_to(end_block, expr.get_true_expr()->get_source_span());

        current_block_ = false_block;
        CoreIrValue *false_value = build_expr(expr.get_false_expr());
        if (false_value == nullptr) {
            return nullptr;
        }
        if (can_use_phi) {
            false_value = build_converted_value(false_value,
                                                get_node_type(expr.get_false_expr()),
                                                get_node_type(&expr),
                                                expr.get_false_expr()->get_source_span());
            if (false_value == nullptr) {
                return nullptr;
            }
        } else if (result_slot != nullptr) {
            false_value = build_converted_value(
                false_value, get_node_type(expr.get_false_expr()),
                get_node_type(&expr), expr.get_false_expr()->get_source_span());
            if (false_value == nullptr) {
                return nullptr;
            }
            auto *false_store =
                current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, false_value, result_slot);
            false_store->set_source_span(expr.get_false_expr()->get_source_span());
        }
        CoreIrBasicBlock *false_incoming_block = current_block_;
        emit_jump_to(end_block, expr.get_false_expr()->get_source_span());

        current_block_ = end_block;
        if (!can_use_phi) {
            if (result_slot == nullptr) {
                return condition;
            }
            auto *load = current_block_->create_instruction<CoreIrLoadInst>(
                result_type, next_temp_name(), result_slot);
            load->set_source_span(expr.get_source_span());
            return load;
        }
        auto *phi = current_block_->create_instruction<CoreIrPhiInst>(
            result_type, next_temp_name());
        phi->add_incoming(true_incoming_block, true_value);
        phi->add_incoming(false_incoming_block, false_value);
        phi->set_source_span(expr.get_source_span());
        return phi;
    }

    std::optional<CoreIrComparePredicate>
    get_compare_predicate(const std::string &operator_text,
                          const SemanticType *operand_type) const {
        const bool is_unsigned =
            operand_type != nullptr &&
            (is_builtin_type_named(operand_type, "unsigned char") ||
             is_builtin_type_named(operand_type, "unsigned short") ||
             is_builtin_type_named(operand_type, "unsigned int") ||
             is_builtin_type_named(operand_type, "unsigned long") ||
             is_builtin_type_named(operand_type, "unsigned long long"));
        if (operator_text == "==") {
            return CoreIrComparePredicate::Equal;
        }
        if (operator_text == "!=") {
            return CoreIrComparePredicate::NotEqual;
        }
        if (operator_text == "<") {
            return is_unsigned ? CoreIrComparePredicate::UnsignedLess
                               : CoreIrComparePredicate::SignedLess;
        }
        if (operator_text == "<=") {
            return is_unsigned ? CoreIrComparePredicate::UnsignedLessEqual
                               : CoreIrComparePredicate::SignedLessEqual;
        }
        if (operator_text == ">") {
            return is_unsigned ? CoreIrComparePredicate::UnsignedGreater
                               : CoreIrComparePredicate::SignedGreater;
        }
        if (operator_text == ">=") {
            return is_unsigned ? CoreIrComparePredicate::UnsignedGreaterEqual
                               : CoreIrComparePredicate::SignedGreaterEqual;
        }
        return std::nullopt;
    }

    CoreIrValue *build_compare_expr(const BinaryExpr &expr,
                                    CoreIrComparePredicate predicate) {
        const SemanticType *lhs_semantic_type = get_node_type(expr.get_lhs());
        const SemanticType *rhs_semantic_type = get_node_type(expr.get_rhs());
        const std::optional<int> lhs_bit_field_width =
            get_bit_field_width_for_expr(expr.get_lhs());
        const std::optional<int> rhs_bit_field_width =
            get_bit_field_width_for_expr(expr.get_rhs());
        const SemanticType *compare_semantic_type = nullptr;
        if (!is_pointer_semantic_type(lhs_semantic_type) &&
            !is_pointer_semantic_type(rhs_semantic_type)) {
            compare_semantic_type = get_usual_arithmetic_conversion_type(
                lhs_semantic_type, lhs_bit_field_width, rhs_semantic_type,
                rhs_bit_field_width, expr.get_source_span());
            if (compare_semantic_type == nullptr) {
                return nullptr;
            }
            const std::optional<CoreIrComparePredicate> adjusted_predicate =
                get_compare_predicate(expr.get_operator_text(),
                                      compare_semantic_type);
            if (!adjusted_predicate.has_value()) {
                add_error("core ir generation could not resolve comparison "
                          "predicate",
                          expr.get_source_span());
                return nullptr;
            }
            predicate = *adjusted_predicate;
        }

        CoreIrValue *lhs = build_expr(expr.get_lhs());
        if (lhs == nullptr) {
            return nullptr;
        }
        CoreIrValue *rhs = build_expr(expr.get_rhs());
        if (rhs == nullptr) {
            return nullptr;
        }
        if (compare_semantic_type == nullptr) {
            const std::optional<long long> lhs_constant =
                get_integer_constant_value(expr.get_lhs());
            const std::optional<long long> rhs_constant =
                get_integer_constant_value(expr.get_rhs());
            if (is_pointer_semantic_type(lhs_semantic_type) &&
                is_integer_semantic_type(rhs_semantic_type) &&
                rhs_constant.has_value() && *rhs_constant == 0) {
                rhs = build_converted_value(rhs, rhs_semantic_type,
                                            lhs_semantic_type,
                                            expr.get_rhs()->get_source_span());
                if (rhs == nullptr) {
                    return nullptr;
                }
            } else if (is_pointer_semantic_type(rhs_semantic_type) &&
                       is_integer_semantic_type(lhs_semantic_type) &&
                       lhs_constant.has_value() && *lhs_constant == 0) {
                lhs = build_converted_value(lhs, lhs_semantic_type,
                                            rhs_semantic_type,
                                            expr.get_lhs()->get_source_span());
                if (lhs == nullptr) {
                    return nullptr;
                }
            }
        }
        if (compare_semantic_type != nullptr) {
            lhs = build_converted_value(lhs, lhs_semantic_type,
                                        compare_semantic_type,
                                        expr.get_lhs()->get_source_span());
            rhs = build_converted_value(rhs, rhs_semantic_type,
                                        compare_semantic_type,
                                        expr.get_rhs()->get_source_span());
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }
        }
        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve comparison result type",
                      expr.get_source_span());
            return nullptr;
        }
        auto *instruction = current_block_->create_instruction<CoreIrCompareInst>(
            predicate, result_type, next_temp_name(), lhs, rhs);
        instruction->set_source_span(expr.get_source_span());
        return instruction;
    }

    CoreIrValue *build_assign_expr(const AssignExpr &expr) {
        const std::optional<BitFieldLValue> bit_field_lvalue =
            get_bit_field_lvalue(expr.get_target());
        if (bit_field_lvalue.has_value()) {
            if (expr.get_operator_text() == "=") {
                CoreIrValue *value = build_expr(expr.get_value());
                if (value == nullptr) {
                    return nullptr;
                }
                return store_bit_field_value(*bit_field_lvalue, value,
                                             get_node_type(expr.get_value()),
                                             expr.get_source_span());
            }
            if (!is_supported_compound_assignment_operator(
                    expr.get_operator_text())) {
                add_error("core ir generation does not support assignment operator '" +
                              expr.get_operator_text() + "' yet",
                          expr.get_source_span());
                return nullptr;
            }

            CoreIrValue *current_value =
                build_bit_field_value(*bit_field_lvalue, expr.get_source_span());
            if (current_value == nullptr) {
                return nullptr;
            }

            CoreIrValue *rhs_value = build_expr(expr.get_value());
            if (rhs_value == nullptr) {
                return nullptr;
            }

            const std::string binary_operator =
                get_compound_assignment_binary_operator(expr.get_operator_text());
            const SemanticType *compute_semantic_type =
                get_compound_assignment_compute_type(expr);
            if (compute_semantic_type == nullptr) {
                add_error("core ir generation could not resolve compound "
                          "assignment compute type",
                          expr.get_source_span());
                return nullptr;
            }

            CoreIrValue *lhs_value = build_converted_value(
                current_value, get_node_type(expr.get_target()),
                compute_semantic_type, expr.get_target()->get_source_span());
            rhs_value = build_converted_value(rhs_value, get_node_type(expr.get_value()),
                                              compute_semantic_type,
                                              expr.get_value()->get_source_span());
            if (lhs_value == nullptr || rhs_value == nullptr) {
                return nullptr;
            }

            const bool is_division_like =
                binary_operator == "/" || binary_operator == "%";
            const bool is_right_shift = binary_operator == ">>";
            const std::optional<CoreIrBinaryOpcode> binary_opcode =
                (is_right_shift || is_division_like)
                    ? std::optional<CoreIrBinaryOpcode>(CoreIrBinaryOpcode::Add)
                    : get_binary_opcode(binary_operator);
            if (!binary_opcode.has_value()) {
                add_error("core ir generation could not resolve compound "
                          "assignment operator",
                          expr.get_source_span());
                return nullptr;
            }

            const CoreIrType *compute_type =
                get_or_create_type(compute_semantic_type);
            if (compute_type == nullptr) {
                add_error("core ir generation could not resolve compound "
                          "assignment result type",
                          expr.get_source_span());
                return nullptr;
            }
            const CoreIrBinaryOpcode resolved_binary_opcode =
                is_right_shift
                    ? get_right_shift_binary_opcode(compute_semantic_type)
                    : (is_division_like
                           ? get_division_binary_opcode(binary_operator,
                                                        compute_semantic_type)
                           : *binary_opcode);
            auto *instruction =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    resolved_binary_opcode, compute_type, next_temp_name(),
                    lhs_value, rhs_value);
            instruction->set_source_span(expr.get_source_span());
            return store_bit_field_value(*bit_field_lvalue, instruction,
                                         compute_semantic_type,
                                         expr.get_source_span());
        }

        CoreIrValue *address = build_lvalue_address(expr.get_target());
        if (address == nullptr) {
            return nullptr;
        }

        if (expr.get_operator_text() == "=") {
            if (expr.get_value() != nullptr &&
                expr.get_value()->get_kind() == AstKind::InitListExpr) {
                if (!emit_local_initializer_to_address(
                        address, get_node_type(expr.get_target()),
                        expr.get_value(), expr.get_value()->get_source_span())) {
                    return nullptr;
                }
                return address;
            }
            CoreIrValue *value = build_expr(expr.get_value());
            if (value == nullptr) {
                return nullptr;
            }
            value = build_converted_value(value, get_node_type(expr.get_value()),
                                          get_node_type(expr.get_target()),
                                          expr.get_value()->get_source_span());
            if (value == nullptr) {
                return nullptr;
            }
            auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                void_type_, value, address);
            store->set_source_span(expr.get_source_span());
            return value;
        }

        if (!is_supported_compound_assignment_operator(expr.get_operator_text())) {
            add_error("core ir generation does not support assignment operator '" +
                          expr.get_operator_text() + "' yet",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *current_value =
            build_value_from_lvalue(*expr.get_target(), address);
        if (current_value == nullptr) {
            return nullptr;
        }

        CoreIrValue *rhs_value = build_expr(expr.get_value());
        if (rhs_value == nullptr) {
            return nullptr;
        }

        const std::string binary_operator =
            get_compound_assignment_binary_operator(expr.get_operator_text());
        const SemanticType *compute_semantic_type =
            get_compound_assignment_compute_type(expr);
        if (compute_semantic_type == nullptr) {
            add_error("core ir generation could not resolve compound assignment "
                      "compute type",
                      expr.get_source_span());
            return nullptr;
        }

        CoreIrValue *updated_value = nullptr;
        const SemanticType *target_semantic_type = get_node_type(expr.get_target());
        if (strip_qualifiers(target_semantic_type) != nullptr &&
            strip_qualifiers(target_semantic_type)->get_kind() ==
                SemanticTypeKind::Pointer &&
            (binary_operator == "+" || binary_operator == "-")) {
            const SemanticType *index_type =
                get_integer_promotion_type(get_node_type(expr.get_value()),
                                           expr.get_value()->get_source_span());
            if (index_type == nullptr) {
                return nullptr;
            }
            rhs_value = build_converted_value(rhs_value, get_node_type(expr.get_value()),
                                              index_type,
                                              expr.get_value()->get_source_span());
            if (rhs_value == nullptr) {
                return nullptr;
            }
            if (binary_operator == "-") {
                CoreIrValue *zero = build_converted_value(
                    create_i32_constant(0, expr.get_source_span()),
                    get_static_builtin_semantic_type("int"), index_type,
                    expr.get_source_span());
                if (zero == nullptr) {
                    return nullptr;
                }
                auto *negated_offset =
                    current_block_->create_instruction<CoreIrBinaryInst>(
                        CoreIrBinaryOpcode::Sub, rhs_value->get_type(),
                        next_temp_name(), zero, rhs_value);
                negated_offset->set_source_span(expr.get_source_span());
                rhs_value = negated_offset;
            }
            updated_value = build_pointer_step(current_value, target_semantic_type,
                                               rhs_value, expr.get_source_span());
        } else {
            CoreIrValue *lhs_value = build_converted_value(
                current_value, target_semantic_type, compute_semantic_type,
                expr.get_target()->get_source_span());
            if (lhs_value == nullptr) {
                return nullptr;
            }
            rhs_value = build_converted_value(rhs_value, get_node_type(expr.get_value()),
                                              compute_semantic_type,
                                              expr.get_value()->get_source_span());
            if (rhs_value == nullptr) {
                return nullptr;
            }

            const bool is_division_like =
                binary_operator == "/" || binary_operator == "%";
            const bool is_right_shift = binary_operator == ">>";
            const std::optional<CoreIrBinaryOpcode> binary_opcode =
                (is_right_shift || is_division_like)
                    ? std::optional<CoreIrBinaryOpcode>(CoreIrBinaryOpcode::Add)
                    : get_binary_opcode(binary_operator);
            if (!binary_opcode.has_value()) {
                add_error("core ir generation could not resolve compound "
                          "assignment operator",
                          expr.get_source_span());
                return nullptr;
            }

            const CoreIrType *compute_type =
                get_or_create_type(compute_semantic_type);
            if (compute_type == nullptr) {
                add_error("core ir generation could not resolve compound "
                          "assignment result type",
                          expr.get_source_span());
                return nullptr;
            }
            const CoreIrBinaryOpcode resolved_binary_opcode =
                is_right_shift
                    ? get_right_shift_binary_opcode(compute_semantic_type)
                    : (is_division_like
                           ? get_division_binary_opcode(binary_operator,
                                                        compute_semantic_type)
                           : *binary_opcode);
            auto *instruction =
                current_block_->create_instruction<CoreIrBinaryInst>(
                    resolved_binary_opcode, compute_type, next_temp_name(),
                    lhs_value, rhs_value);
            instruction->set_source_span(expr.get_source_span());
            updated_value = instruction;
        }
        if (updated_value == nullptr) {
            return nullptr;
        }

        updated_value = build_converted_value(
            updated_value, compute_semantic_type, target_semantic_type,
            expr.get_source_span());
        if (updated_value == nullptr) {
            return nullptr;
        }

        auto *store = current_block_->create_instruction<CoreIrStoreInst>(
            void_type_, updated_value, address);
        store->set_source_span(expr.get_source_span());
        return updated_value;
    }

    CoreIrValue *build_call_expr(const CallExpr &expr) {
        if (const auto *callee_identifier =
                dynamic_cast<const IdentifierExpr *>(expr.get_callee());
            callee_identifier != nullptr &&
            callee_identifier->get_name() == "__builtin_alloca") {
            if (expr.get_arguments().size() != 1) {
                add_error("core ir generation found malformed alloca builtin",
                          expr.get_source_span());
                return nullptr;
            }
            const FunctionSemanticType *callee_semantic_type =
                get_function_semantic_type(get_node_type(expr.get_callee()));
            if (callee_semantic_type == nullptr ||
                callee_semantic_type->get_parameter_types().empty()) {
                add_error(
                    "core ir generation could not resolve alloca builtin type",
                    expr.get_source_span());
                return nullptr;
            }
            CoreIrValue *size_value = build_expr(expr.get_arguments().front().get());
            if (size_value == nullptr) {
                return nullptr;
            }
            size_value = build_converted_value(
                size_value, get_node_type(expr.get_arguments().front().get()),
                callee_semantic_type->get_parameter_types().front(),
                expr.get_arguments().front()->get_source_span());
            if (size_value == nullptr) {
                return nullptr;
            }
            const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
            if (result_type == nullptr ||
                result_type->get_kind() != CoreIrTypeKind::Pointer) {
                add_error("core ir generation could not resolve alloca result type",
                          expr.get_source_span());
                return nullptr;
            }
            auto *alloca = current_block_->create_instruction<CoreIrDynamicAllocaInst>(
                result_type, next_temp_name(), size_value);
            alloca->set_source_span(expr.get_source_span());
            return alloca;
        }

        if (const auto *callee_identifier =
                dynamic_cast<const IdentifierExpr *>(expr.get_callee());
            callee_identifier != nullptr &&
            (callee_identifier->get_name() == "__builtin_bswap16" ||
             callee_identifier->get_name() == "__builtin_bswap32" ||
             callee_identifier->get_name() == "__builtin_bswap64")) {
            if (expr.get_arguments().size() != 1) {
                add_error("core ir generation found malformed bswap builtin",
                          expr.get_source_span());
                return nullptr;
            }
            const FunctionSemanticType *callee_semantic_type =
                get_function_semantic_type(get_node_type(expr.get_callee()));
            if (callee_semantic_type == nullptr ||
                callee_semantic_type->get_parameter_types().empty()) {
                add_error("core ir generation could not resolve bswap builtin type",
                          expr.get_source_span());
                return nullptr;
            }
            CoreIrValue *argument_value =
                build_expr(expr.get_arguments().front().get());
            if (argument_value == nullptr) {
                return nullptr;
            }
            argument_value = build_converted_value(
                argument_value, get_node_type(expr.get_arguments().front().get()),
                callee_semantic_type->get_parameter_types().front(),
                expr.get_arguments().front()->get_source_span());
            if (argument_value == nullptr) {
                return nullptr;
            }
            const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
            if (result_type == nullptr) {
                add_error("core ir generation could not resolve bswap result type",
                          expr.get_source_span());
                return nullptr;
            }
            const char *intrinsic_name =
                callee_identifier->get_name() == "__builtin_bswap16"
                    ? "llvm.bswap.i16"
                    : (callee_identifier->get_name() == "__builtin_bswap32"
                           ? "llvm.bswap.i32"
                           : "llvm.bswap.i64");
            auto *call = current_block_->create_instruction<CoreIrCallInst>(
                result_type, next_temp_name(), intrinsic_name,
                core_ir_context_->create_type<CoreIrFunctionType>(
                    result_type, std::vector<const CoreIrType *>{argument_value->get_type()},
                    false),
                std::vector<CoreIrValue *>{argument_value});
            call->set_source_span(expr.get_source_span());
            return call;
        }

        if (const auto *callee_identifier =
                dynamic_cast<const IdentifierExpr *>(expr.get_callee());
            callee_identifier != nullptr &&
            (callee_identifier->get_name() == "__builtin_clzll" ||
             callee_identifier->get_name() == "__builtin_ctzll")) {
            if (expr.get_arguments().size() != 1) {
                add_error("core ir generation found malformed bit-scan builtin",
                          expr.get_source_span());
                return nullptr;
            }
            const FunctionSemanticType *callee_semantic_type =
                get_function_semantic_type(get_node_type(expr.get_callee()));
            if (callee_semantic_type == nullptr ||
                callee_semantic_type->get_parameter_types().empty()) {
                add_error("core ir generation could not resolve bit-scan builtin type",
                          expr.get_source_span());
                return nullptr;
            }
            CoreIrValue *argument_value =
                build_expr(expr.get_arguments().front().get());
            if (argument_value == nullptr) {
                return nullptr;
            }
            argument_value = build_converted_value(
                argument_value, get_node_type(expr.get_arguments().front().get()),
                callee_semantic_type->get_parameter_types().front(),
                expr.get_arguments().front()->get_source_span());
            if (argument_value == nullptr) {
                return nullptr;
            }

            const auto *i1_type =
                core_ir_context_->create_type<CoreIrIntegerType>(1, false);
            auto *is_zero_undef =
                core_ir_context_->create_constant<CoreIrConstantInt>(i1_type, 0);
            is_zero_undef->set_source_span(expr.get_source_span());

            const char *intrinsic_name =
                callee_identifier->get_name() == "__builtin_clzll"
                    ? "llvm.ctlz.i64"
                    : "llvm.cttz.i64";
            auto *bitscan_call = current_block_->create_instruction<CoreIrCallInst>(
                argument_value->get_type(), next_temp_name(), intrinsic_name,
                core_ir_context_->create_type<CoreIrFunctionType>(
                    argument_value->get_type(),
                    std::vector<const CoreIrType *>{argument_value->get_type(),
                                                    i1_type},
                    false),
                std::vector<CoreIrValue *>{argument_value, is_zero_undef});
            bitscan_call->set_source_span(expr.get_source_span());

            return build_converted_value(
                bitscan_call, callee_semantic_type->get_parameter_types().front(),
                get_node_type(&expr), expr.get_source_span());
        }

        if (const auto *callee_identifier =
                dynamic_cast<const IdentifierExpr *>(expr.get_callee());
            callee_identifier != nullptr &&
            (callee_identifier->get_name() == "__builtin_va_start" ||
             callee_identifier->get_name() == "__builtin_va_end" ||
             callee_identifier->get_name() == "__builtin_va_copy")) {
            std::vector<CoreIrValue *> arguments;
            const std::size_t required_argument_count =
                callee_identifier->get_name() == "__builtin_va_copy" ? 2 : 1;
            if (expr.get_arguments().size() < required_argument_count) {
                add_error("core ir generation found malformed va_list builtin",
                          expr.get_source_span());
                return nullptr;
            }
            arguments.reserve(required_argument_count);
            for (std::size_t index = 0; index < required_argument_count; ++index) {
                CoreIrValue *argument_value =
                    build_expr(expr.get_arguments()[index].get());
                if (argument_value == nullptr) {
                    return nullptr;
                }
                arguments.push_back(argument_value);
            }
            const std::string intrinsic_name =
                callee_identifier->get_name() == "__builtin_va_start"
                    ? "llvm.va_start"
                    : (callee_identifier->get_name() == "__builtin_va_end"
                           ? "llvm.va_end"
                           : "llvm.va_copy");
            std::vector<const CoreIrType *> parameter_types;
            parameter_types.reserve(arguments.size());
            for (CoreIrValue *argument : arguments) {
                parameter_types.push_back(argument->get_type());
            }
            const auto *callee_type =
                core_ir_context_->create_type<CoreIrFunctionType>(
                    void_type_, std::move(parameter_types), false);
            auto *call = current_block_->create_instruction<CoreIrCallInst>(
                void_type_, std::string(), intrinsic_name, callee_type,
                std::move(arguments));
            call->set_source_span(expr.get_source_span());
            return call;
        }

        const FunctionSemanticType *callee_semantic_type =
            get_function_semantic_type(get_node_type(expr.get_callee()));
        if (callee_semantic_type == nullptr) {
            add_error("core ir generation could not resolve call target type",
                      expr.get_source_span());
            return nullptr;
        }
        const auto *callee_function_type =
            dynamic_cast<const CoreIrFunctionType *>(
                get_or_create_type(callee_semantic_type));
        if (callee_function_type == nullptr) {
            add_error("core ir generation call target is not a function",
                      expr.get_source_span());
            return nullptr;
        }

        std::vector<CoreIrValue *> arguments;
        arguments.reserve(expr.get_arguments().size());
        const auto &parameter_semantic_types =
            callee_semantic_type->get_parameter_types();
        for (std::size_t argument_index = 0; argument_index < expr.get_arguments().size();
             ++argument_index) {
            const auto &argument = expr.get_arguments()[argument_index];
            CoreIrValue *argument_value = build_expr(argument.get());
            if (argument_value == nullptr) {
                return nullptr;
            }
            if (argument_index < parameter_semantic_types.size()) {
                const SemanticType *parameter_semantic_type =
                    adjust_function_parameter_semantic_type(
                        parameter_semantic_types[argument_index]);
                argument_value = build_converted_value(
                    argument_value, get_node_type(argument.get()),
                    parameter_semantic_type,
                    argument->get_source_span());
                if (argument_value == nullptr) {
                    return nullptr;
                }
            } else if (callee_semantic_type->get_is_variadic()) {
                argument_value = apply_default_argument_promotion(
                    argument_value, get_node_type(argument.get()),
                    argument->get_source_span());
                if (argument_value == nullptr) {
                    return nullptr;
                }
            }
            arguments.push_back(argument_value);
        }

        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve call result type",
                      expr.get_source_span());
            return nullptr;
        }
        std::string result_name;
        if (result_type != void_type_) {
            result_name = next_temp_name();
        }
        if (const auto *callee_identifier =
                dynamic_cast<const IdentifierExpr *>(expr.get_callee());
            callee_identifier != nullptr) {
            const SemanticSymbol *callee_symbol =
                get_symbol_binding(callee_identifier);
            if (callee_symbol != nullptr &&
                strip_qualifiers(callee_symbol->get_type()) != nullptr &&
                strip_qualifiers(callee_symbol->get_type())->get_kind() ==
                    SemanticTypeKind::Function) {
                CoreIrFunction *callee_function =
                    ensure_function_binding(callee_symbol, expr.get_source_span());
                if (callee_function == nullptr) {
                    return nullptr;
                }
                auto *call =
                    current_block_->create_instruction<CoreIrCallInst>(
                        result_type, std::move(result_name),
                        callee_function->get_name(), callee_function_type,
                        std::move(arguments));
                call->set_source_span(expr.get_source_span());
                return call;
            }
        }

        CoreIrValue *callee_value = build_expr(expr.get_callee());
        if (callee_value == nullptr) {
            return nullptr;
        }
        auto *call = current_block_->create_instruction<CoreIrCallInst>(
            result_type, std::move(result_name), callee_value,
            callee_function_type, std::move(arguments));
        call->set_source_span(expr.get_source_span());
        return call;
    }

    CoreIrValue *build_builtin_va_arg_expr(const BuiltinVaArgExpr &expr) {
        CoreIrValue *va_list_value = build_expr(expr.get_va_list_expr());
        if (va_list_value == nullptr) {
            return nullptr;
        }
        const CoreIrType *result_type = get_or_create_type(get_node_type(&expr));
        if (result_type == nullptr) {
            add_error("core ir generation could not resolve va_arg result type",
                      expr.get_source_span());
            return nullptr;
        }
        auto *call = current_block_->create_instruction<CoreIrCallInst>(
            result_type, next_temp_name(), "__sysycc.va_arg",
            core_ir_context_->create_type<CoreIrFunctionType>(
                result_type, std::vector<const CoreIrType *>{va_list_value->get_type()},
                false),
            std::vector<CoreIrValue *>{va_list_value});
        call->set_source_span(expr.get_source_span());
        return call;
    }

    bool emit_stmt(const Stmt *stmt) {
        if (stmt == nullptr) {
            return true;
        }

        if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
            block_stmt != nullptr) {
            return emit_block_stmt(*block_stmt);
        }
        if (const auto *decl_stmt = dynamic_cast<const DeclStmt *>(stmt);
            decl_stmt != nullptr) {
            return emit_decl_stmt(*decl_stmt);
        }
        if (const auto *while_stmt = dynamic_cast<const WhileStmt *>(stmt);
            while_stmt != nullptr) {
            return emit_while_stmt(*while_stmt);
        }
        if (const auto *do_while_stmt = dynamic_cast<const DoWhileStmt *>(stmt);
            do_while_stmt != nullptr) {
            return emit_do_while_stmt(*do_while_stmt);
        }
        if (const auto *for_stmt = dynamic_cast<const ForStmt *>(stmt);
            for_stmt != nullptr) {
            return emit_for_stmt(*for_stmt);
        }
        if (const auto *switch_stmt = dynamic_cast<const SwitchStmt *>(stmt);
            switch_stmt != nullptr) {
            return emit_switch_stmt(*switch_stmt);
        }
        if (const auto *return_stmt = dynamic_cast<const ReturnStmt *>(stmt);
            return_stmt != nullptr) {
            return emit_return_stmt(*return_stmt);
        }
        if (const auto *if_stmt = dynamic_cast<const IfStmt *>(stmt);
            if_stmt != nullptr) {
            return emit_if_stmt(*if_stmt);
        }
        if (const auto *expr_stmt = dynamic_cast<const ExprStmt *>(stmt);
            expr_stmt != nullptr) {
            return emit_expr_stmt(*expr_stmt);
        }
        if (const auto *label_stmt = dynamic_cast<const LabelStmt *>(stmt);
            label_stmt != nullptr) {
            return emit_label_stmt(*label_stmt);
        }
        if (dynamic_cast<const BreakStmt *>(stmt) != nullptr) {
            return emit_break_stmt(stmt->get_source_span());
        }
        if (dynamic_cast<const ContinueStmt *>(stmt) != nullptr) {
            return emit_continue_stmt(stmt->get_source_span());
        }
        if (const auto *goto_stmt = dynamic_cast<const GotoStmt *>(stmt);
            goto_stmt != nullptr) {
            return emit_goto_stmt(*goto_stmt);
        }

        add_error("core ir generation does not support this statement yet",
                  stmt->get_source_span());
        return false;
    }

    bool emit_block_stmt(const BlockStmt &block_stmt) {
        for (const auto &statement : block_stmt.get_statements()) {
            if (current_block_ == nullptr &&
                (statement == nullptr ||
                 !stmt_contains_label(statement.get()))) {
                continue;
            }
            if (!emit_stmt(statement.get())) {
                return false;
            }
        }
        return true;
    }

    bool emit_decl_stmt(const DeclStmt &decl_stmt) {
        for (const auto &declaration : decl_stmt.get_declarations()) {
            const auto *var_decl = dynamic_cast<const VarDecl *>(declaration.get());
            if (var_decl != nullptr) {
                if (!emit_var_decl(*var_decl)) {
                    return false;
                }
                continue;
            }
            const auto *const_decl =
                dynamic_cast<const ConstDecl *>(declaration.get());
            if (const_decl != nullptr) {
                if (!emit_const_decl(*const_decl)) {
                    return false;
                }
                continue;
            }
            if (dynamic_cast<const StructDecl *>(declaration.get()) != nullptr ||
                dynamic_cast<const UnionDecl *>(declaration.get()) != nullptr ||
                dynamic_cast<const EnumDecl *>(declaration.get()) != nullptr ||
                dynamic_cast<const TypedefDecl *>(declaration.get()) != nullptr) {
                continue;
            }
            if (dynamic_cast<const FunctionDecl *>(declaration.get()) != nullptr) {
                add_error("core ir generation currently supports only local "
                          "type and variable declarations inside block scopes",
                          declaration->get_source_span());
                return false;
            }
        }
        return true;
    }

    CoreIrValue *build_zero_constant_value(const SemanticType *target_semantic_type,
                                           const CoreIrType *target_type,
                                           SourceSpan source_span) {
        if (target_type == nullptr) {
            add_error("core ir generation could not resolve zero scalar target "
                      "type",
                      source_span);
            return nullptr;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Integer) {
            auto *zero_value = core_ir_context_->create_constant<CoreIrConstantInt>(
                target_type, 0);
            zero_value->set_source_span(source_span);
            return zero_value;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Pointer) {
            auto *null_value =
                core_ir_context_->create_constant<CoreIrConstantNull>(target_type);
            null_value->set_source_span(source_span);
            return null_value;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Float) {
            auto *zero_value =
                core_ir_context_->create_constant<CoreIrConstantFloat>(
                    target_type, "0.0");
            zero_value->set_source_span(source_span);
            return zero_value;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Array ||
            target_type->get_kind() == CoreIrTypeKind::Struct) {
            auto *zero_value =
                core_ir_context_->create_constant<CoreIrConstantZeroInitializer>(
                    target_type);
            zero_value->set_source_span(source_span);
            return zero_value;
        }

        add_error("core ir generation does not yet support zero-initializing "
                  "this local scalar type",
                  source_span);
        return nullptr;
    }

    bool emit_zero_core_value_to_address(CoreIrValue *address,
                                         const CoreIrType *target_type,
                                         SourceSpan source_span) {
        if (address == nullptr || target_type == nullptr) {
            add_error("core ir generation could not resolve zero core-value "
                      "target",
                      source_span);
            return false;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Integer) {
            auto *zero = core_ir_context_->create_constant<CoreIrConstantInt>(
                target_type, 0);
            zero->set_source_span(source_span);
            auto *store =
                current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, zero, address);
            store->set_source_span(source_span);
            return true;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Pointer) {
            auto *zero = core_ir_context_->create_constant<CoreIrConstantNull>(
                target_type);
            zero->set_source_span(source_span);
            auto *store =
                current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, zero, address);
            store->set_source_span(source_span);
            return true;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Float) {
            auto *zero =
                core_ir_context_->create_constant<CoreIrConstantFloat>(
                    target_type, "0.0");
            zero->set_source_span(source_span);
            auto *store =
                current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, zero, address);
            store->set_source_span(source_span);
            return true;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Array) {
            const auto *array_type = static_cast<const CoreIrArrayType *>(target_type);
            for (std::size_t index = 0; index < array_type->get_element_count();
                 ++index) {
                CoreIrValue *element_address =
                    build_gep(address, array_type->get_element_type(),
                              {create_i32_constant(0, source_span),
                               create_i32_constant(static_cast<long long>(index),
                                                   source_span)},
                              source_span);
                if (element_address == nullptr ||
                    !emit_zero_core_value_to_address(element_address,
                                                     array_type->get_element_type(),
                                                     source_span)) {
                    return false;
                }
            }
            return true;
        }
        if (target_type->get_kind() == CoreIrTypeKind::Struct) {
            const auto *struct_type =
                static_cast<const CoreIrStructType *>(target_type);
            for (std::size_t index = 0; index < struct_type->get_element_types().size();
                 ++index) {
                CoreIrValue *element_address =
                    build_gep(address, struct_type->get_element_types()[index],
                              {create_i32_constant(0, source_span),
                               create_i32_constant(static_cast<long long>(index),
                                                   source_span)},
                              source_span);
                if (element_address == nullptr ||
                    !emit_zero_core_value_to_address(
                        element_address, struct_type->get_element_types()[index],
                        source_span)) {
                    return false;
                }
            }
            return true;
        }
        add_error("core ir generation does not yet support zero-initializing "
                  "this Core IR type",
                  source_span);
        return false;
    }

    bool try_collect_char_array_string_initializer_bytes(
        const Expr *initializer, const SemanticType *element_semantic_type,
        std::size_t element_count, const std::string &diagnostic_context,
        std::vector<std::uint8_t> &bytes, bool &matched) {
        matched = false;
        bytes.clear();
        if (initializer == nullptr ||
            initializer->get_kind() != AstKind::StringLiteralExpr ||
            !is_character_semantic_type(element_semantic_type)) {
            return true;
        }

        const auto *string_literal =
            static_cast<const StringLiteralExpr *>(initializer);
        const std::string decoded =
            decode_string_literal_token(string_literal->get_value_text());
        if (decoded.size() + 1 > element_count) {
            add_error("core ir generation encountered an oversized " +
                          diagnostic_context,
                      string_literal->get_source_span());
            return false;
        }

        bytes.assign(decoded.begin(), decoded.end());
        bytes.resize(element_count, 0);
        matched = true;
        return true;
    }

    bool normalize_single_element_initializer(const Expr *initializer,
                                              const std::string &diagnostic_context,
                                              const Expr *&normalized_initializer) {
        normalized_initializer = initializer;
        if (initializer == nullptr || initializer->get_kind() != AstKind::InitListExpr) {
            return true;
        }

        const auto *init_list = static_cast<const InitListExpr *>(initializer);
        if (init_list->get_elements().size() > 1) {
            add_error("core ir generation encountered too many " +
                          diagnostic_context + " elements",
                      initializer->get_source_span());
            return false;
        }
        normalized_initializer = init_list->get_elements().empty()
                                     ? nullptr
                                     : init_list->get_elements().front().get();
        return true;
    }

    bool normalize_single_element_union_initializer(
        const Expr *initializer, const std::string &diagnostic_context,
        const InitListExpr *&normalized_init_list) {
        normalized_init_list = nullptr;
        if (initializer == nullptr) {
            return true;
        }
        if (initializer->get_kind() != AstKind::InitListExpr) {
            add_error("core ir generation currently requires an initializer "
                          "list for this " +
                          diagnostic_context,
                      initializer->get_source_span());
            return false;
        }

        normalized_init_list = static_cast<const InitListExpr *>(initializer);
        if (normalized_init_list->get_elements().size() > 1) {
            add_error("core ir generation encountered too many " +
                          diagnostic_context + " elements",
                      initializer->get_source_span());
            return false;
        }
        return true;
    }

    bool initializer_can_target_semantic_type(
        const Expr *initializer, const SemanticType *semantic_type) const {
        semantic_type = strip_qualifiers(semantic_type);
        if (initializer == nullptr || semantic_type == nullptr) {
            return initializer == nullptr;
        }
        if (is_zero_initializer_expr(initializer)) {
            return true;
        }

        switch (semantic_type->get_kind()) {
        case SemanticTypeKind::Builtin:
        case SemanticTypeKind::Enum:
            if (initializer->get_kind() == AstKind::InitListExpr) {
                return false;
            }
            if (is_integer_semantic_type(semantic_type)) {
                const SemanticType *initializer_type =
                    strip_qualifiers(get_node_type(initializer));
                return get_integer_constant_value(initializer).has_value() ||
                       is_integer_semantic_type(initializer_type);
            }
            if (is_float_semantic_type(semantic_type)) {
                const SemanticType *initializer_type =
                    strip_qualifiers(get_node_type(initializer));
                return get_scalar_numeric_constant_value(initializer).has_value() ||
                       is_integer_semantic_type(initializer_type) ||
                       is_float_semantic_type(initializer_type);
            }
            return false;
        case SemanticTypeKind::Pointer: {
            if (dynamic_cast<const StringLiteralExpr *>(initializer) != nullptr) {
                return true;
            }
            if (const auto *unary_expr =
                    dynamic_cast<const UnaryExpr *>(initializer);
                unary_expr != nullptr && unary_expr->get_operator_text() == "&") {
                return true;
            }
            const SemanticType *initializer_type =
                strip_qualifiers(get_node_type(initializer));
            return initializer_type != nullptr &&
                   (initializer_type->get_kind() == SemanticTypeKind::Pointer ||
                    initializer_type->get_kind() == SemanticTypeKind::Array ||
                    initializer_type->get_kind() == SemanticTypeKind::Function);
        }
        case SemanticTypeKind::Array:
            return initializer->get_kind() == AstKind::InitListExpr ||
                   (dynamic_cast<const StringLiteralExpr *>(initializer) !=
                    nullptr &&
                    is_character_semantic_type(
                        static_cast<const ArraySemanticType *>(semantic_type)
                            ->get_element_type()));
        case SemanticTypeKind::Struct:
        case SemanticTypeKind::Union:
            return initializer->get_kind() == AstKind::InitListExpr;
        case SemanticTypeKind::Function:
        case SemanticTypeKind::Qualified:
            return false;
        }
        return false;
    }

    bool initializer_element_designates_field(const InitListExpr *init_list,
                                              std::size_t element_index,
                                              const std::string &field_name,
                                              std::size_t designator_depth) const {
        if (init_list == nullptr || field_name.empty()) {
            return false;
        }
        const auto &designator = init_list->get_element_designator(element_index);
        if (!designator.has_value() || designator->size() <= designator_depth) {
            return false;
        }
        const auto &part = (*designator)[designator_depth];
        return part.kind == InitListExpr::Designator::Kind::Field &&
               part.text == field_name;
    }

    bool initializer_element_has_designator(const InitListExpr *init_list,
                                            std::size_t element_index) const {
        if (init_list == nullptr) {
            return false;
        }
        const auto &designator = init_list->get_element_designator(element_index);
        return designator.has_value() && !designator->empty();
    }

    bool initializer_element_is_relevant_at_depth(
        const InitListExpr *init_list, std::size_t element_index,
        std::size_t designator_depth) const {
        if (init_list == nullptr) {
            return false;
        }
        const auto &designator = init_list->get_element_designator(element_index);
        if (!designator.has_value() || designator->empty()) {
            return designator_depth == 0;
        }
        return designator->size() > designator_depth;
    }

    struct StructFieldInitializer {
        const Expr *expr = nullptr;
        const InitListExpr *nested_designator_list = nullptr;
        std::size_t nested_designator_depth = 0;

        bool has_nested_designators() const noexcept {
            return nested_designator_list != nullptr;
        }
    };

    template <typename VisitElement>
    bool walk_array_initializer_elements(const ArraySemanticType *array_semantic_type,
                                         const CoreIrArrayType *array_type,
                                         const Expr *initializer,
                                         SourceSpan source_span,
                                         const std::string &diagnostic_context,
                                         VisitElement &&visit_element) {
        if (array_semantic_type == nullptr || array_type == nullptr) {
            add_error("core ir generation could not resolve array initializer "
                      "shape",
                      source_span);
            return false;
        }

        const InitListExpr *init_list = nullptr;
        if (initializer != nullptr) {
            if (initializer->get_kind() != AstKind::InitListExpr) {
                add_error("core ir generation currently requires an initializer "
                              "list for this " +
                              diagnostic_context,
                          initializer->get_source_span());
                return false;
            }
            init_list = static_cast<const InitListExpr *>(initializer);
        }

        std::function<bool(const ArraySemanticType *, const CoreIrArrayType *,
                           const InitListExpr *, std::size_t &,
                           std::vector<std::size_t> &, SourceSpan)>
            walk = [&](const ArraySemanticType *current_semantic_type,
                       const CoreIrArrayType *current_array_type,
                       const InitListExpr *current_init_list, std::size_t &cursor,
                       std::vector<std::size_t> &element_path,
                       SourceSpan current_source_span) -> bool {
            const SemanticType *current_element_semantic_type =
                current_semantic_type->get_element_type();
            const CoreIrType *current_element_type =
                current_array_type->get_element_type();
            const auto *nested_array_semantic_type =
                dynamic_cast<const ArraySemanticType *>(
                    strip_qualifiers(current_element_semantic_type));
            const auto *nested_array_type =
                dynamic_cast<const CoreIrArrayType *>(current_element_type);

            for (std::size_t index = 0;
                 index < current_array_type->get_element_count(); ++index) {
                element_path.push_back(index);
                const Expr *element_initializer =
                    current_init_list != nullptr &&
                            cursor < current_init_list->get_elements().size()
                        ? current_init_list->get_elements()[cursor].get()
                        : nullptr;
                const SourceSpan element_source_span =
                    element_initializer == nullptr
                        ? current_source_span
                        : element_initializer->get_source_span();

                if (nested_array_semantic_type != nullptr &&
                    nested_array_type != nullptr && element_initializer != nullptr &&
                    !consumes_array_subinitializer_directly(
                        element_initializer, nested_array_semantic_type)) {
                    if (!walk(nested_array_semantic_type, nested_array_type,
                              current_init_list, cursor, element_path,
                              element_source_span)) {
                        element_path.pop_back();
                        return false;
                    }
                    element_path.pop_back();
                    continue;
                }

                if (element_initializer != nullptr && current_init_list != nullptr) {
                    ++cursor;
                }
                if (!visit_element(element_path, current_element_semantic_type,
                                   current_element_type, element_initializer,
                                   element_source_span)) {
                    element_path.pop_back();
                    return false;
                }
                element_path.pop_back();
            }
            return true;
        };

        std::size_t cursor = 0;
        std::vector<std::size_t> element_path;
        if (!walk(array_semantic_type, array_type, init_list, cursor, element_path,
                  source_span)) {
            return false;
        }
        if (init_list != nullptr && cursor < init_list->get_elements().size()) {
            add_error("core ir generation encountered too many " +
                          diagnostic_context + " elements",
                      initializer->get_source_span());
            return false;
        }
        return true;
    }

    template <typename VisitField>
    bool walk_struct_initializer_fields(
        const StructSemanticType *struct_semantic_type,
        const detail::AggregateLayoutInfo &layout, const Expr *initializer,
        SourceSpan source_span, const std::string &diagnostic_context,
        VisitField &&visit_field, std::size_t designator_depth = 0) {
        if (struct_semantic_type == nullptr) {
            add_error("core ir generation could not resolve struct initializer "
                      "shape",
                      source_span);
            return false;
        }

        const InitListExpr *init_list = nullptr;
        if (initializer != nullptr) {
            if (initializer->get_kind() != AstKind::InitListExpr) {
                add_error("core ir generation currently requires an initializer "
                              "list for this " +
                              diagnostic_context,
                          initializer->get_source_span());
                return false;
            }
            init_list = static_cast<const InitListExpr *>(initializer);
        }

        std::size_t initializer_index = 0;
        std::vector<bool> consumed_initializers(
            init_list == nullptr ? 0 : init_list->get_elements().size(), false);
        for (std::size_t field_index = 0;
             field_index < struct_semantic_type->get_fields().size(); ++field_index) {
            const auto &field = struct_semantic_type->get_fields()[field_index];
            const auto &field_layout = layout.field_layouts[field_index];
            if (!field_layout.has_value()) {
                continue;
            }

            StructFieldInitializer field_initializer;
            if (!field.get_name().empty()) {
                if (init_list != nullptr) {
                    for (std::size_t index = 0;
                         index < init_list->get_elements().size(); ++index) {
                        if (consumed_initializers[index] ||
                            !initializer_element_designates_field(
                                init_list, index, field.get_name(),
                                designator_depth)) {
                            continue;
                        }
                        const auto &designator =
                            init_list->get_element_designator(index);
                        if (designator.has_value() &&
                            designator->size() > designator_depth + 1) {
                            field_initializer.nested_designator_list = init_list;
                            field_initializer.nested_designator_depth =
                                designator_depth + 1;
                            for (std::size_t nested_index = index;
                                 nested_index < init_list->get_elements().size();
                                 ++nested_index) {
                                if (!consumed_initializers[nested_index] &&
                                    initializer_element_designates_field(
                                        init_list, nested_index, field.get_name(),
                                        designator_depth)) {
                                    consumed_initializers[nested_index] = true;
                                }
                            }
                        } else {
                            field_initializer.expr =
                                init_list->get_elements()[index].get();
                            consumed_initializers[index] = true;
                        }
                        break;
                    }
                }

                while (field_initializer.expr == nullptr &&
                       !field_initializer.has_nested_designators() &&
                       init_list != nullptr &&
                       initializer_index < init_list->get_elements().size()) {
                    if (consumed_initializers[initializer_index] ||
                        initializer_element_has_designator(init_list,
                                                           initializer_index)) {
                        ++initializer_index;
                        continue;
                    }
                    const Expr *candidate =
                        init_list->get_elements()[initializer_index].get();
                    if (initializer_can_target_semantic_type(candidate,
                                                             field.get_type())) {
                        field_initializer.expr = candidate;
                        consumed_initializers[initializer_index] = true;
                    }
                    ++initializer_index;
                }
            }

            const SourceSpan field_source_span =
                field_initializer.expr == nullptr ? source_span
                                                  : field_initializer.expr
                                                        ->get_source_span();
            if (!visit_field(field_index, field, *field_layout, field_initializer,
                             field_source_span)) {
                return false;
            }
        }

        if (init_list != nullptr &&
            [&]() {
                for (std::size_t index = 0;
                     index < consumed_initializers.size(); ++index) {
                    if (!consumed_initializers[index] &&
                        initializer_element_is_relevant_at_depth(
                            init_list, index, designator_depth)) {
                        return true;
                    }
                }
                return false;
            }()) {
            add_error("core ir generation encountered too many " +
                          diagnostic_context + " elements",
                      initializer->get_source_span());
            return false;
        }
        return true;
    }

    bool emit_local_initializer_to_address(CoreIrValue *address,
                                           const SemanticType *target_semantic_type,
                                           const Expr *initializer,
                                           SourceSpan source_span,
                                           std::size_t designator_depth = 0) {
        target_semantic_type = strip_qualifiers(target_semantic_type);
        const CoreIrType *target_type = get_or_create_type(target_semantic_type);
        if (address == nullptr || target_semantic_type == nullptr ||
            target_type == nullptr) {
            add_error("core ir generation could not resolve local initializer "
                      "target",
                      source_span);
            return false;
        }

        if (initializer == nullptr || is_zero_initializer_expr(initializer)) {
            CoreIrValue *zero_value = build_zero_constant_value(
                target_semantic_type, target_type, source_span);
            if (zero_value == nullptr) {
                return false;
            }
            auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                void_type_, zero_value, address);
            store->set_source_span(initializer == nullptr
                                       ? source_span
                                       : initializer->get_source_span());
            return true;
        }

        if (target_semantic_type->get_kind() == SemanticTypeKind::Array) {
            const auto *array_semantic_type =
                static_cast<const ArraySemanticType *>(target_semantic_type);
            const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(target_type);
            if (array_type == nullptr) {
                add_error("core ir generation expected an array type for local "
                          "initializer",
                          source_span);
                return false;
            }

            const std::size_t element_count = array_type->get_element_count();
            const CoreIrType *element_type = array_type->get_element_type();
            std::vector<std::uint8_t> string_bytes;
            bool matched_string_literal = false;
            if (!try_collect_char_array_string_initializer_bytes(
                    initializer, array_semantic_type->get_element_type(),
                    element_count, "local string literal initializer",
                    string_bytes, matched_string_literal)) {
                return false;
            }
            if (matched_string_literal) {
                for (std::size_t index = 0; index < element_count; ++index) {
                    CoreIrValue *element_address =
                        build_gep(address, element_type,
                                  {create_i32_constant(0, source_span),
                                   create_i32_constant(
                                       static_cast<long long>(index), source_span)},
                                  source_span);
                    if (element_address == nullptr) {
                        return false;
                    }
                    CoreIrValue *element_value = build_converted_value(
                        create_i32_constant(string_bytes[index], source_span),
                        get_static_builtin_semantic_type("int"),
                        array_semantic_type->get_element_type(), source_span);
                    if (element_value == nullptr) {
                        return false;
                    }
                    auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                        void_type_, element_value, element_address);
                    store->set_source_span(initializer->get_source_span());
                }
                return true;
            }

            return walk_array_initializer_elements(
                array_semantic_type, array_type, initializer, source_span,
                "local array initializer",
                [&](const std::vector<std::size_t> &element_path,
                    const SemanticType *element_semantic_type,
                    const CoreIrType *current_element_type,
                    const Expr *element_initializer,
                    SourceSpan element_source_span) -> bool {
                    CoreIrValue *element_address =
                        build_local_array_element_address(
                            address, current_element_type, element_path,
                            element_source_span);
                    if (element_address == nullptr) {
                        return false;
                    }
                    return emit_local_initializer_to_address(
                        element_address, element_semantic_type, element_initializer,
                        element_source_span);
                });
        }

        if (target_semantic_type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_semantic_type =
                static_cast<const StructSemanticType *>(target_semantic_type);
            const detail::AggregateLayoutInfo layout =
                detail::compute_aggregate_layout(target_semantic_type);
            if (initializer != nullptr) {
                if (initializer->get_kind() != AstKind::InitListExpr) {
                    CoreIrValue *value = build_expr(initializer);
                    if (value == nullptr) {
                        return false;
                    }
                    value = build_converted_value(value, get_node_type(initializer),
                                                  target_semantic_type,
                                                  initializer->get_source_span());
                    if (value == nullptr) {
                        return false;
                    }
                    auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                        void_type_, value, address);
                    store->set_source_span(initializer->get_source_span());
                    return true;
                }
            }
            return walk_struct_initializer_fields(
                struct_semantic_type, layout, initializer, source_span,
                "local struct initializer",
                [&](std::size_t /*field_index*/, const SemanticFieldInfo &field,
                    const detail::AggregateFieldLayout &field_layout,
                    const StructFieldInitializer &field_initializer,
                    SourceSpan field_source_span) -> bool {
                    const Expr *field_expr = field_initializer.expr;
                    if (field_layout.is_bit_field) {
                        const CoreIrType *storage_type =
                            get_or_create_type(field_layout.storage_type);
                        if (storage_type == nullptr) {
                            add_error("core ir generation could not resolve local "
                                      "bit-field storage type",
                                      source_span);
                            return false;
                        }
                        CoreIrValue *storage_address =
                            build_gep(address, storage_type,
                                      {create_i32_constant(0, source_span),
                                       create_i32_constant(
                                           static_cast<long long>(
                                               field_layout.llvm_element_index),
                                           source_span)},
                                      source_span);
                        if (storage_address == nullptr) {
                            return false;
                        }
                        CoreIrValue *field_value =
                            field_expr == nullptr
                                ? create_i32_constant(0, source_span)
                                : build_expr(field_expr);
                        if (field_value == nullptr) {
                            return false;
                        }
                        return store_bit_field_value(
                                   BitFieldLValue{storage_address, field_layout,
                                                  field.get_type()},
                                   field_value,
                                   field_expr == nullptr
                                       ? get_static_builtin_semantic_type("int")
                                       : get_node_type(field_expr),
                                   field_source_span) != nullptr;
                    }

                    const CoreIrType *field_type =
                        get_or_create_type(field_layout.field_type);
                    if (field_type == nullptr) {
                        add_error("core ir generation could not resolve local struct "
                                  "field type",
                                  source_span);
                        return false;
                    }
                    CoreIrValue *field_address =
                        build_gep(address, field_type,
                                  {create_i32_constant(0, source_span),
                                   create_i32_constant(
                                       static_cast<long long>(
                                           field_layout.llvm_element_index),
                                       source_span)},
                                  source_span);
                    if (field_address == nullptr) {
                        return false;
                    }
                    if (field_initializer.has_nested_designators()) {
                        return emit_local_initializer_to_address(
                            field_address, field.get_type(),
                            field_initializer.nested_designator_list,
                            field_source_span,
                            field_initializer.nested_designator_depth);
                    }
                    return emit_local_initializer_to_address(
                        field_address, field.get_type(), field_expr,
                        field_source_span);
                },
                designator_depth);
        }

        if (target_semantic_type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_semantic_type =
                static_cast<const UnionSemanticType *>(target_semantic_type);
            const auto *union_type =
                dynamic_cast<const CoreIrStructType *>(target_type);
            if (union_type == nullptr || union_type->get_element_types().empty()) {
                add_error("core ir generation expected a union type for local "
                          "initializer",
                          source_span);
                return false;
            }

            if (initializer != nullptr &&
                initializer->get_kind() != AstKind::InitListExpr) {
                CoreIrValue *value = build_expr(initializer);
                if (value == nullptr) {
                    return false;
                }
                value = build_converted_value(value, get_node_type(initializer),
                                              target_semantic_type,
                                              initializer->get_source_span());
                if (value == nullptr) {
                    return false;
                }
                auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, value, address);
                store->set_source_span(initializer->get_source_span());
                return true;
            }

            const SemanticType *carrier_semantic_type =
                get_union_carrier_semantic_type(union_semantic_type);
            if (carrier_semantic_type == nullptr) {
                add_error("core ir generation could not resolve local union "
                          "carrier type",
                          source_span);
                return false;
            }

            for (std::size_t index = 0; index < union_type->get_element_types().size();
                 ++index) {
                CoreIrValue *element_address =
                    build_gep(address, union_type->get_element_types()[index],
                              {create_i32_constant(0, source_span),
                               create_i32_constant(static_cast<long long>(index),
                                                   source_span)},
                              source_span);
                if (element_address == nullptr) {
                    return false;
                }
                if (index == 0) {
                    if (!emit_local_initializer_to_address(
                            element_address, carrier_semantic_type, nullptr,
                            source_span)) {
                        return false;
                    }
                    continue;
                }
                if (!emit_zero_core_value_to_address(
                        element_address, union_type->get_element_types()[index],
                        source_span)) {
                    return false;
                }
            }

            if (initializer == nullptr) {
                return true;
            }
            if (initializer->get_kind() != AstKind::InitListExpr) {
                add_error("core ir generation currently requires an initializer "
                          "list for this local union initializer",
                          initializer->get_source_span());
                return false;
            }
            const auto *init_list = static_cast<const InitListExpr *>(initializer);
            if (union_semantic_type->get_fields().empty() ||
                init_list->get_elements().empty()) {
                return true;
            }

            std::size_t selected_field_index = 0;
            const Expr *selected_initializer = nullptr;
            const InitListExpr *nested_designator_list = nullptr;
            std::size_t nested_designator_depth = 0;
            bool matched_designator = false;

            for (std::size_t field_index = 0;
                 field_index < union_semantic_type->get_fields().size();
                 ++field_index) {
                const auto &field = union_semantic_type->get_fields()[field_index];
                if (field.get_name().empty()) {
                    continue;
                }
                for (std::size_t element_index = 0;
                     element_index < init_list->get_elements().size();
                     ++element_index) {
                    if (!initializer_element_designates_field(
                            init_list, element_index, field.get_name(),
                            designator_depth)) {
                        continue;
                    }
                    selected_field_index = field_index;
                    matched_designator = true;
                    const auto &designator =
                        init_list->get_element_designator(element_index);
                    if (designator.has_value() &&
                        designator->size() > designator_depth + 1) {
                        nested_designator_list = init_list;
                        nested_designator_depth = designator_depth + 1;
                    } else {
                        selected_initializer =
                            init_list->get_elements()[element_index].get();
                    }
                    break;
                }
                if (matched_designator) {
                    break;
                }
            }

            if (!matched_designator) {
                std::size_t undesignated_count = 0;
                for (std::size_t element_index = 0;
                     element_index < init_list->get_elements().size();
                     ++element_index) {
                    if (initializer_element_has_designator(init_list,
                                                           element_index)) {
                        continue;
                    }
                    if (undesignated_count == 0) {
                        selected_initializer =
                            init_list->get_elements()[element_index].get();
                    }
                    ++undesignated_count;
                }
                if (undesignated_count > 1) {
                    add_error("core ir generation encountered too many local "
                              "union initializer elements",
                              initializer->get_source_span());
                    return false;
                }
                if (undesignated_count == 0) {
                    return true;
                }
            }

            const auto &selected_field =
                union_semantic_type->get_fields()[selected_field_index];
            const CoreIrType *selected_field_type =
                get_or_create_type(selected_field.get_type());
            if (selected_field_type == nullptr) {
                return false;
            }
            CoreIrValue *field_address =
                build_gep(address, selected_field_type,
                          {create_i32_constant(0, source_span),
                           create_i32_constant(0, source_span)},
                          source_span);
            if (field_address == nullptr) {
                return false;
            }
            if (nested_designator_list != nullptr) {
                return emit_local_initializer_to_address(
                    field_address, selected_field.get_type(),
                    nested_designator_list, source_span,
                    nested_designator_depth);
            }
            return emit_local_initializer_to_address(
                field_address, selected_field.get_type(), selected_initializer,
                selected_initializer == nullptr
                    ? source_span
                    : selected_initializer->get_source_span());
        }

        if (!normalize_single_element_initializer(
                initializer, "local scalar initializer", initializer)) {
            return false;
        }

        CoreIrValue *value = nullptr;
        if (initializer == nullptr) {
            value = build_zero_constant_value(target_semantic_type, target_type,
                                              source_span);
        } else {
            value = build_expr(initializer);
            if (value == nullptr) {
                return false;
            }
            value = build_converted_value(value, get_node_type(initializer),
                                          target_semantic_type,
                                          initializer->get_source_span());
        }
        if (value == nullptr) {
            return false;
        }

        auto *store = current_block_->create_instruction<CoreIrStoreInst>(
            void_type_, value, address);
        store->set_source_span(source_span);
        return true;
    }

    bool emit_local_decl(const SemanticSymbol *symbol, const std::string &symbol_name,
                         const Expr *initializer, SourceSpan source_span) {
        if (symbol == nullptr || symbol->get_type() == nullptr) {
            add_error("core ir generation could not resolve local variable type",
                      source_span);
            return false;
        }
        const CoreIrType *declared_type = get_or_create_type(symbol->get_type());
        if (declared_type == nullptr) {
            add_error("core ir generation does not support this local variable type",
                      source_span);
            return false;
        }

        auto *stack_slot = current_function_->create_stack_slot<CoreIrStackSlot>(
            next_stack_slot_name(symbol_name + ".addr"), declared_type,
            get_default_alignment(declared_type));
        local_bindings_[symbol] = ValueBinding{nullptr, stack_slot, nullptr};

        if (initializer != nullptr) {
            CoreIrValue *address =
                build_stack_slot_address(*stack_slot, source_span);
            if (address == nullptr) {
                return false;
            }
            if (!emit_local_initializer_to_address(
                    address, symbol->get_type(), initializer, source_span)) {
                return false;
            }
        }
        return true;
    }

    bool emit_local_static_decl(const SemanticSymbol *symbol,
                                const std::string &symbol_name,
                                const Expr *initializer,
                                SourceSpan source_span) {
        if (symbol == nullptr || symbol->get_type() == nullptr) {
            add_error("core ir generation could not resolve local static variable type",
                      source_span);
            return false;
        }
        const CoreIrType *declared_type = get_or_create_type(symbol->get_type());
        if (declared_type == nullptr) {
            add_error("core ir generation does not support this local static "
                      "variable type",
                      source_span);
            return false;
        }

        auto *global = module_->create_global<CoreIrGlobal>(
            next_local_static_global_name(symbol_name), declared_type, nullptr,
            true, false);
        local_bindings_[symbol] = ValueBinding{nullptr, nullptr, global};

        const CoreIrConstant *global_initializer = build_global_initializer(
            initializer, symbol->get_type(), declared_type, source_span, false);
        if (compiler_context_.get_diagnostic_engine().has_error()) {
            return false;
        }
        global->set_initializer(global_initializer);
        return true;
    }

    bool emit_var_decl(const VarDecl &var_decl) {
        if (var_decl.get_is_static()) {
            return emit_local_static_decl(get_symbol_binding(&var_decl),
                                          var_decl.get_name(),
                                          var_decl.get_initializer(),
                                          var_decl.get_source_span());
        }
        return emit_local_decl(get_symbol_binding(&var_decl), var_decl.get_name(),
                               var_decl.get_initializer(),
                               var_decl.get_source_span());
    }

    bool emit_const_decl(const ConstDecl &const_decl) {
        return emit_local_decl(get_symbol_binding(&const_decl), const_decl.get_name(),
                               const_decl.get_initializer(),
                               const_decl.get_source_span());
    }

    bool emit_return_stmt(const ReturnStmt &return_stmt) {
        if (current_block_ == nullptr) {
            add_error("core ir generation reached a return in a terminated "
                      "control-flow path",
                      return_stmt.get_source_span());
            return false;
        }

        CoreIrReturnInst *instruction = nullptr;
        if (return_stmt.get_value() == nullptr) {
            if (current_function_return_type_ != void_type_) {
                add_error("core ir generation expected a return value",
                          return_stmt.get_source_span());
                return false;
            }
            instruction =
                current_block_->create_instruction<CoreIrReturnInst>(void_type_);
        } else {
            CoreIrValue *return_value = build_expr(return_stmt.get_value());
            if (return_value == nullptr) {
                return false;
            }
            return_value = build_converted_value(
                return_value, get_node_type(return_stmt.get_value()),
                current_function_return_semantic_type_,
                return_stmt.get_source_span());
            if (return_value == nullptr) {
                return false;
            }
            instruction = current_block_->create_instruction<CoreIrReturnInst>(
                void_type_, return_value);
        }
        instruction->set_source_span(return_stmt.get_source_span());
        current_block_ = nullptr;
        return true;
    }

    bool emit_expr_stmt(const ExprStmt &expr_stmt) {
        if (expr_stmt.get_expression() == nullptr) {
            return true;
        }
        if (current_block_ == nullptr || current_block_->get_has_terminator()) {
            return true;
        }
        return build_expr(expr_stmt.get_expression()) != nullptr;
    }

    bool emit_jump_to(CoreIrBasicBlock *target_block, SourceSpan source_span) {
        if (current_block_ == nullptr || current_block_->get_has_terminator()) {
            return true;
        }
        auto *instruction =
            current_block_->create_instruction<CoreIrJumpInst>(void_type_,
                                                               target_block);
        instruction->set_source_span(source_span);
        return true;
    }

    bool emit_if_stmt(const IfStmt &if_stmt) {
        if (current_block_ == nullptr) {
            add_error("core ir generation reached an if statement in a "
                      "terminated control-flow path",
                      if_stmt.get_source_span());
            return false;
        }

        CoreIrValue *condition = build_expr(if_stmt.get_condition());
        if (condition == nullptr) {
            return false;
        }

        const std::string if_suffix = next_if_suffix();
        CoreIrBasicBlock *then_block =
            current_function_->create_basic_block<CoreIrBasicBlock>("if.then" +
                                                                    if_suffix);
        CoreIrBasicBlock *else_block = nullptr;
        CoreIrBasicBlock *continuation_block = nullptr;
        if (if_stmt.get_else_branch() != nullptr) {
            else_block = current_function_->create_basic_block<CoreIrBasicBlock>(
                "if.else" + if_suffix);
        } else {
            continuation_block =
                current_function_->create_basic_block<CoreIrBasicBlock>(
                    "if.end" + if_suffix);
            else_block = continuation_block;
        }

        auto *branch_instruction =
            current_block_->create_instruction<CoreIrCondJumpInst>(
                void_type_, condition, then_block, else_block);
        branch_instruction->set_source_span(if_stmt.get_source_span());

        current_block_ = then_block;
        if (!emit_stmt(if_stmt.get_then_branch())) {
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            if (continuation_block == nullptr) {
                continuation_block =
                    current_function_->create_basic_block<CoreIrBasicBlock>(
                        "if.end" + if_suffix);
            }
            emit_jump_to(continuation_block, if_stmt.get_source_span());
        }

        if (if_stmt.get_else_branch() != nullptr) {
            current_block_ = else_block;
            if (!emit_stmt(if_stmt.get_else_branch())) {
                return false;
            }
            if (current_block_ != nullptr &&
                !current_block_->get_has_terminator()) {
                if (continuation_block == nullptr) {
                    continuation_block =
                        current_function_->create_basic_block<CoreIrBasicBlock>(
                            "if.end" + if_suffix);
                }
                emit_jump_to(continuation_block, if_stmt.get_source_span());
            }
        }

        current_block_ = continuation_block;
        return true;
    }

    bool emit_while_stmt(const WhileStmt &while_stmt) {
        const bool has_label_entry =
            current_block_ == nullptr && stmt_contains_label(while_stmt.get_body());
        if (current_block_ == nullptr && !has_label_entry) {
            add_error("core ir generation reached a while statement in a "
                      "terminated control-flow path",
                      while_stmt.get_source_span());
            return false;
        }

        const bool infinite_loop_without_break =
            expr_is_obviously_nonzero_constant(while_stmt.get_condition()) &&
            !stmt_contains_break(while_stmt.get_body());
        const std::string loop_suffix = next_loop_suffix();
        CoreIrBasicBlock *condition_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "while.cond" + loop_suffix);
        CoreIrBasicBlock *body_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "while.body" + loop_suffix);
        CoreIrBasicBlock *end_block =
            infinite_loop_without_break
                ? nullptr
                : current_function_->create_basic_block<CoreIrBasicBlock>(
                      "while.end" + loop_suffix);

        if (!has_label_entry) {
            emit_jump_to(condition_block, while_stmt.get_source_span());
        }

        current_block_ = condition_block;
        if (infinite_loop_without_break) {
            auto *jump_instruction =
                current_block_->create_instruction<CoreIrJumpInst>(void_type_,
                                                                   body_block);
            jump_instruction->set_source_span(while_stmt.get_source_span());
        } else {
            CoreIrValue *condition = build_expr(while_stmt.get_condition());
            if (condition == nullptr) {
                return false;
            }
            auto *branch_instruction =
                current_block_->create_instruction<CoreIrCondJumpInst>(
                    void_type_, condition, body_block, end_block);
            branch_instruction->set_source_span(while_stmt.get_source_span());
        }

        loop_frames_.push_back(LoopFrame{end_block, condition_block});
        if (!infinite_loop_without_break) {
            break_blocks_.push_back(end_block);
        }
        current_block_ = body_block;
        if (!emit_stmt(while_stmt.get_body())) {
            loop_frames_.pop_back();
            if (!infinite_loop_without_break) {
                break_blocks_.pop_back();
            }
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            emit_jump_to(condition_block, while_stmt.get_source_span());
        }
        loop_frames_.pop_back();
        if (!infinite_loop_without_break) {
            break_blocks_.pop_back();
        }
        current_block_ = infinite_loop_without_break ? nullptr : end_block;
        return true;
    }

    bool emit_do_while_stmt(const DoWhileStmt &do_while_stmt) {
        if (current_block_ == nullptr) {
            add_error("core ir generation reached a do-while statement in a "
                      "terminated control-flow path",
                      do_while_stmt.get_source_span());
            return false;
        }

        const std::string loop_suffix = next_loop_suffix();
        CoreIrBasicBlock *body_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "dowhile.body" + loop_suffix);
        CoreIrBasicBlock *condition_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "dowhile.cond" + loop_suffix);
        CoreIrBasicBlock *end_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "dowhile.end" + loop_suffix);

        emit_jump_to(body_block, do_while_stmt.get_source_span());

        loop_frames_.push_back(LoopFrame{end_block, condition_block});
        break_blocks_.push_back(end_block);
        current_block_ = body_block;
        if (!emit_stmt(do_while_stmt.get_body())) {
            loop_frames_.pop_back();
            break_blocks_.pop_back();
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            emit_jump_to(condition_block, do_while_stmt.get_source_span());
        }
        loop_frames_.pop_back();
        break_blocks_.pop_back();

        current_block_ = condition_block;
        CoreIrValue *condition = build_expr(do_while_stmt.get_condition());
        if (condition == nullptr) {
            return false;
        }
        auto *branch_instruction =
            current_block_->create_instruction<CoreIrCondJumpInst>(
                void_type_, condition, body_block, end_block);
        branch_instruction->set_source_span(do_while_stmt.get_source_span());

        current_block_ = end_block;
        return true;
    }

    bool emit_for_stmt(const ForStmt &for_stmt) {
        const bool has_label_entry =
            current_block_ == nullptr && stmt_contains_label(for_stmt.get_body());
        if (current_block_ == nullptr && !has_label_entry) {
            add_error("core ir generation reached a for statement in a "
                      "terminated control-flow path",
                      for_stmt.get_source_span());
            return false;
        }

        if (!has_label_entry) {
            if (for_stmt.get_init_decl() != nullptr &&
                !emit_decl_stmt(*for_stmt.get_init_decl())) {
                return false;
            }
            if (for_stmt.get_init() != nullptr &&
                build_expr(for_stmt.get_init()) == nullptr) {
                return false;
            }
        }

        const std::string loop_suffix = next_loop_suffix();
        CoreIrBasicBlock *condition_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "for.cond" + loop_suffix);
        CoreIrBasicBlock *body_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "for.body" + loop_suffix);
        CoreIrBasicBlock *step_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "for.step" + loop_suffix);
        CoreIrBasicBlock *end_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "for.end" + loop_suffix);

        if (!has_label_entry) {
            emit_jump_to(condition_block, for_stmt.get_source_span());
        }

        current_block_ = condition_block;
        if (for_stmt.get_condition() != nullptr) {
            CoreIrValue *condition = build_expr(for_stmt.get_condition());
            if (condition == nullptr) {
                return false;
            }
            auto *branch_instruction =
                current_block_->create_instruction<CoreIrCondJumpInst>(
                    void_type_, condition, body_block, end_block);
            branch_instruction->set_source_span(for_stmt.get_source_span());
        } else {
            emit_jump_to(body_block, for_stmt.get_source_span());
        }

        loop_frames_.push_back(LoopFrame{end_block, step_block});
        break_blocks_.push_back(end_block);
        current_block_ = body_block;
        if (!emit_stmt(for_stmt.get_body())) {
            loop_frames_.pop_back();
            break_blocks_.pop_back();
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            emit_jump_to(step_block, for_stmt.get_source_span());
        }
        loop_frames_.pop_back();
        break_blocks_.pop_back();

        current_block_ = step_block;
        if (for_stmt.get_step() != nullptr && build_expr(for_stmt.get_step()) == nullptr) {
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            emit_jump_to(condition_block, for_stmt.get_source_span());
        }

        current_block_ = end_block;
        return true;
    }

    bool emit_switch_stmt(const SwitchStmt &switch_stmt) {
        if (current_block_ == nullptr) {
            add_error("core ir generation reached a switch statement in a "
                      "terminated control-flow path",
                      switch_stmt.get_source_span());
            return false;
        }

        CoreIrValue *switch_value = build_expr(switch_stmt.get_condition());
        const SemanticType *switch_semantic_type =
            get_node_type(switch_stmt.get_condition());
        const CoreIrType *switch_core_type =
            switch_value == nullptr ? nullptr : switch_value->get_type();
        if (switch_value == nullptr || switch_semantic_type == nullptr ||
            switch_core_type == nullptr) {
            return false;
        }

        const std::string switch_suffix = next_switch_suffix();
        struct SwitchEntrySpec {
            const CaseStmt *case_stmt = nullptr;
            const DefaultStmt *default_stmt = nullptr;
            std::vector<const Stmt *> body_statements;
        };
        struct SwitchEntry {
            SwitchEntrySpec spec;
            CoreIrBasicBlock *block = nullptr;
        };

        std::vector<const Stmt *> pre_case_statements;
        std::vector<SwitchEntrySpec> entry_specs;
        auto append_statement_to_current_entry =
            [&](const Stmt *stmt) -> bool {
            if (stmt == nullptr) {
                return true;
            }
            if (entry_specs.empty()) {
                pre_case_statements.push_back(stmt);
                return true;
            }
            entry_specs.back().body_statements.push_back(stmt);
            return true;
        };
        std::function<bool(const Stmt *)> collect_switch_entries =
            [&](const Stmt *stmt) -> bool {
            if (stmt == nullptr) {
                return true;
            }
            if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
                block_stmt != nullptr) {
                for (const auto &child : block_stmt->get_statements()) {
                    if (!collect_switch_entries(child.get())) {
                        return false;
                    }
                }
                return true;
            }
            if (const auto *case_stmt = dynamic_cast<const CaseStmt *>(stmt);
                case_stmt != nullptr) {
                entry_specs.push_back(SwitchEntrySpec{case_stmt, nullptr, {}});
                if (case_stmt->get_body() != nullptr) {
                    return collect_switch_entries(case_stmt->get_body());
                }
                return true;
            }
            if (const auto *default_stmt = dynamic_cast<const DefaultStmt *>(stmt);
                default_stmt != nullptr) {
                entry_specs.push_back(SwitchEntrySpec{nullptr, default_stmt, {}});
                if (default_stmt->get_body() != nullptr) {
                    return collect_switch_entries(default_stmt->get_body());
                }
                return true;
            }
            return append_statement_to_current_entry(stmt);
        };
        if (!collect_switch_entries(switch_stmt.get_body())) {
            return false;
        }

        std::vector<SwitchEntry> entries;
        std::vector<const CaseStmt *> case_entries;
        std::vector<CoreIrBasicBlock *> case_blocks;
        const DefaultStmt *default_entry = nullptr;
        CoreIrBasicBlock *default_block = nullptr;
        for (const SwitchEntrySpec &entry_spec : entry_specs) {
            if (entry_spec.case_stmt != nullptr) {
                CoreIrBasicBlock *case_block =
                    current_function_->create_basic_block<CoreIrBasicBlock>(
                        "switch.case" + switch_suffix + "." +
                        std::to_string(case_entries.size()));
                entries.push_back({entry_spec, case_block});
                case_entries.push_back(entry_spec.case_stmt);
                case_blocks.push_back(case_block);
                continue;
            }
            if (entry_spec.default_stmt != nullptr) {
                default_block =
                    current_function_->create_basic_block<CoreIrBasicBlock>(
                        "switch.default" + switch_suffix);
                entries.push_back({entry_spec, default_block});
                default_entry = entry_spec.default_stmt;
                continue;
            }
        }

        CoreIrBasicBlock *end_block =
            current_function_->create_basic_block<CoreIrBasicBlock>(
                "switch.end" + switch_suffix);

        for (const Stmt *pre_case_statement : pre_case_statements) {
            if (!emit_stmt(pre_case_statement)) {
                return false;
            }
        }

        if (!case_entries.empty()) {
            std::vector<CoreIrBasicBlock *> test_blocks;
            test_blocks.reserve(case_entries.size());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                test_blocks.push_back(
                    current_function_->create_basic_block<CoreIrBasicBlock>(
                        "switch.test" + switch_suffix + "." +
                        std::to_string(index)));
            }

            emit_jump_to(test_blocks.front(), switch_stmt.get_source_span());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                current_block_ = test_blocks[index];
                const std::optional<long long> case_value =
                    get_integer_constant_value(case_entries[index]->get_value());
                if (!case_value.has_value()) {
                    add_error("core ir generation expected a constant case value",
                              case_entries[index]->get_source_span());
                    return false;
                }
                CoreIrValue *case_ir_value =
                    build_converted_value(
                        create_i32_constant(*case_value,
                                            case_entries[index]->get_source_span()),
                        get_static_builtin_semantic_type("int"),
                        switch_semantic_type,
                        case_entries[index]->get_source_span());
                if (case_ir_value == nullptr) {
                    return false;
                }
                const auto *compare_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(1);
                auto *comparison =
                    current_block_->create_instruction<CoreIrCompareInst>(
                        get_compare_predicate("==", switch_semantic_type).value(),
                        compare_type, next_temp_name(), switch_value,
                        case_ir_value);
                comparison->set_source_span(case_entries[index]->get_source_span());
                CoreIrBasicBlock *false_block =
                    index + 1 < case_entries.size()
                        ? test_blocks[index + 1]
                        : (default_entry != nullptr ? default_block : end_block);
                auto *branch_instruction =
                    current_block_->create_instruction<CoreIrCondJumpInst>(
                        void_type_, comparison, case_blocks[index], false_block);
                branch_instruction->set_source_span(case_entries[index]->get_source_span());
            }
        } else {
            emit_jump_to(default_entry != nullptr ? default_block : end_block,
                         switch_stmt.get_source_span());
        }

        break_blocks_.push_back(end_block);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            current_block_ = entries[index].block;
            for (const Stmt *entry_body : entries[index].spec.body_statements) {
                if (current_block_ == nullptr &&
                    (entry_body == nullptr || !stmt_contains_label(entry_body))) {
                    continue;
                }
                if (!emit_stmt(entry_body)) {
                    break_blocks_.pop_back();
                    return false;
                }
            }
            if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
                CoreIrBasicBlock *fallthrough_block =
                    index + 1 < entries.size() ? entries[index + 1].block : end_block;
                emit_jump_to(fallthrough_block, switch_stmt.get_source_span());
            }
        }
        break_blocks_.pop_back();

        current_block_ = end_block;
        return true;
    }

    bool emit_break_stmt(SourceSpan source_span) {
        if (break_blocks_.empty()) {
            add_error("core ir generation encountered break outside of a loop or "
                      "switch",
                      source_span);
            return false;
        }
        emit_jump_to(break_blocks_.back(), source_span);
        current_block_ = nullptr;
        return true;
    }

    bool emit_continue_stmt(SourceSpan source_span) {
        if (loop_frames_.empty()) {
            add_error("core ir generation encountered continue outside of a loop",
                      source_span);
            return false;
        }
        emit_jump_to(loop_frames_.back().continue_block, source_span);
        current_block_ = nullptr;
        return true;
    }

    bool emit_label_stmt(const LabelStmt &label_stmt) {
        CoreIrBasicBlock *label_block =
            get_or_create_label_block(label_stmt.get_label_name());
        if (label_block == nullptr) {
            add_error("core ir generation could not allocate a label block",
                      label_stmt.get_source_span());
            return false;
        }
        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            emit_jump_to(label_block, label_stmt.get_source_span());
        }
        current_block_ = label_block;
        return emit_stmt(label_stmt.get_body());
    }

    bool emit_goto_stmt(const GotoStmt &goto_stmt) {
        if (goto_stmt.get_is_indirect()) {
            CoreIrValue *address = build_expr(goto_stmt.get_indirect_target());
            if (address == nullptr) {
                return false;
            }
            if (address_taken_label_blocks_.empty()) {
                add_error("core ir generation could not resolve any label "
                          "addresses for indirect goto",
                          goto_stmt.get_source_span());
                return false;
            }
            auto *jump = current_block_->create_instruction<CoreIrIndirectJumpInst>(
                void_type_, address, address_taken_label_blocks_);
            jump->set_source_span(goto_stmt.get_source_span());
            current_block_ = nullptr;
            return true;
        }
        CoreIrBasicBlock *target_block =
            get_or_create_label_block(goto_stmt.get_target_label());
        if (target_block == nullptr) {
            add_error("core ir generation could not resolve goto label '" +
                          goto_stmt.get_target_label() + "'",
                      goto_stmt.get_source_span());
            return false;
        }
        emit_jump_to(target_block, goto_stmt.get_source_span());
        current_block_ = nullptr;
        return true;
    }

    bool bind_function_parameters(const FunctionDecl &function_decl,
                                  CoreIrFunction &function) {
        const auto *function_semantic_type =
            static_cast<const FunctionSemanticType *>(
                strip_qualifiers(get_symbol_binding(&function_decl)->get_type()));
        const auto &parameter_semantic_types =
            function_semantic_type->get_parameter_types();
        for (std::size_t parameter_index = 0;
             parameter_index < function_decl.get_parameters().size();
             ++parameter_index) {
            const auto &parameter_decl = function_decl.get_parameters()[parameter_index];
            const auto *parameter_symbol = get_symbol_binding(parameter_decl.get());
            const SemanticType *parameter_semantic_type = nullptr;
            std::string parameter_name;
            if (parameter_symbol != nullptr) {
                parameter_semantic_type = parameter_symbol->get_type();
                parameter_name = parameter_symbol->get_name();
            } else if (parameter_index < parameter_semantic_types.size()) {
                parameter_semantic_type = parameter_semantic_types[parameter_index];
                parameter_name = "arg" + std::to_string(parameter_index);
            } else {
                add_error("core ir generation could not resolve one function "
                          "parameter type",
                          parameter_decl->get_source_span());
                return false;
            }
            const CoreIrType *parameter_type =
                get_or_create_type(parameter_semantic_type);
            if (parameter_type == nullptr) {
                add_error("core ir generation does not support this parameter type",
                          parameter_decl->get_source_span());
                return false;
            }
            auto *parameter = function.create_parameter<CoreIrParameter>(
                parameter_type, parameter_name);
            parameter->set_source_span(parameter_decl->get_source_span());
            if (parameter_symbol != nullptr) {
                auto *stack_slot = function.create_stack_slot<CoreIrStackSlot>(
                    next_stack_slot_name(parameter_name + ".addr"),
                    parameter_type,
                    get_default_alignment(parameter_type));
                auto *store = current_block_->create_instruction<CoreIrStoreInst>(
                    void_type_, parameter, stack_slot);
                store->set_source_span(parameter_decl->get_source_span());
                local_bindings_[parameter_symbol] =
                    ValueBinding{nullptr, stack_slot, nullptr};
            }
        }
        return true;
    }

    bool declare_function(const FunctionDecl &function_decl) {
        const auto *function_symbol = get_symbol_binding(&function_decl);
        if (function_symbol == nullptr ||
            function_symbol->get_type() == nullptr ||
            strip_qualifiers(function_symbol->get_type()) == nullptr ||
            strip_qualifiers(function_symbol->get_type())->get_kind() !=
                SemanticTypeKind::Function) {
            add_error("core ir generation expected a resolved function type",
                      function_decl.get_source_span());
            return false;
        }

        const CoreIrType *function_type_base =
            get_or_create_type(function_symbol->get_type());
        const auto *function_type =
            dynamic_cast<const CoreIrFunctionType *>(function_type_base);
        if (function_type == nullptr) {
            add_error("core ir generation could not lower function type",
                      function_decl.get_source_span());
            return false;
        }

        std::string function_name = function_decl.get_name();
        if (!function_decl.get_asm_label().empty()) {
            function_name = function_decl.get_asm_label();
        }

        if (CoreIrFunction *existing_function =
                module_->find_function(function_name);
            existing_function != nullptr) {
            function_bindings_[function_symbol] = existing_function;
            return true;
        }

        bool is_always_inline = false;
        if (const auto *attributes =
                semantic_model_.get_function_attributes(&function_decl);
            attributes != nullptr) {
            for (SemanticFunctionAttribute attribute : *attributes) {
                if (attribute == SemanticFunctionAttribute::AlwaysInline) {
                    is_always_inline = true;
                    break;
                }
            }
        }
        auto *function = module_->create_function<CoreIrFunction>(
            function_name, function_type, function_decl.get_is_static(),
            is_always_inline);
        function_bindings_[function_symbol] = function;
        return true;
    }

    bool declare_global_symbol(const SemanticSymbol *symbol,
                               const std::string &symbol_name,
                               SourceSpan source_span,
                               bool is_static) {
        if (symbol == nullptr || symbol->get_type() == nullptr) {
            add_error("core ir generation could not resolve top-level variable type",
                      source_span);
            return false;
        }
        if (global_bindings_.find(symbol) != global_bindings_.end()) {
            return true;
        }

        const CoreIrType *declared_type = get_or_create_type(symbol->get_type());
        if (declared_type == nullptr) {
            add_error("core ir generation does not support this top-level variable type",
                      source_span);
            return false;
        }

        auto *global = module_->create_global<CoreIrGlobal>(
            symbol_name, declared_type, nullptr, is_static, false);
        global_bindings_[symbol] = ValueBinding{nullptr, nullptr, global};
        return true;
    }

    bool declare_global_var(const VarDecl &var_decl) {
        return declare_global_symbol(get_symbol_binding(&var_decl),
                                     var_decl.get_name(),
                                     var_decl.get_source_span(),
                                     var_decl.get_is_static());
    }

    bool declare_global_const(const ConstDecl &const_decl) {
        const SemanticSymbol *symbol = get_symbol_binding(&const_decl);
        if (!should_materialize_global_const(symbol)) {
            return true;
        }
        return declare_global_symbol(symbol, const_decl.get_name(),
                                     const_decl.get_source_span(), false);
    }

    bool append_constant_bytes(const CoreIrConstant *constant, const CoreIrType *type,
                               std::vector<std::uint8_t> &bytes,
                               SourceSpan source_span) {
        if (constant == nullptr || type == nullptr) {
            add_error("core ir generation could not serialize a constant",
                      source_span);
            return false;
        }

        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
            bytes.insert(bytes.end(), get_core_ir_type_size(type), 0);
            return true;
        }

        switch (type->get_kind()) {
        case CoreIrTypeKind::Integer: {
            const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(constant);
            if (int_constant == nullptr) {
                add_error("core ir generation expected an integer constant while "
                          "packing aggregate bytes",
                          source_span);
                return false;
            }
            const std::size_t byte_count = get_core_ir_type_size(type);
            std::uint64_t value = int_constant->get_value();
            for (std::size_t index = 0; index < byte_count; ++index) {
                bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
                value >>= 8;
            }
            return true;
        }
        case CoreIrTypeKind::Pointer:
            if (dynamic_cast<const CoreIrConstantNull *>(constant) == nullptr) {
                add_error("core ir generation currently supports only null pointer "
                          "constants while packing aggregate bytes",
                          source_span);
                return false;
            }
            bytes.insert(bytes.end(), get_core_ir_type_size(type), 0);
            return true;
        case CoreIrTypeKind::Array: {
            if (const auto *byte_string =
                    dynamic_cast<const CoreIrConstantByteString *>(constant);
                byte_string != nullptr) {
                const auto &string_bytes = byte_string->get_bytes();
                bytes.insert(bytes.end(), string_bytes.begin(), string_bytes.end());
                return true;
            }
            const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            const auto *array_type = static_cast<const CoreIrArrayType *>(type);
            if (aggregate == nullptr) {
                add_error("core ir generation expected an array constant while "
                          "packing aggregate bytes",
                          source_span);
                return false;
            }
            if (aggregate->get_elements().size() != array_type->get_element_count()) {
                add_error("core ir generation encountered a mismatched array "
                          "constant while packing aggregate bytes",
                          source_span);
                return false;
            }
            for (const CoreIrConstant *element : aggregate->get_elements()) {
                if (!append_constant_bytes(element, array_type->get_element_type(),
                                           bytes, source_span)) {
                    return false;
                }
            }
            return true;
        }
        case CoreIrTypeKind::Struct: {
            const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            const auto *struct_type = static_cast<const CoreIrStructType *>(type);
            if (aggregate == nullptr) {
                add_error("core ir generation expected a struct constant while "
                          "packing aggregate bytes",
                          source_span);
                return false;
            }
            if (aggregate->get_elements().size() !=
                struct_type->get_element_types().size()) {
                add_error("core ir generation encountered a mismatched struct "
                          "constant while packing aggregate bytes",
                          source_span);
                return false;
            }
            for (std::size_t index = 0;
                 index < aggregate->get_elements().size(); ++index) {
                if (!append_constant_bytes(aggregate->get_elements()[index],
                                           struct_type->get_element_types()[index],
                                           bytes, source_span)) {
                    return false;
                }
            }
            return true;
        }
        case CoreIrTypeKind::Float:
        case CoreIrTypeKind::Void:
        case CoreIrTypeKind::Function:
            add_error("core ir generation does not yet support packing this "
                      "constant type into aggregate bytes",
                      source_span);
            return false;
        }
        return false;
    }

    const CoreIrConstant *
    build_constant_from_bytes(const CoreIrType *type,
                              const std::vector<std::uint8_t> &bytes,
                              SourceSpan source_span) {
        if (type == nullptr) {
            add_error("core ir generation could not materialize bytes without a "
                      "target type",
                      source_span);
            return nullptr;
        }

        switch (type->get_kind()) {
        case CoreIrTypeKind::Integer: {
            const std::size_t byte_count = get_core_ir_type_size(type);
            if (bytes.size() < byte_count || byte_count > sizeof(std::uint64_t)) {
                add_error("core ir generation does not yet support materializing "
                          "an integer constant of this byte width",
                          source_span);
                return nullptr;
            }
            std::uint64_t value = 0;
            for (std::size_t index = 0; index < byte_count; ++index) {
                value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
            }
            return core_ir_context_->create_constant<CoreIrConstantInt>(type, value);
        }
        case CoreIrTypeKind::Pointer:
            if (std::all_of(bytes.begin(), bytes.end(),
                            [](std::uint8_t byte) { return byte == 0; })) {
                return core_ir_context_->create_constant<CoreIrConstantNull>(type);
            }
            add_error("core ir generation does not yet support non-null pointer "
                      "byte materialization",
                      source_span);
            return nullptr;
        case CoreIrTypeKind::Array: {
            const auto *array_type = static_cast<const CoreIrArrayType *>(type);
            if (array_type->get_element_type()->get_kind() == CoreIrTypeKind::Integer &&
                static_cast<const CoreIrIntegerType *>(
                    array_type->get_element_type())->get_bit_width() == 8 &&
                bytes.size() == array_type->get_element_count()) {
                return core_ir_context_->create_constant<CoreIrConstantByteString>(
                    type, bytes);
            }
            std::vector<const CoreIrConstant *> elements;
            const std::size_t element_size =
                get_core_ir_type_size(array_type->get_element_type());
            for (std::size_t index = 0; index < array_type->get_element_count();
                 ++index) {
                const std::size_t begin = index * element_size;
                const std::size_t end = begin + element_size;
                if (end > bytes.size()) {
                    add_error("core ir generation encountered truncated bytes "
                              "while materializing an array constant",
                              source_span);
                    return nullptr;
                }
                const CoreIrConstant *element =
                    build_constant_from_bytes(
                        array_type->get_element_type(),
                        std::vector<std::uint8_t>(bytes.begin() + begin,
                                                  bytes.begin() + end),
                        source_span);
                if (element == nullptr) {
                    return nullptr;
                }
                elements.push_back(element);
            }
            return core_ir_context_->create_constant<CoreIrConstantAggregate>(
                type, std::move(elements));
        }
        case CoreIrTypeKind::Struct: {
            const auto *struct_type = static_cast<const CoreIrStructType *>(type);
            std::vector<const CoreIrConstant *> elements;
            std::size_t offset = 0;
            for (const CoreIrType *element_type : struct_type->get_element_types()) {
                const std::size_t element_size = get_core_ir_type_size(element_type);
                if (offset + element_size > bytes.size()) {
                    add_error("core ir generation encountered truncated bytes "
                              "while materializing a struct constant",
                              source_span);
                    return nullptr;
                }
                const CoreIrConstant *element =
                    build_constant_from_bytes(
                        element_type,
                        std::vector<std::uint8_t>(bytes.begin() + offset,
                                                  bytes.begin() + offset +
                                                      element_size),
                        source_span);
                if (element == nullptr) {
                    return nullptr;
                }
                elements.push_back(element);
                offset += element_size;
            }
            return core_ir_context_->create_constant<CoreIrConstantAggregate>(
                type, std::move(elements));
        }
        case CoreIrTypeKind::Float:
            return build_float_constant_from_bytes(
                static_cast<const CoreIrFloatType *>(type), bytes, source_span);
        case CoreIrTypeKind::Void:
        case CoreIrTypeKind::Function:
            add_error("core ir generation does not yet support materializing "
                      "this constant type from bytes",
                      source_span);
            return nullptr;
        }
        return nullptr;
    }

    const CoreIrConstant *build_global_constant_address(const Expr *expr,
                                                        const CoreIrType *target_type,
                                                        SourceSpan source_span) {
        if (expr == nullptr || target_type == nullptr ||
            target_type->get_kind() != CoreIrTypeKind::Pointer) {
            add_error("core ir generation expected a pointer target for a global "
                      "address constant",
                      source_span);
            return nullptr;
        }

        if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(expr);
            identifier != nullptr) {
            const SemanticSymbol *symbol = get_symbol_binding(identifier);
            if (symbol == nullptr) {
                add_error("core ir generation could not resolve global address "
                          "identifier: " + identifier->get_name(),
                          identifier->get_source_span());
                return nullptr;
            }
            if (strip_qualifiers(symbol->get_type()) != nullptr &&
                strip_qualifiers(symbol->get_type())->get_kind() ==
                    SemanticTypeKind::Function) {
                CoreIrFunction *function =
                    ensure_function_binding(symbol, identifier->get_source_span());
                if (function == nullptr) {
                    return nullptr;
                }
                return core_ir_context_->create_constant<CoreIrConstantGlobalAddress>(
                    target_type, function);
            }
            ValueBinding *binding = find_value_binding(symbol);
            if (binding == nullptr || binding->global == nullptr) {
                add_error("core ir generation expected a predeclared global for "
                          "this address constant",
                          identifier->get_source_span());
                return nullptr;
            }
            return core_ir_context_->create_constant<CoreIrConstantGlobalAddress>(
                target_type, binding->global);
        }

        if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
            member_expr != nullptr) {
            const SemanticType *owner_type = get_member_owner_type(
                get_node_type(member_expr->get_base()),
                member_expr->get_operator_text());
            if (owner_type == nullptr) {
                add_error("core ir generation could not resolve member owner type "
                          "for a global address constant",
                          member_expr->get_source_span());
                return nullptr;
            }
            const std::optional<std::size_t> field_index =
                find_aggregate_field_index(owner_type, member_expr->get_member_name());
            const std::optional<detail::AggregateFieldLayout> field_layout =
                field_index.has_value()
                    ? detail::get_aggregate_field_layout(owner_type, *field_index)
                    : std::nullopt;
            if (!field_layout.has_value()) {
                add_error("core ir generation could not resolve member layout for "
                          "a global address constant",
                          member_expr->get_source_span());
                return nullptr;
            }
            const CoreIrType *owner_core_type = get_or_create_type(owner_type);
            if (owner_core_type == nullptr) {
                return nullptr;
            }
            const CoreIrConstant *base =
                build_global_constant_address(
                    member_expr->get_base(),
                    core_ir_context_->create_type<CoreIrPointerType>(owner_core_type),
                    member_expr->get_source_span());
            if (base == nullptr) {
                return nullptr;
            }
            const auto *i32_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32);
            std::vector<const CoreIrConstant *> indices;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            const std::size_t element_index =
                strip_qualifiers(owner_type)->get_kind() == SemanticTypeKind::Union
                    ? 0
                    : field_layout->llvm_element_index;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(
                    i32_type, static_cast<std::uint64_t>(element_index)));
            return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                target_type, base, std::move(indices));
        }

        if (const auto *index_expr = dynamic_cast<const IndexExpr *>(expr);
            index_expr != nullptr) {
            const SemanticType *base_type =
                strip_qualifiers(get_node_type(index_expr->get_base()));
            if (base_type == nullptr) {
                add_error("core ir generation could not resolve indexed base type "
                          "for a global address constant",
                          index_expr->get_source_span());
                return nullptr;
            }
            if (base_type->get_kind() != SemanticTypeKind::Array) {
                add_error("core ir generation currently supports only array-based "
                          "constant address indexing for top-level initializers",
                          index_expr->get_source_span());
                return nullptr;
            }
            const std::optional<long long> constant_index =
                get_integer_constant_value(index_expr->get_index());
            if (!constant_index.has_value()) {
                add_error("core ir generation currently requires a constant index "
                          "for a global address initializer",
                          index_expr->get_index()->get_source_span());
                return nullptr;
            }
            const CoreIrType *base_core_type = get_or_create_type(base_type);
            if (base_core_type == nullptr) {
                return nullptr;
            }
            const CoreIrConstant *base =
                build_global_constant_address(
                    index_expr->get_base(),
                    core_ir_context_->create_type<CoreIrPointerType>(base_core_type),
                    index_expr->get_source_span());
            if (base == nullptr) {
                return nullptr;
            }
            const auto *i32_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32);
            std::vector<const CoreIrConstant *> indices;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(
                    i32_type, static_cast<std::uint64_t>(*constant_index)));
            return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                target_type, base, std::move(indices));
        }

        add_error("core ir generation does not yet support this global address "
                  "initializer expression",
                  source_span);
        return nullptr;
    }

    const CoreIrConstant *build_global_constant_pointer_value(
        const Expr *expr, const CoreIrType *target_type, SourceSpan source_span) {
        if (expr == nullptr || target_type == nullptr ||
            target_type->get_kind() != CoreIrTypeKind::Pointer) {
            add_error("core ir generation expected a pointer target for a global "
                      "pointer initializer",
                      source_span);
            return nullptr;
        }

        if (is_null_pointer_constant_expr(expr)) {
            return core_ir_context_->create_constant<CoreIrConstantNull>(
                target_type);
        }

        if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
            cast_expr != nullptr) {
            if (const std::optional<long long> integer_value =
                    get_integer_constant_value(cast_expr->get_operand());
                integer_value.has_value()) {
                const auto *intptr_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(64, false);
                const auto *integer_constant =
                    core_ir_context_->create_constant<CoreIrConstantInt>(
                        intptr_type,
                        static_cast<std::uint64_t>(*integer_value));
                return core_ir_context_->create_constant<CoreIrConstantCast>(
                    target_type, CoreIrCastKind::IntToPtr, integer_constant);
            }
            return build_global_constant_pointer_value(cast_expr->get_operand(),
                                                       target_type, source_span);
        }

        if (const auto *string_literal = dynamic_cast<const StringLiteralExpr *>(expr);
            string_literal != nullptr) {
            const std::string decoded_text =
                decode_string_literal_token(string_literal->get_value_text());
            const std::string global_key = string_literal->get_value_text();
            CoreIrGlobal *global = nullptr;
            const auto global_it = string_literal_globals_.find(global_key);
            if (global_it != string_literal_globals_.end()) {
                global = global_it->second;
            } else {
                const auto *char_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(8);
                const auto *array_type =
                    core_ir_context_->create_type<CoreIrArrayType>(
                        char_type, decoded_text.size() + 1);
                std::vector<std::uint8_t> bytes(decoded_text.begin(),
                                                decoded_text.end());
                bytes.push_back(0);
                const auto *initializer =
                    core_ir_context_->create_constant<CoreIrConstantByteString>(
                        array_type, std::move(bytes));
                global = module_->create_global<CoreIrGlobal>(
                    next_string_literal_name(), array_type, initializer, true,
                    true);
                string_literal_globals_[global_key] = global;
            }

            const auto *array_pointer_type =
                core_ir_context_->create_type<CoreIrPointerType>(global->get_type());
            const CoreIrConstant *base =
                core_ir_context_->create_constant<CoreIrConstantGlobalAddress>(
                    array_pointer_type, global);
            const auto *i32_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32);
            std::vector<const CoreIrConstant *> indices;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                target_type, base, std::move(indices));
        }

        if (const auto *unary_expr = dynamic_cast<const UnaryExpr *>(expr);
            unary_expr != nullptr && unary_expr->get_operator_text() == "&") {
            return build_global_constant_address(unary_expr->get_operand(),
                                                 target_type,
                                                 unary_expr->get_source_span());
        }

        if (const auto *unary_expr = dynamic_cast<const UnaryExpr *>(expr);
            unary_expr != nullptr && unary_expr->get_operator_text() == "&&") {
            if (current_function_ == nullptr) {
                add_error("core ir generation encountered a label address outside "
                          "a function",
                          unary_expr->get_source_span());
                return nullptr;
            }
            const auto *label_expr =
                dynamic_cast<const IdentifierExpr *>(unary_expr->get_operand());
            if (label_expr == nullptr) {
                add_error("core ir generation expected a label name after '&&'",
                          unary_expr->get_source_span());
                return nullptr;
            }
            CoreIrBasicBlock *label_block =
                record_address_taken_label(label_expr->get_name());
            if (label_block == nullptr) {
                return nullptr;
            }
            return core_ir_context_->create_constant<CoreIrConstantBlockAddress>(
                target_type, current_function_->get_name(),
                label_block->get_name());
        }

        if (const auto *binary_expr = dynamic_cast<const BinaryExpr *>(expr);
            binary_expr != nullptr &&
            (binary_expr->get_operator_text() == "+" ||
             binary_expr->get_operator_text() == "-")) {
            const Expr *base_expr = binary_expr->get_lhs();
            std::optional<long long> step =
                get_integer_constant_value(binary_expr->get_rhs());
            if (!step.has_value() && binary_expr->get_operator_text() == "+") {
                base_expr = binary_expr->get_rhs();
                step = get_integer_constant_value(binary_expr->get_lhs());
            }
            if (!step.has_value()) {
                add_error("core ir generation currently requires a constant "
                          "pointer offset for a global address initializer",
                          expr->get_source_span());
                return nullptr;
            }
            if (binary_expr->get_operator_text() == "-" &&
                base_expr == binary_expr->get_lhs()) {
                *step = -*step;
            }
            if (*step == 0) {
                return build_global_constant_pointer_value(
                    base_expr, target_type, base_expr->get_source_span());
            }
            const SemanticType *base_semantic_type =
                strip_qualifiers(get_node_type(base_expr));
            if (base_semantic_type != nullptr &&
                base_semantic_type->get_kind() == SemanticTypeKind::Array) {
                const CoreIrType *array_core_type =
                    get_or_create_type(base_semantic_type);
                if (array_core_type == nullptr) {
                    return nullptr;
                }
                const CoreIrConstant *base = build_global_constant_address(
                    base_expr,
                    core_ir_context_->create_type<CoreIrPointerType>(array_core_type),
                    base_expr->get_source_span());
                if (base == nullptr) {
                    return nullptr;
                }
                const auto *i32_type =
                    core_ir_context_->create_type<CoreIrIntegerType>(32);
                std::vector<const CoreIrConstant *> indices;
                indices.push_back(core_ir_context_->create_constant<CoreIrConstantInt>(
                    i32_type, 0));
                indices.push_back(core_ir_context_->create_constant<CoreIrConstantInt>(
                    i32_type, static_cast<std::uint64_t>(*step)));
                return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                    target_type, base, std::move(indices));
            }
            const CoreIrConstant *base =
                build_global_constant_pointer_value(base_expr, target_type,
                                                    base_expr->get_source_span());
            if (base == nullptr) {
                return nullptr;
            }
            const auto *i32_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32);
            std::vector<const CoreIrConstant *> indices;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(
                    i32_type, static_cast<std::uint64_t>(*step)));
            return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                target_type, base, std::move(indices));
        }

        const SemanticType *expr_semantic_type =
            strip_qualifiers(get_node_type(expr));
        if (expr_semantic_type != nullptr &&
            expr_semantic_type->get_kind() == SemanticTypeKind::Function) {
            return build_global_constant_address(expr, target_type, source_span);
        }
        if (expr_semantic_type != nullptr &&
            expr_semantic_type->get_kind() == SemanticTypeKind::Array) {
            const CoreIrType *array_core_type = get_or_create_type(expr_semantic_type);
            if (array_core_type == nullptr) {
                return nullptr;
            }
            const CoreIrConstant *base = build_global_constant_address(
                expr,
                core_ir_context_->create_type<CoreIrPointerType>(array_core_type),
                source_span);
            if (base == nullptr) {
                return nullptr;
            }
            const auto *i32_type =
                core_ir_context_->create_type<CoreIrIntegerType>(32);
            std::vector<const CoreIrConstant *> indices;
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            indices.push_back(
                core_ir_context_->create_constant<CoreIrConstantInt>(i32_type, 0));
            return core_ir_context_->create_constant<CoreIrConstantGetElementPtr>(
                target_type, base, std::move(indices));
        }

        add_error("core ir generation does not yet support this global pointer "
                  "initializer expression",
                  source_span);
        return nullptr;
    }

    const CoreIrConstant *
    build_global_scalar_initializer(const Expr *initializer,
                                    const SemanticType *declared_semantic_type,
                                    const CoreIrType *declared_type,
                                    SourceSpan source_span,
                                    bool is_extern) {
        declared_semantic_type = strip_qualifiers(declared_semantic_type);
        if (!normalize_single_element_initializer(
                initializer, "top-level scalar initializer", initializer)) {
            return nullptr;
        }

        if (initializer == nullptr) {
            if (is_extern) {
                return nullptr;
            }
            if (declared_type->get_kind() == CoreIrTypeKind::Integer) {
                return core_ir_context_->create_constant<CoreIrConstantInt>(
                    declared_type, 0);
            }
            if (declared_type->get_kind() == CoreIrTypeKind::Pointer) {
                return core_ir_context_->create_constant<CoreIrConstantNull>(
                    declared_type);
            }
            if (declared_type->get_kind() == CoreIrTypeKind::Float) {
                return core_ir_context_->create_constant<CoreIrConstantFloat>(
                    declared_type, "0.0");
            }
            add_error("core ir generation does not yet support zero-initializing "
                      "this global type",
                      source_span);
            return nullptr;
        }

        if (declared_type->get_kind() == CoreIrTypeKind::Float) {
            const std::optional<long double> constant_value =
                get_scalar_numeric_constant_value(initializer);
            if (constant_value.has_value()) {
                return core_ir_context_->create_constant<CoreIrConstantFloat>(
                    declared_type, format_scalar_float_literal(*constant_value));
            }
        }

        const std::optional<long long> constant_value =
            get_integer_constant_value(initializer);
        const std::optional<long long> converted_constant_value =
            constant_value.has_value()
                ? constant_value
                : constant_evaluator_.get_scalar_constant_value_as_integer(
                      initializer, declared_semantic_type, semantic_model_);
        if (!converted_constant_value.has_value()) {
            if (declared_type->get_kind() == CoreIrTypeKind::Integer) {
                const auto *cast_expr = dynamic_cast<const CastExpr *>(initializer);
                if (cast_expr != nullptr) {
                    const auto *pointer_type =
                        core_ir_context_->create_type<CoreIrPointerType>(
                            declared_type);
                    const CoreIrConstant *pointer_constant =
                        build_global_constant_pointer_value(
                            cast_expr->get_operand(), pointer_type,
                            cast_expr->get_operand() == nullptr
                                ? source_span
                                : cast_expr->get_operand()->get_source_span());
                    if (pointer_constant != nullptr) {
                        return core_ir_context_
                            ->create_constant<CoreIrConstantCast>(
                                declared_type, CoreIrCastKind::PtrToInt,
                                pointer_constant);
                    }
                }
            }
            if (declared_type->get_kind() == CoreIrTypeKind::Pointer) {
                const CoreIrConstant *pointer_constant =
                    build_global_constant_pointer_value(
                        initializer, declared_type,
                        initializer == nullptr ? source_span
                                               : initializer->get_source_span());
                if (pointer_constant != nullptr) {
                    return pointer_constant;
                }
                return nullptr;
            }
            add_error("core ir generation currently requires a constant scalar "
                      "initializer for top-level globals",
                      source_span);
            return nullptr;
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Integer) {
            return core_ir_context_->create_constant<CoreIrConstantInt>(
                declared_type,
                static_cast<std::uint64_t>(*converted_constant_value));
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Pointer &&
            *converted_constant_value == 0) {
            return core_ir_context_->create_constant<CoreIrConstantNull>(
                declared_type);
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Pointer) {
            const auto *intptr_type =
                core_ir_context_->create_type<CoreIrIntegerType>(64, false);
            const auto *integer_constant =
                core_ir_context_->create_constant<CoreIrConstantInt>(
                    intptr_type,
                    static_cast<std::uint64_t>(*converted_constant_value));
            return core_ir_context_->create_constant<CoreIrConstantCast>(
                declared_type, CoreIrCastKind::IntToPtr, integer_constant);
        }
        add_error("core ir generation does not yet support this top-level "
                  "scalar initializer type",
                  source_span);
        return nullptr;
    }

    const CoreIrConstant *build_zero_global_constant(
        const SemanticType *semantic_type, const CoreIrType *declared_type,
        SourceSpan source_span) {
        semantic_type = strip_qualifiers(semantic_type);
        if (declared_type == nullptr) {
            add_error("core ir generation could not resolve zero initializer type",
                      source_span);
            return nullptr;
        }

        if (declared_type->get_kind() == CoreIrTypeKind::Integer) {
            return core_ir_context_->create_constant<CoreIrConstantInt>(
                declared_type, 0);
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Pointer) {
            return core_ir_context_->create_constant<CoreIrConstantNull>(
                declared_type);
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Float) {
            return core_ir_context_->create_constant<CoreIrConstantFloat>(
                declared_type, "0.0");
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Array) {
            if (dynamic_cast<const CoreIrArrayType *>(declared_type) == nullptr) {
                add_error("core ir generation expected a resolved array zero "
                          "initializer type",
                          source_span);
                return nullptr;
            }
            return core_ir_context_->create_constant<CoreIrConstantZeroInitializer>(
                declared_type);
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Struct) {
            const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(declared_type);
            if (struct_type == nullptr) {
                add_error("core ir generation expected a resolved aggregate zero "
                          "initializer type",
                          source_span);
                return nullptr;
            }
            return core_ir_context_->create_constant<CoreIrConstantZeroInitializer>(
                declared_type);
        }

        add_error("core ir generation does not yet support zero-initializing "
                  "this global type",
                  source_span);
        return nullptr;
    }

    // Global array constants must preserve one aggregate element per declared
    // slot even when the source initializer is flattened.
    const CoreIrConstant *build_global_array_constant_aggregate(
        const ArraySemanticType *array_semantic_type,
        const CoreIrArrayType *array_type, const InitListExpr *init_list,
        std::size_t &cursor, SourceSpan source_span) {
        if (array_semantic_type == nullptr || array_type == nullptr) {
            add_error("core ir generation could not resolve top-level array "
                      "initializer shape",
                      source_span);
            return nullptr;
        }

        const SemanticType *element_semantic_type =
            array_semantic_type->get_element_type();
        const CoreIrType *element_type = array_type->get_element_type();
        const auto *nested_array_semantic_type =
            dynamic_cast<const ArraySemanticType *>(
                strip_qualifiers(element_semantic_type));
        const auto *nested_array_type =
            dynamic_cast<const CoreIrArrayType *>(element_type);

        std::vector<const CoreIrConstant *> elements;
        elements.reserve(array_type->get_element_count());
        for (std::size_t index = 0; index < array_type->get_element_count();
             ++index) {
            const Expr *element_initializer =
                init_list != nullptr && cursor < init_list->get_elements().size()
                    ? init_list->get_elements()[cursor].get()
                    : nullptr;
            const SourceSpan element_source_span =
                element_initializer == nullptr ? source_span
                                               : element_initializer->get_source_span();

            const CoreIrConstant *element_constant = nullptr;
            if (nested_array_semantic_type != nullptr &&
                nested_array_type != nullptr) {
                const bool consumes_nested_initializer_directly =
                    consumes_array_subinitializer_directly(
                        element_initializer, nested_array_semantic_type);
                if (consumes_nested_initializer_directly) {
                    ++cursor;
                    element_constant = build_global_constant_for_initializer(
                        element_initializer, nested_array_semantic_type,
                        nested_array_type, element_source_span);
                } else {
                    element_constant = build_global_array_constant_aggregate(
                        nested_array_semantic_type, nested_array_type, init_list,
                        cursor, element_source_span);
                }
            } else {
                if (element_initializer != nullptr && init_list != nullptr) {
                    ++cursor;
                }
                element_constant = build_global_constant_for_initializer(
                    element_initializer, element_semantic_type, element_type,
                    element_source_span);
            }

            if (element_constant == nullptr) {
                return nullptr;
            }
            elements.push_back(element_constant);
        }

        return core_ir_context_->create_constant<CoreIrConstantAggregate>(
            array_type, std::move(elements));
    }

    const CoreIrConstant *build_global_constant_for_initializer(
        const Expr *initializer, const SemanticType *semantic_type,
        const CoreIrType *declared_type, SourceSpan source_span,
        std::size_t designator_depth = 0) {
        semantic_type = strip_qualifiers(semantic_type);
        if (semantic_type == nullptr || declared_type == nullptr) {
            add_error("core ir generation could not resolve top-level initializer "
                      "type",
                      source_span);
            return nullptr;
        }
        if (initializer != nullptr && is_zero_initializer_expr(initializer)) {
            return build_zero_global_constant(semantic_type, declared_type,
                                              initializer->get_source_span());
        }
        if (initializer == nullptr) {
            return build_zero_global_constant(semantic_type, declared_type,
                                              source_span);
        }

        if (semantic_type->get_kind() == SemanticTypeKind::Array) {
            const auto *array_semantic_type =
                static_cast<const ArraySemanticType *>(semantic_type);
            const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(declared_type);
            if (array_type == nullptr) {
                add_error("core ir generation expected an array Core IR type for "
                          "top-level array initializer",
                          source_span);
                return nullptr;
            }

            std::vector<std::uint8_t> string_bytes;
            bool matched_string_literal = false;
            if (!try_collect_char_array_string_initializer_bytes(
                    initializer, array_semantic_type->get_element_type(),
                    array_type->get_element_count(), "string literal initializer",
                    string_bytes, matched_string_literal)) {
                return nullptr;
            }
            if (matched_string_literal) {
                return core_ir_context_->create_constant<CoreIrConstantByteString>(
                    array_type, std::move(string_bytes));
            }

            if (initializer->get_kind() != AstKind::InitListExpr) {
                add_error("core ir generation currently requires an initializer "
                              "list for this top-level array initializer",
                          initializer->get_source_span());
                return nullptr;
            }
            const auto *init_list = static_cast<const InitListExpr *>(initializer);
            std::size_t cursor = 0;
            const CoreIrConstant *array_constant =
                build_global_array_constant_aggregate(
                    array_semantic_type, array_type, init_list, cursor,
                    source_span);
            if (array_constant == nullptr) {
                return nullptr;
            }
            if (cursor < init_list->get_elements().size()) {
                add_error("core ir generation encountered too many top-level "
                          "array initializer elements",
                          initializer->get_source_span());
                return nullptr;
            }
            return array_constant;
        }

        if (semantic_type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_semantic_type =
                static_cast<const StructSemanticType *>(semantic_type);
            const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(declared_type);
            if (struct_type == nullptr) {
                add_error("core ir generation expected a struct Core IR type for "
                          "top-level struct initializer",
                          source_span);
                return nullptr;
            }
            const detail::AggregateLayoutInfo layout =
                detail::compute_aggregate_layout(struct_semantic_type);
            std::vector<const CoreIrConstant *> element_values(
                struct_type->get_element_types().size(), nullptr);
            std::vector<std::uint64_t> bit_field_storage_values(
                struct_type->get_element_types().size(), 0);
            std::vector<bool> bit_field_storage_initialized(
                struct_type->get_element_types().size(), false);

            if (!walk_struct_initializer_fields(
                    struct_semantic_type, layout, initializer, source_span,
                    "top-level struct initializer",
                    [&](std::size_t /*field_index*/,
                        const SemanticFieldInfo &field,
                        const detail::AggregateFieldLayout &field_layout,
                        const StructFieldInitializer &field_initializer,
                        SourceSpan field_source_span) -> bool {
                        const Expr *field_expr = field_initializer.expr;
                        if (field_layout.is_bit_field) {
                            const auto integer_constant =
                                field_expr == nullptr
                                    ? std::optional<long long>(0)
                                    : get_integer_constant_value(field_expr);
                            if (!integer_constant.has_value()) {
                                add_error("core ir generation currently requires an "
                                          "integer constant bit-field initializer",
                                          field_source_span);
                                return false;
                            }
                            bit_field_storage_values[field_layout.llvm_element_index] |=
                                (static_cast<std::uint64_t>(*integer_constant) &
                                 get_low_bit_mask(field_layout.bit_width))
                                << field_layout.bit_offset;
                            bit_field_storage_initialized
                                [field_layout.llvm_element_index] = true;
                            return true;
                        }

                        const CoreIrConstant *field_constant =
                            build_global_constant_for_initializer(
                                field_initializer.has_nested_designators()
                                    ? field_initializer.nested_designator_list
                                    : field_expr,
                                field.get_type(),
                                struct_type->get_element_types()
                                    [field_layout.llvm_element_index],
                                field_source_span,
                                field_initializer.has_nested_designators()
                                    ? field_initializer.nested_designator_depth
                                    : 0);
                        if (field_constant == nullptr) {
                            return false;
                        }
                        element_values[field_layout.llvm_element_index] =
                            field_constant;
                        return true;
                    },
                    designator_depth)) {
                return nullptr;
            }

            std::vector<const CoreIrConstant *> elements;
            elements.reserve(struct_type->get_element_types().size());
            for (std::size_t index = 0; index < struct_type->get_element_types().size();
                 ++index) {
                if (bit_field_storage_initialized[index]) {
                    elements.push_back(
                        core_ir_context_->create_constant<CoreIrConstantInt>(
                            struct_type->get_element_types()[index],
                            bit_field_storage_values[index]));
                    continue;
                }
                if (element_values[index] != nullptr) {
                    elements.push_back(element_values[index]);
                    continue;
                }
                const SemanticType *element_semantic_type =
                    index < layout.elements.size() ? layout.elements[index].type
                                                   : nullptr;
                const CoreIrConstant *zero_element =
                    build_zero_global_constant(
                        element_semantic_type, struct_type->get_element_types()[index],
                        source_span);
                if (zero_element == nullptr) {
                    return nullptr;
                }
                elements.push_back(zero_element);
            }

            return core_ir_context_->create_constant<CoreIrConstantAggregate>(
                struct_type, std::move(elements));
        }

        if (semantic_type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_semantic_type =
                static_cast<const UnionSemanticType *>(semantic_type);
            const auto *union_type =
                dynamic_cast<const CoreIrStructType *>(declared_type);
            if (union_type == nullptr || union_type->get_element_types().empty()) {
                add_error("core ir generation expected a union Core IR type for "
                          "top-level union initializer",
                          source_span);
                return nullptr;
            }
            const auto &fields = union_semantic_type->get_fields();
            if (fields.empty()) {
                return build_zero_global_constant(semantic_type, declared_type,
                                                  source_span);
            }

            const InitListExpr *init_list = nullptr;
            if (!normalize_single_element_union_initializer(
                    initializer, "top-level union initializer", init_list)) {
                return nullptr;
            }

            const auto &first_field = fields.front();
            const CoreIrType *first_field_type =
                get_or_create_type(first_field.get_type());
            if (first_field_type == nullptr) {
                return nullptr;
            }
            const Expr *field_initializer =
                init_list != nullptr && !init_list->get_elements().empty()
                    ? init_list->get_elements().front().get()
                    : nullptr;
            const CoreIrConstant *field_constant =
                build_global_constant_for_initializer(
                    field_initializer, first_field.get_type(), first_field_type,
                    field_initializer == nullptr ? source_span
                                                 : field_initializer
                                                       ->get_source_span());
            if (field_constant == nullptr) {
                return nullptr;
            }

            std::vector<std::uint8_t> field_bytes;
            if (!append_constant_bytes(field_constant, first_field_type, field_bytes,
                                       field_initializer == nullptr
                                           ? source_span
                                           : field_initializer->get_source_span())) {
                return nullptr;
            }
            const std::size_t union_storage_size =
                get_core_ir_type_size(union_type);
            if (field_bytes.size() > union_storage_size) {
                add_error("core ir generation encountered an oversized union "
                          "initializer field payload",
                          field_initializer == nullptr ? source_span
                                                       : field_initializer
                                                             ->get_source_span());
                return nullptr;
            }
            field_bytes.resize(union_storage_size, 0);
            return build_constant_from_bytes(union_type, field_bytes,
                                             source_span);
        }

        if (declared_type->get_kind() == CoreIrTypeKind::Float) {
            const std::optional<long double> scalar_value =
                get_scalar_numeric_constant_value(initializer);
            if (scalar_value.has_value()) {
                return core_ir_context_->create_constant<CoreIrConstantFloat>(
                    declared_type, format_scalar_float_literal(*scalar_value));
            }
        }

        const std::optional<long long> constant_value =
            get_integer_constant_value(initializer);
        const std::optional<long long> converted_constant_value =
            constant_value.has_value()
                ? constant_value
                : constant_evaluator_.get_scalar_constant_value_as_integer(
                      initializer, semantic_type, semantic_model_);
        if (declared_type->get_kind() == CoreIrTypeKind::Integer &&
            converted_constant_value.has_value()) {
            return core_ir_context_->create_constant<CoreIrConstantInt>(
                declared_type,
                static_cast<std::uint64_t>(*converted_constant_value));
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Integer) {
            const auto *cast_expr = dynamic_cast<const CastExpr *>(initializer);
            if (cast_expr != nullptr) {
                const auto *pointer_type =
                    core_ir_context_->create_type<CoreIrPointerType>(
                        declared_type);
                const CoreIrConstant *pointer_constant =
                    build_global_constant_pointer_value(
                        cast_expr->get_operand(), pointer_type,
                        cast_expr->get_operand() == nullptr
                            ? source_span
                            : cast_expr->get_operand()->get_source_span());
                if (pointer_constant != nullptr) {
                    return core_ir_context_
                        ->create_constant<CoreIrConstantCast>(
                            declared_type, CoreIrCastKind::PtrToInt,
                            pointer_constant);
                }
            }
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Pointer &&
            is_null_pointer_constant_expr(initializer)) {
            return core_ir_context_->create_constant<CoreIrConstantNull>(
                declared_type);
        }
        if (declared_type->get_kind() == CoreIrTypeKind::Pointer) {
            const CoreIrConstant *pointer_constant =
                build_global_constant_pointer_value(initializer, declared_type,
                                                    initializer->get_source_span());
            if (pointer_constant != nullptr) {
                return pointer_constant;
            }
            return nullptr;
        }

        add_error("core ir generation does not yet support this top-level "
                  "initializer expression",
                  initializer->get_source_span());
        return nullptr;
    }

    const CoreIrConstant *build_global_array_initializer(
        const VarDecl &var_decl, const ArraySemanticType &semantic_type,
        const CoreIrArrayType &declared_type) {
        return build_global_constant_for_initializer(
            var_decl.get_initializer(), &semantic_type, &declared_type,
            var_decl.get_source_span());
    }

    const CoreIrConstant *build_global_struct_initializer(
        const VarDecl &var_decl, const StructSemanticType &semantic_type,
        const CoreIrStructType &declared_type) {
        return build_global_constant_for_initializer(
            var_decl.get_initializer(), &semantic_type, &declared_type,
            var_decl.get_source_span());
    }

    const CoreIrConstant *
    build_global_initializer(const Expr *initializer,
                             const SemanticType *semantic_type,
                             const CoreIrType *declared_type,
                             SourceSpan source_span,
                             bool is_extern) {
        semantic_type = strip_qualifiers(semantic_type);
        if (semantic_type == nullptr || declared_type == nullptr) {
            add_error("core ir generation could not resolve top-level initializer "
                      "type",
                      source_span);
            return nullptr;
        }
        if (is_extern && initializer == nullptr) {
            return nullptr;
        }

        if (semantic_type->get_kind() == SemanticTypeKind::Array) {
            const auto *array_semantic_type =
                static_cast<const ArraySemanticType *>(semantic_type);
            const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(declared_type);
            if (array_type == nullptr) {
                add_error("core ir generation expected an array Core IR type for "
                          "top-level array initializer",
                          source_span);
                return nullptr;
            }
            return build_global_constant_for_initializer(
                initializer, semantic_type, array_type, source_span);
        }
        if (semantic_type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_semantic_type =
                static_cast<const StructSemanticType *>(semantic_type);
            const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(declared_type);
            if (struct_type == nullptr) {
                add_error("core ir generation expected a struct Core IR type for "
                          "top-level struct initializer",
                          source_span);
                return nullptr;
            }
            return build_global_constant_for_initializer(
                initializer, struct_semantic_type, struct_type, source_span);
        }
        if (semantic_type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_type =
                dynamic_cast<const CoreIrStructType *>(declared_type);
            if (union_type == nullptr) {
                add_error("core ir generation expected a union Core IR type for "
                          "top-level union initializer",
                          source_span);
                return nullptr;
            }
            return build_global_constant_for_initializer(
                initializer, semantic_type, union_type, source_span);
        }

        return build_global_scalar_initializer(initializer, semantic_type,
                                               declared_type, source_span,
                                               is_extern);
    }

    bool emit_global_var_decl(const VarDecl &var_decl) {
        const SemanticSymbol *symbol = get_symbol_binding(&var_decl);
        if (symbol == nullptr || symbol->get_type() == nullptr) {
            add_error("core ir generation could not resolve top-level variable type",
                      var_decl.get_source_span());
            return false;
        }
        ValueBinding *binding = find_value_binding(symbol);
        if (binding == nullptr || binding->global == nullptr) {
            add_error("core ir generation expected a predeclared global binding",
                      var_decl.get_source_span());
            return false;
        }
        const CoreIrType *declared_type = binding->global->get_type();

        const CoreIrConstant *initializer =
            build_global_initializer(var_decl.get_initializer(), symbol->get_type(),
                                     declared_type, var_decl.get_source_span(),
                                     var_decl.get_is_extern());
        if (compiler_context_.get_diagnostic_engine().has_error()) {
            return false;
        }
        binding->global->set_initializer(initializer);
        return true;
    }

    bool emit_global_const_decl(const ConstDecl &const_decl) {
        const SemanticSymbol *symbol = get_symbol_binding(&const_decl);
        if (!should_materialize_global_const(symbol)) {
            return true;
        }
        if (symbol == nullptr || symbol->get_type() == nullptr) {
            add_error("core ir generation could not resolve top-level constant type",
                      const_decl.get_source_span());
            return false;
        }
        ValueBinding *binding = find_value_binding(symbol);
        if (binding == nullptr || binding->global == nullptr) {
            add_error("core ir generation expected a predeclared global binding",
                      const_decl.get_source_span());
            return false;
        }
        const CoreIrType *declared_type = binding->global->get_type();

        const CoreIrConstant *initializer = build_global_initializer(
            const_decl.get_initializer(), symbol->get_type(), declared_type,
            const_decl.get_source_span(), false);
        if (compiler_context_.get_diagnostic_engine().has_error()) {
            return false;
        }
        binding->global->set_initializer(initializer);
        return true;
    }

    bool emit_function(const FunctionDecl &function_decl) {
        const auto *function_symbol = get_symbol_binding(&function_decl);
        if (function_symbol == nullptr ||
            function_symbol->get_type() == nullptr ||
            strip_qualifiers(function_symbol->get_type())->get_kind() !=
                SemanticTypeKind::Function) {
            add_error("core ir generation expected a resolved function type",
                      function_decl.get_source_span());
            return false;
        }

        CoreIrFunction *function = find_function_binding(function_symbol);
        if (function == nullptr) {
            add_error("core ir generation expected one predeclared function binding",
                      function_decl.get_source_span());
            return false;
        }
        current_function_ = function;
        current_function_return_type_ =
            function->get_function_type()->get_return_type();
        current_function_return_semantic_type_ =
            static_cast<const FunctionSemanticType *>(
                strip_qualifiers(function_symbol->get_type()))
                ->get_return_type();
        local_bindings_.clear();
        label_blocks_.clear();
        address_taken_label_blocks_.clear();
        local_stack_slot_name_counts_.clear();
        next_temp_id_ = 0;
        current_block_ = function->create_basic_block<CoreIrBasicBlock>("entry");

        if (!bind_function_parameters(function_decl, *function)) {
            current_function_ = nullptr;
            current_function_return_type_ = nullptr;
            current_function_return_semantic_type_ = nullptr;
            current_block_ = nullptr;
            return false;
        }
        if (!emit_stmt(function_decl.get_body())) {
            current_function_ = nullptr;
            current_function_return_type_ = nullptr;
            current_function_return_semantic_type_ = nullptr;
            current_block_ = nullptr;
            return false;
        }

        if (current_block_ != nullptr && !current_block_->get_has_terminator()) {
            if (current_function_return_type_ == void_type_) {
                current_block_->create_instruction<CoreIrReturnInst>(void_type_);
            } else {
                CoreIrValue *fallback_return = build_zero_constant_value(
                    current_function_return_semantic_type_,
                    current_function_return_type_,
                    function_decl.get_source_span());
                if (fallback_return == nullptr) {
                    current_function_ = nullptr;
                    current_function_return_type_ = nullptr;
                    current_function_return_semantic_type_ = nullptr;
                    current_block_ = nullptr;
                    return false;
                }
                current_block_->create_instruction<CoreIrReturnInst>(
                    void_type_, fallback_return);
            }
        }

        current_function_ = nullptr;
        current_function_return_type_ = nullptr;
        current_function_return_semantic_type_ = nullptr;
        current_block_ = nullptr;
        local_bindings_.clear();
        label_blocks_.clear();
        address_taken_label_blocks_.clear();
        local_stack_slot_name_counts_.clear();
        return true;
    }

    void collect_named_aggregate_definitions(
        const TranslationUnit &translation_unit) {
        named_struct_types_.clear();
        named_union_types_.clear();
        std::unordered_set<const SemanticType *> visited;
        for (const auto &decl : translation_unit.get_top_level_decls()) {
            if (decl == nullptr) {
                continue;
            }
            collect_named_aggregate_type(semantic_model_.get_node_type(decl.get()),
                                         visited);
        }
    }

    void collect_named_aggregate_type(
        const SemanticType *type,
        std::unordered_set<const SemanticType *> &visited) {
        type = strip_qualifiers(type);
        if (type == nullptr || !visited.insert(type).second) {
            return;
        }

        if (type->get_kind() == SemanticTypeKind::Pointer) {
            collect_named_aggregate_type(
                static_cast<const PointerSemanticType *>(type)->get_pointee_type(),
                visited);
            return;
        }
        if (type->get_kind() == SemanticTypeKind::Array) {
            collect_named_aggregate_type(
                static_cast<const ArraySemanticType *>(type)->get_element_type(),
                visited);
            return;
        }
        if (type->get_kind() == SemanticTypeKind::Function) {
            const auto *function_type =
                static_cast<const FunctionSemanticType *>(type);
            collect_named_aggregate_type(function_type->get_return_type(),
                                         visited);
            for (const SemanticType *parameter_type :
                 function_type->get_parameter_types()) {
                collect_named_aggregate_type(parameter_type, visited);
            }
            return;
        }
        if (type->get_kind() == SemanticTypeKind::Struct) {
            const auto *struct_type =
                static_cast<const StructSemanticType *>(type);
            if (!struct_type->get_name().empty() &&
                !struct_type->get_fields().empty()) {
                named_struct_types_[struct_type->get_name()] = struct_type;
            }
            for (const auto &field : struct_type->get_fields()) {
                collect_named_aggregate_type(field.get_type(), visited);
            }
            return;
        }
        if (type->get_kind() == SemanticTypeKind::Union) {
            const auto *union_type = static_cast<const UnionSemanticType *>(type);
            if (!union_type->get_name().empty() &&
                !union_type->get_fields().empty()) {
                named_union_types_[union_type->get_name()] = union_type;
            }
            for (const auto &field : union_type->get_fields()) {
                collect_named_aggregate_type(field.get_type(), visited);
            }
        }
    }

  public:
    explicit CoreIrBuildSession(CompilerContext &compiler_context)
        : compiler_context_(compiler_context),
          semantic_model_(*compiler_context.get_semantic_model()),
          core_ir_context_(std::make_unique<CoreIrContext>()) {
        void_type_ = core_ir_context_->create_type<CoreIrVoidType>();
    }

    std::unique_ptr<CoreIrBuildResult> Build() {
        const TranslationUnit *translation_unit =
            get_translation_unit(compiler_context_);
        if (translation_unit == nullptr) {
            add_error("core ir generation requires a translation unit AST root");
            return nullptr;
        }

        const std::filesystem::path input_path(compiler_context_.get_input_file());
        const std::string module_name =
            input_path.stem().empty() ? "module" : input_path.stem().string();
        module_ = core_ir_context_->create_module<CoreIrModule>(module_name);
        collect_named_aggregate_definitions(*translation_unit);

        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (const auto *function_decl =
                    dynamic_cast<const FunctionDecl *>(decl.get());
                function_decl != nullptr) {
                if (find_function_binding(get_symbol_binding(function_decl)) != nullptr) {
                    continue;
                }
                if (!declare_function(*function_decl)) {
                    return nullptr;
                }
            }
        }

        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (const auto *var_decl = dynamic_cast<const VarDecl *>(decl.get());
                var_decl != nullptr) {
                if (!declare_global_var(*var_decl)) {
                    return nullptr;
                }
                continue;
            }
            if (const auto *const_decl = dynamic_cast<const ConstDecl *>(decl.get());
                const_decl != nullptr) {
                if (!declare_global_const(*const_decl)) {
                    return nullptr;
                }
            }
        }

        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (const auto *var_decl = dynamic_cast<const VarDecl *>(decl.get());
                var_decl != nullptr) {
                if (!emit_global_var_decl(*var_decl)) {
                    return nullptr;
                }
                continue;
            }
            if (const auto *const_decl = dynamic_cast<const ConstDecl *>(decl.get());
                const_decl != nullptr) {
                if (!emit_global_const_decl(*const_decl)) {
                    return nullptr;
                }
                continue;
            }
            const auto *function_decl =
                dynamic_cast<const FunctionDecl *>(decl.get());
            if (function_decl != nullptr) {
                if (function_decl->get_body() == nullptr) {
                    continue;
                }
                if (!emit_function(*function_decl)) {
                    return nullptr;
                }
                continue;
            }
            if (dynamic_cast<const StructDecl *>(decl.get()) != nullptr ||
                dynamic_cast<const UnionDecl *>(decl.get()) != nullptr ||
                dynamic_cast<const EnumDecl *>(decl.get()) != nullptr ||
                dynamic_cast<const TypedefDecl *>(decl.get()) != nullptr) {
                continue;
            }

            add_error(
                "core ir generation currently supports only function "
                "declarations/definitions, type declarations, and scalar "
                "top-level variable declarations (decl kind=" +
                    std::to_string(static_cast<int>(decl->get_kind())) + ")",
                decl->get_source_span());
            return nullptr;
        }

        return std::make_unique<CoreIrBuildResult>(std::move(core_ir_context_),
                                                   module_);
    }
};

} // namespace

CoreIrBuildResult::CoreIrBuildResult(std::unique_ptr<CoreIrContext> context,
                                     CoreIrModule *module) noexcept
    : context_(std::move(context)), module_(module),
      analysis_manager_(std::make_unique<CoreIrAnalysisManager>()) {}

const CoreIrContext *CoreIrBuildResult::get_context() const noexcept {
    return context_.get();
}

CoreIrContext *CoreIrBuildResult::get_context() noexcept {
    return context_.get();
}

const CoreIrModule *CoreIrBuildResult::get_module() const noexcept {
    return module_;
}

CoreIrModule *CoreIrBuildResult::get_module() noexcept { return module_; }

const CoreIrAnalysisManager *CoreIrBuildResult::get_analysis_manager() const noexcept {
    return analysis_manager_.get();
}

CoreIrAnalysisManager *CoreIrBuildResult::get_analysis_manager() noexcept {
    return analysis_manager_.get();
}

void CoreIrBuildResult::invalidate_all_core_ir_analyses() noexcept {
    if (analysis_manager_ != nullptr) {
        analysis_manager_->invalidate_all();
    }
}

void CoreIrBuildResult::invalidate_core_ir_analyses(
    CoreIrFunction &function) noexcept {
    if (analysis_manager_ != nullptr) {
        analysis_manager_->invalidate(function);
    }
}

std::unique_ptr<CoreIrBuildResult> CoreIrBuilder::Build(CompilerContext &context) {
    if (context.get_ast_root() == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "core ir generation requires a completed AST");
        return nullptr;
    }
    if (context.get_semantic_model() == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "core ir generation requires a completed semantic model");
        return nullptr;
    }
    if (!context.get_semantic_model()->get_success()) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "core ir generation requires successful semantic analysis");
        return nullptr;
    }

    CoreIrBuildSession build_session(context);
    return build_session.Build();
}

} // namespace sysycc
