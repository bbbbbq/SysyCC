#pragma once

#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "backend/ir/ir_backend.hpp"
#include "backend/ir/detail/ir_context.hpp"

namespace sysycc {

class SemanticType;

// Implements IRBackend by producing LLVM IR text.
class LlvmIrBackend : public IRBackend {
  private:
    std::ostringstream module_header_;
    std::ostringstream declarations_;
    std::ostringstream output_;
    std::ostringstream function_entry_allocas_;
    std::ostringstream function_body_;
    detail::IRContext ir_context_;
    std::unordered_map<std::string, int> address_counts_;
    std::unordered_set<std::string> declared_function_signatures_;
    std::unordered_set<std::string> declared_globals_;
    std::unordered_set<std::string> defined_globals_;
    std::unordered_map<std::string, std::string> string_literal_globals_;
    bool is_emitting_function_ = false;

    std::ostringstream &get_instruction_stream();

  public:
    IrKind get_kind() const noexcept override;
    void begin_module() override;
    void end_module() override;
    void declare_global(const std::string &name,
                        const SemanticType *type,
                        bool is_internal_linkage) override;
    void define_global(const std::string &name, const SemanticType *type,
                       const std::string &initializer_text,
                       bool is_internal_linkage) override;
    void define_raw_global(const std::string &name,
                           const std::string &llvm_type_text,
                           const std::string &initializer_text,
                           bool is_internal_linkage,
                           std::size_t explicit_alignment) override;
    void define_global_alias(const std::string &name,
                             const std::string &llvm_type_text,
                             const std::string &target_name,
                             bool is_internal_linkage) override;
    void declare_function(
        const std::string &name, const SemanticType *return_type,
        const std::vector<const SemanticType *> &parameter_types,
        bool is_variadic, bool is_internal_linkage) override;
    void begin_function(const std::string &name,
                        const SemanticType *return_type,
                        const std::vector<IRFunctionParameter> &parameters,
                        bool is_variadic,
                        const std::vector<IRFunctionAttribute>
                            &attributes,
                        bool is_internal_linkage) override;
    void end_function() override;
    std::string create_label(const std::string &hint) override;
    void emit_label(const std::string &label) override;
    void emit_branch(const std::string &target_label) override;
    void emit_cond_branch(const IRValue &condition, const std::string &true_label,
                          const std::string &false_label) override;
    IRValue emit_integer_literal(int value) override;
    IRValue emit_floating_literal(const std::string &value_text,
                                  const SemanticType *type) override;
    IRValue emit_string_literal(const std::string &value_text,
                                const SemanticType *type) override;
    std::string emit_alloca(const std::string &name,
                            const SemanticType *type) override;
    std::string emit_member_address(const std::string &base_address,
                                    const SemanticType *owner_type,
                                    std::size_t field_index,
                                    const SemanticType *field_type) override;
    std::string emit_element_address(const std::string &base_address,
                                     const SemanticType *element_type,
                                     const IRValue &index_value) override;
    IRValue emit_pointer_difference(const IRValue &lhs_pointer,
                                    const IRValue &rhs_pointer,
                                    const SemanticType *pointee_type,
                                    const SemanticType *result_type) override;
    void emit_store(const std::string &address, const IRValue &value) override;
    IRValue emit_load(const std::string &address,
                      const SemanticType *type) override;
    IRValue emit_binary(const std::string &op, const IRValue &lhs,
                        const IRValue &rhs,
                        const SemanticType *result_type) override;
    IRValue emit_cast(const IRValue &value,
                      const SemanticType *target_type) override;
    IRValue emit_integer_conversion(
        const IRValue &value, detail::IntegerConversionKind conversion_kind,
        const SemanticType *target_type) override;
    IRValue emit_call(const std::string &callee,
                      const std::vector<IRValue> &arguments,
                      const SemanticType *return_type,
                      const std::vector<const SemanticType *>
                          &parameter_types,
                      bool is_variadic) override;
    void emit_return(const IRValue &value) override;
    void emit_return_void() override;
    std::string get_output_text() const override;
};

} // namespace sysycc
