#pragma once

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"

namespace sysycc {

template <typename ReportErrorFn, typename CreateVirtualRegFn,
          typename CreatePointerVirtualRegFn, typename ClassifyCallFn>
class AArch64LambdaFunctionPlanningContext final
    : public AArch64FunctionPlanningContext {
  private:
    ReportErrorFn report_error_fn_;
    CreateVirtualRegFn create_virtual_reg_fn_;
    CreatePointerVirtualRegFn create_pointer_virtual_reg_fn_;
    ClassifyCallFn classify_call_fn_;

  public:
    AArch64LambdaFunctionPlanningContext(
        ReportErrorFn report_error_fn, CreateVirtualRegFn create_virtual_reg_fn,
        CreatePointerVirtualRegFn create_pointer_virtual_reg_fn,
        ClassifyCallFn classify_call_fn)
        : report_error_fn_(std::move(report_error_fn)),
          create_virtual_reg_fn_(std::move(create_virtual_reg_fn)),
          create_pointer_virtual_reg_fn_(
              std::move(create_pointer_virtual_reg_fn)),
          classify_call_fn_(std::move(classify_call_fn)) {}

    void report_error(const std::string &message) override {
        report_error_fn_(message);
    }

    AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                         const CoreIrType *type) override {
        return create_virtual_reg_fn_(function, type);
    }

    AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) override {
        return create_pointer_virtual_reg_fn_(function);
    }

    AArch64FunctionAbiInfo classify_call(
        const CoreIrCallInst &call) const override {
        return classify_call_fn_(call);
    }
};

template <typename ReportErrorFn, typename CreateVirtualRegFn,
          typename CreatePointerVirtualRegFn, typename ClassifyCallFn>
auto make_aarch64_function_planning_context(
    ReportErrorFn report_error_fn, CreateVirtualRegFn create_virtual_reg_fn,
    CreatePointerVirtualRegFn create_pointer_virtual_reg_fn,
    ClassifyCallFn classify_call_fn) {
    return AArch64LambdaFunctionPlanningContext<
        ReportErrorFn, CreateVirtualRegFn, CreatePointerVirtualRegFn,
        ClassifyCallFn>(std::move(report_error_fn),
                        std::move(create_virtual_reg_fn),
                        std::move(create_pointer_virtual_reg_fn),
                        std::move(classify_call_fn));
}

template <typename ReportErrorFn, typename BlockLabelFn>
class AArch64LambdaPhiPlanContext final : public AArch64PhiPlanContext {
  private:
    ReportErrorFn report_error_fn_;
    BlockLabelFn block_label_fn_;

  public:
    AArch64LambdaPhiPlanContext(ReportErrorFn report_error_fn,
                                BlockLabelFn block_label_fn)
        : report_error_fn_(std::move(report_error_fn)),
          block_label_fn_(std::move(block_label_fn)) {}

    void report_error(const std::string &message) const override {
        report_error_fn_(message);
    }

    const std::string &
    block_label(const CoreIrBasicBlock *block) const override {
        return block_label_fn_(block);
    }
};

template <typename ReportErrorFn, typename BlockLabelFn>
auto make_aarch64_phi_plan_context(ReportErrorFn report_error_fn,
                                   BlockLabelFn block_label_fn) {
    return AArch64LambdaPhiPlanContext<ReportErrorFn, BlockLabelFn>(
        std::move(report_error_fn), std::move(block_label_fn));
}

template <typename ReportErrorFn, typename RequireCanonicalVregFn,
          typename TryGetValueVregFn, typename MaterializeValueFn>
class AArch64LambdaPhiCopyLoweringContext final
    : public AArch64PhiCopyLoweringContext {
  private:
    ReportErrorFn report_error_fn_;
    RequireCanonicalVregFn require_canonical_vreg_fn_;
    TryGetValueVregFn try_get_value_vreg_fn_;
    MaterializeValueFn materialize_value_fn_;

  public:
    AArch64LambdaPhiCopyLoweringContext(
        ReportErrorFn report_error_fn,
        RequireCanonicalVregFn require_canonical_vreg_fn,
        TryGetValueVregFn try_get_value_vreg_fn,
        MaterializeValueFn materialize_value_fn)
        : report_error_fn_(std::move(report_error_fn)),
          require_canonical_vreg_fn_(std::move(require_canonical_vreg_fn)),
          try_get_value_vreg_fn_(std::move(try_get_value_vreg_fn)),
          materialize_value_fn_(std::move(materialize_value_fn)) {}

    void report_error(const std::string &message) override {
        report_error_fn_(message);
    }

    bool require_canonical_vreg(const CoreIrValue *value,
                                AArch64VirtualReg &out) const override {
        return require_canonical_vreg_fn_(value, out);
    }

    bool try_get_value_vreg(const CoreIrValue *value,
                            AArch64VirtualReg &out) const override {
        return try_get_value_vreg_fn_(value, out);
    }

    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value,
                           const AArch64VirtualReg &target_reg) override {
        return materialize_value_fn_(machine_block, value, target_reg);
    }
};

template <typename ReportErrorFn, typename RequireCanonicalVregFn,
          typename TryGetValueVregFn, typename MaterializeValueFn>
auto make_aarch64_phi_copy_lowering_context(
    ReportErrorFn report_error_fn,
    RequireCanonicalVregFn require_canonical_vreg_fn,
    TryGetValueVregFn try_get_value_vreg_fn,
    MaterializeValueFn materialize_value_fn) {
    return AArch64LambdaPhiCopyLoweringContext<
        ReportErrorFn, RequireCanonicalVregFn, TryGetValueVregFn,
        MaterializeValueFn>(std::move(report_error_fn),
                            std::move(require_canonical_vreg_fn),
                            std::move(try_get_value_vreg_fn),
                            std::move(materialize_value_fn));
}

} // namespace sysycc
