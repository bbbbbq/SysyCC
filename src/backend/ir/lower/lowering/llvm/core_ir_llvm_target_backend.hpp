#pragma once

#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <string>
#include <vector>

#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrConstant;
class CoreIrFunction;
class CoreIrGlobal;
class CoreIrInstruction;
class CoreIrModule;
class CoreIrStackSlot;
class CoreIrType;
class CoreIrValue;

class CoreIrLlvmTargetBackend final : public CoreIrTargetBackend {
  private:
    struct EmittedGepChain {
        const CoreIrValue *root_base = nullptr;
        std::vector<const CoreIrValue *> indices;
    };

    std::ostringstream output_;
    std::size_t helper_id_ = 0;
    std::size_t next_value_id_ = 0;
    std::unordered_map<const CoreIrValue *, std::string> emitted_value_names_;
    std::unordered_map<const CoreIrValue *, EmittedGepChain> emitted_gep_chains_;
    std::unordered_map<const CoreIrStackSlot *, std::string> emitted_stack_slot_names_;
    std::unordered_set<std::string> used_stack_slot_names_;

    std::string next_helper_name(const std::string &prefix);
    std::string next_value_name();
    std::string get_emitted_value_name(const CoreIrValue *value);
    std::string get_emitted_stack_slot_name(const CoreIrStackSlot *stack_slot);
    std::string format_type(const CoreIrType *type) const;
    std::string format_constant(const CoreIrConstant *constant) const;
    std::string format_value_ref(const CoreIrValue *value);
    std::string format_pointer_ref(const CoreIrValue *value);
    bool append_instruction(std::string &text, const CoreIrInstruction &instruction,
                            DiagnosticEngine &diagnostic_engine);
    bool append_function(std::string &text, const CoreIrFunction &function,
                         DiagnosticEngine &diagnostic_engine);
    void append_global(std::string &text, const CoreIrGlobal &global) const;

  public:
    IrKind get_kind() const noexcept override;
    std::unique_ptr<IRResult>
    Lower(const CoreIrModule &module, DiagnosticEngine &diagnostic_engine) override;
};

} // namespace sysycc
