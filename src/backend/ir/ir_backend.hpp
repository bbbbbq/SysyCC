#pragma once

#include <string>
#include <vector>

#include "backend/ir/ir_kind.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc {

class SemanticType;

// Represents one typed IR value produced or consumed by the backend.
struct IRValue {
    std::string text;
    const SemanticType *type = nullptr;
};

// Represents one lowered function parameter in backend-independent form.
struct IRFunctionParameter {
    std::string name;
    const SemanticType *type = nullptr;
};

enum class IRFunctionAttribute : unsigned char {
    AlwaysInline,
};

// Defines the backend-independent interface used by IR generation.
class IRBackend {
  public:
    virtual ~IRBackend() = default;

    virtual IrKind get_kind() const noexcept = 0;
    virtual void begin_module() = 0;
    virtual void end_module() = 0;
    virtual void declare_global(const std::string &name,
                                const SemanticType *type) = 0;
    virtual void define_global(const std::string &name,
                               const SemanticType *type,
                               const std::string &initializer_text) = 0;
    virtual void declare_function(
        const std::string &name, const SemanticType *return_type,
        const std::vector<const SemanticType *> &parameter_types) = 0;
    virtual void begin_function(const std::string &name,
                                const SemanticType *return_type,
                                const std::vector<IRFunctionParameter> &parameters,
                                const std::vector<IRFunctionAttribute>
                                    &attributes) = 0;
    virtual void end_function() = 0;
    virtual std::string create_label(const std::string &hint) = 0;
    virtual void emit_label(const std::string &label) = 0;
    virtual void emit_branch(const std::string &target_label) = 0;
    virtual void emit_cond_branch(const IRValue &condition,
                                  const std::string &true_label,
                                  const std::string &false_label) = 0;
    virtual IRValue emit_integer_literal(int value) = 0;
    virtual std::string emit_alloca(const std::string &name,
                                    const SemanticType *type) = 0;
    virtual std::string emit_member_address(const std::string &base_address,
                                            const SemanticType *owner_type,
                                            std::size_t field_index,
                                            const SemanticType *field_type) = 0;
    virtual std::string emit_element_address(const std::string &base_address,
                                             const SemanticType *element_type,
                                             const IRValue &index_value) = 0;
    virtual IRValue emit_pointer_difference(const IRValue &lhs_pointer,
                                            const IRValue &rhs_pointer,
                                            const SemanticType *pointee_type,
                                            const SemanticType *result_type) = 0;
    virtual void emit_store(const std::string &address,
                            const IRValue &value) = 0;
    virtual IRValue emit_load(const std::string &address,
                              const SemanticType *type) = 0;
    virtual IRValue emit_binary(const std::string &op, const IRValue &lhs,
                                const IRValue &rhs,
                                const SemanticType *result_type) = 0;
    virtual IRValue emit_cast(const IRValue &value,
                              const SemanticType *target_type) = 0;
    virtual IRValue emit_integer_conversion(
        const IRValue &value, detail::IntegerConversionKind conversion_kind,
        const SemanticType *target_type) = 0;
    virtual IRValue emit_call(const std::string &callee,
                              const std::vector<IRValue> &arguments,
                              const SemanticType *return_type) = 0;
    virtual void emit_return(const IRValue &value) = 0;
    virtual void emit_return_void() = 0;
    virtual std::string get_output_text() const = 0;
};

} // namespace sysycc
