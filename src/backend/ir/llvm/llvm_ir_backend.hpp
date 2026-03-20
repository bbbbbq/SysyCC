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
    std::ostringstream declarations_;
    std::ostringstream output_;
    detail::IRContext ir_context_;
    std::unordered_map<std::string, int> address_counts_;
    std::unordered_set<std::string> declared_function_signatures_;

  public:
    IrKind get_kind() const noexcept override;
    void begin_module() override;
    void end_module() override;
    void declare_function(
        const std::string &name, const SemanticType *return_type,
        const std::vector<const SemanticType *> &parameter_types) override;
    void begin_function(const std::string &name,
                        const SemanticType *return_type,
                        const std::vector<IRFunctionParameter> &parameters) override;
    void end_function() override;
    std::string create_label(const std::string &hint) override;
    void emit_label(const std::string &label) override;
    void emit_branch(const std::string &target_label) override;
    void emit_cond_branch(const IRValue &condition, const std::string &true_label,
                          const std::string &false_label) override;
    IRValue emit_integer_literal(int value) override;
    std::string emit_alloca(const std::string &name,
                            const SemanticType *type) override;
    void emit_store(const std::string &address, const IRValue &value) override;
    IRValue emit_load(const std::string &address,
                      const SemanticType *type) override;
    IRValue emit_binary(const std::string &op, const IRValue &lhs,
                        const IRValue &rhs,
                        const SemanticType *result_type) override;
    IRValue emit_call(const std::string &callee,
                      const std::vector<IRValue> &arguments,
                      const SemanticType *return_type) override;
    void emit_return(const IRValue &value) override;
    void emit_return_void() override;
    std::string get_output_text() const override;
};

} // namespace sysycc
