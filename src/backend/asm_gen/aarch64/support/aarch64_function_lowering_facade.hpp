#pragma once

#include <optional>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_machine_lowering_state.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_abi_emission_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_call_return_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_global_data_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_scalar_lowering_context.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_context.hpp"
#include "common/source_span.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrBinaryInst;
class CoreIrCallInst;
class CoreIrCastInst;
class CoreIrCompareInst;
class CoreIrCondJumpInst;
class CoreIrConstantFloat;
class CoreIrLoadInst;
class CoreIrReturnInst;
class CoreIrStoreInst;
class CoreIrUnaryInst;
class DiagnosticEngine;

class AArch64LoweringFacadeServices
    : public AArch64MemoryAccessContext,
      public AArch64ConstantMaterializationContext {
  public:
    using FunctionState = AArch64FunctionLoweringState;

    ~AArch64LoweringFacadeServices() override = default;

    virtual DiagnosticEngine &diagnostic_engine() const = 0;
    virtual void record_symbol_definition(const std::string &name,
                                          AArch64SymbolKind kind,
                                          AArch64SectionKind section_kind,
                                          bool is_global_symbol) = 0;
    virtual void record_symbol_reference(const std::string &name,
                                         AArch64SymbolKind kind) = 0;
    virtual AArch64VirtualReg
    create_virtual_reg(AArch64MachineFunction &function,
                       const CoreIrType *type) const = 0;
    virtual AArch64FunctionAbiInfo
    classify_call(const CoreIrCallInst &call) const = 0;
    virtual const std::string &
    block_label(const CoreIrBasicBlock *block) const = 0;
    virtual const AArch64ValueLocation *
    lookup_value_location(const FunctionState &state,
                          const CoreIrValue *value) const = 0;
    virtual bool require_canonical_vreg(const FunctionState &state,
                                        const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;
    virtual bool require_canonical_memory_address(
        const FunctionState &state, const CoreIrValue *value,
        AArch64VirtualReg &out, std::size_t &offset) const = 0;
    virtual bool materialize_value(
        AArch64MachineBlock &machine_block, const CoreIrValue *value,
        const AArch64VirtualReg &target_reg, const FunctionState &state,
        AArch64ValueMaterializationContext &context) = 0;
    virtual bool materialize_noncanonical_value(
        AArch64MachineBlock &machine_block, const CoreIrValue *value,
        const AArch64VirtualReg &target_reg, const FunctionState &state,
        AArch64ValueMaterializationContext &context) = 0;
    virtual std::string
    resolve_branch_target_label(const FunctionState &state,
                                const CoreIrBasicBlock *predecessor,
                                const CoreIrBasicBlock *successor) const = 0;
    virtual void emit_debug_location(AArch64MachineBlock &machine_block,
                                     const SourceSpan &source_span,
                                     FunctionState &state) = 0;
    virtual bool is_position_independent() const = 0;
    virtual bool
    is_nonpreemptible_global_symbol(const std::string &name) const = 0;
    virtual bool
    is_nonpreemptible_function_symbol(const std::string &name) const = 0;
    virtual void append_load_from_incoming_stack_arg(
        AArch64MachineBlock &machine_block, const CoreIrType *type,
        const AArch64VirtualReg &target_reg, std::size_t offset,
        AArch64MachineFunction &function) = 0;
    virtual bool append_load_from_address(AArch64MachineBlock &machine_block,
                                          const CoreIrType *type,
                                          const AArch64VirtualReg &target_reg,
                                          const AArch64VirtualReg &address_reg,
                                          std::size_t offset,
                                          AArch64MachineFunction &function) = 0;
    virtual bool append_store_to_address(AArch64MachineBlock &machine_block,
                                         const CoreIrType *type,
                                         const AArch64VirtualReg &source_reg,
                                         const AArch64VirtualReg &address_reg,
                                         std::size_t offset,
                                         AArch64MachineFunction &function) = 0;
    virtual bool emit_memory_copy(AArch64MachineBlock &machine_block,
                                  const AArch64VirtualReg &destination_address,
                                  const AArch64VirtualReg &source_address,
                                  const CoreIrType *type,
                                  AArch64MachineFunction &function) = 0;
    virtual void apply_sign_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) = 0;
    virtual void apply_zero_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) = 0;
};

class AArch64GlobalDataLoweringFacade final
    : public AArch64GlobalDataLoweringContext {
  private:
    AArch64LoweringFacadeServices &services_;

  public:
    explicit AArch64GlobalDataLoweringFacade(
        AArch64LoweringFacadeServices &services);

    void record_symbol_definition(const std::string &name,
                                  AArch64SymbolKind kind,
                                  AArch64SectionKind section_kind,
                                  bool is_global_symbol) override;
    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override;
    void report_error(const std::string &message) override;
};

class AArch64FunctionPlanningFacade final
    : public AArch64FunctionPlanningContext {
  private:
    AArch64LoweringFacadeServices &services_;

  public:
    explicit AArch64FunctionPlanningFacade(
        AArch64LoweringFacadeServices &services);

    void report_error(const std::string &message) override;
    AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                         const CoreIrType *type) override;
    AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) override;
    AArch64FunctionAbiInfo
    classify_call(const CoreIrCallInst &call) const override;
};

class AArch64FunctionLoweringFacade final
    : public AArch64FunctionPlanningContext,
      public AArch64PhiPlanContext,
      public AArch64PhiCopyLoweringContext,
      public AArch64AbiEmissionContext,
      public AArch64ValueMaterializationContext,
      public AArch64MemoryValueLoweringContext,
      public AArch64FloatHelperLoweringContext,
      public AArch64MemoryInstructionLoweringContext,
      public AArch64ScalarLoweringContext,
      public AArch64CallReturnLoweringContext {
  public:
    using FunctionState = AArch64FunctionLoweringState;

  private:
    AArch64LoweringFacadeServices &services_;
    FunctionState &state_;

  public:
    AArch64FunctionLoweringFacade(AArch64LoweringFacadeServices &services,
                                  FunctionState &state);

    void report_error(const std::string &message) override;
    void report_error(const std::string &message) const override;

    AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                         const CoreIrType *type) override;
    AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) override;
    const CoreIrType *create_fake_pointer_type() const override;
    bool
    materialize_integer_constant(AArch64MachineBlock &machine_block,
                                 const CoreIrType *type, std::uint64_t value,
                                 const AArch64VirtualReg &target_reg,
                                 AArch64MachineFunction &function) override;
    AArch64FunctionAbiInfo
    classify_call(const CoreIrCallInst &call) const override;
    const std::string &
    block_label(const CoreIrBasicBlock *block) const override;

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override;
    bool try_get_value_vreg(const CoreIrValue *value,
                            AArch64VirtualReg &out) const override;
    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value,
                           const AArch64VirtualReg &target_reg) override;

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) override;
    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg,
                             long long offset,
                             AArch64MachineFunction &function) override;
    void record_symbol_reference(const std::string &name,
                                 AArch64SymbolKind kind) override;
    bool is_position_independent() const override;
    bool
    is_nonpreemptible_global_symbol(const std::string &name) const override;
    bool
    is_nonpreemptible_function_symbol(const std::string &name) const override;

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              AArch64VirtualReg &out) override;
    bool ensure_value_in_memory_address(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        AArch64VirtualReg &out) override;
    bool
    materialize_canonical_memory_address(AArch64MachineBlock &machine_block,
                                         const CoreIrValue *value,
                                         AArch64VirtualReg &out) override;
    std::size_t
    get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const override;

    void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &target_reg,
                                       unsigned physical_reg,
                                       AArch64VirtualRegKind reg_kind) override;
    void
    append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                unsigned physical_reg,
                                AArch64VirtualRegKind reg_kind,
                                const AArch64VirtualReg &source_reg) override;
    void append_load_from_incoming_stack_arg(
        AArch64MachineBlock &machine_block, const CoreIrType *type,
        const AArch64VirtualReg &target_reg, std::size_t offset,
        AArch64MachineFunction &function) override;
    bool append_load_from_address(AArch64MachineBlock &machine_block,
                                  const CoreIrType *type,
                                  const AArch64VirtualReg &target_reg,
                                  const AArch64VirtualReg &address_reg,
                                  std::size_t offset,
                                  AArch64MachineFunction &function) override;
    bool append_store_to_address(AArch64MachineBlock &machine_block,
                                 const CoreIrType *type,
                                 const AArch64VirtualReg &source_reg,
                                 const AArch64VirtualReg &address_reg,
                                 std::size_t offset,
                                 AArch64MachineFunction &function) override;
    bool materialize_incoming_stack_address(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &target_reg,
        std::size_t stack_offset, AArch64MachineFunction &function) override;
    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) override;
    bool emit_memory_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &destination_address,
                          const AArch64VirtualReg &source_address,
                          const CoreIrType *type,
                          AArch64MachineFunction &function) override;
    std::optional<AArch64VirtualReg>
    prepare_stack_argument_area(AArch64MachineBlock &machine_block,
                                std::size_t stack_arg_bytes,
                                AArch64MachineFunction &function) override;
    void finish_stack_argument_area(AArch64MachineBlock &machine_block,
                                    std::size_t stack_arg_bytes) override;
    void emit_direct_call(AArch64MachineBlock &machine_block,
                          const std::string &callee_name) override;
    bool emit_indirect_call(AArch64MachineBlock &machine_block,
                            const AArch64VirtualReg &callee_reg) override;
    void append_helper_call(AArch64MachineBlock &machine_block,
                            const std::string &symbol_name) override;
    void apply_sign_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) override;
    void apply_zero_extend_to_virtual_reg(
        AArch64MachineBlock &machine_block, const AArch64VirtualReg &dst_reg,
        const CoreIrType *source_type, const CoreIrType *target_type) override;
    AArch64VirtualReg
    promote_float16_to_float32(AArch64MachineBlock &machine_block,
                               const AArch64VirtualReg &source_reg,
                               AArch64MachineFunction &function) override;
    void
    demote_float32_to_float16(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &source_reg,
                              const AArch64VirtualReg &target_reg) override;
    bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                    const CoreIrConstantFloat &constant,
                                    const AArch64VirtualReg &target_reg,
                                    AArch64MachineFunction &function) override;
    bool
    is_promoted_stack_slot(const CoreIrStackSlot *stack_slot) const override;
    std::optional<AArch64VirtualReg> get_promoted_stack_slot_value(
        const CoreIrStackSlot *stack_slot) const override;
    void
    set_promoted_stack_slot_value(const CoreIrStackSlot *stack_slot,
                                  const AArch64VirtualReg &value_reg) override;
    void append_register_copy(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              const AArch64VirtualReg &source_reg) override;
    bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                     CoreIrBinaryOpcode opcode,
                                     const AArch64VirtualReg &lhs_reg,
                                     const AArch64VirtualReg &rhs_reg,
                                     const AArch64VirtualReg &dst_reg) override;
    bool emit_float128_compare_helper(
        AArch64MachineBlock &machine_block, CoreIrComparePredicate predicate,
        const AArch64VirtualReg &lhs_reg, const AArch64VirtualReg &rhs_reg,
        const AArch64VirtualReg &dst_reg,
        AArch64MachineFunction &function) override;
    bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                                   const CoreIrCastInst &cast,
                                   const AArch64VirtualReg &operand_reg,
                                   const AArch64VirtualReg &dst_reg,
                                   AArch64MachineFunction &function) override;
    DiagnosticEngine &diagnostic_engine() const override;
    const std::vector<std::size_t> *lookup_indirect_call_copy_offsets(
        const CoreIrCallInst &call) const override;
    const AArch64FunctionAbiInfo &function_abi_info() const override;
    const AArch64VirtualReg &indirect_result_address() const override;

    void emit_debug_location(AArch64MachineBlock &machine_block,
                             const SourceSpan &source_span,
                             FunctionState &state);
    std::string
    resolve_branch_target_label(const FunctionState &state,
                                const CoreIrBasicBlock *predecessor,
                                const CoreIrBasicBlock *successor) const;
    bool emit_load(AArch64MachineBlock &machine_block,
                   const CoreIrLoadInst &load, FunctionState &state);
    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, FunctionState &state);
    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary,
                     const FunctionState &state);
    bool emit_unary(AArch64MachineBlock &machine_block,
                    const CoreIrUnaryInst &unary, const FunctionState &state);
    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state);
    bool emit_cast(AArch64MachineBlock &machine_block,
                   const CoreIrCastInst &cast, const FunctionState &state);
    bool emit_call(AArch64MachineBlock &machine_block,
                   const CoreIrCallInst &call, const FunctionState &state);
    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state,
                        const CoreIrBasicBlock *current_block);
    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state);
    bool emit_address_of_stack_slot(AArch64MachineBlock &machine_block,
                                    const CoreIrAddressOfStackSlotInst &inst,
                                    const FunctionState &state);
    bool emit_address_of_global(AArch64MachineBlock &machine_block,
                                const CoreIrAddressOfGlobalInst &inst,
                                const FunctionState &state);
    bool emit_address_of_function(AArch64MachineBlock &machine_block,
                                  const CoreIrAddressOfFunctionInst &inst,
                                  const FunctionState &state);
    bool emit_getelementptr(AArch64MachineBlock &machine_block,
                            const CoreIrGetElementPtrInst &gep,
                            const FunctionState &state);
};

} // namespace sysycc
