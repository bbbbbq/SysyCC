#pragma once

#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrPhiInst;
class CoreIrValue;

struct AArch64PhiEdgeKey {
    const CoreIrBasicBlock *predecessor = nullptr;
    const CoreIrBasicBlock *successor = nullptr;

    bool operator==(const AArch64PhiEdgeKey &other) const noexcept {
        return predecessor == other.predecessor && successor == other.successor;
    }
};

struct AArch64PhiEdgeKeyHash {
    std::size_t operator()(const AArch64PhiEdgeKey &key) const noexcept;
};

struct AArch64PhiCopyOp {
    const CoreIrPhiInst *phi = nullptr;
    const CoreIrValue *source_value = nullptr;
};

struct AArch64PhiEdgePlan {
    AArch64PhiEdgeKey edge;
    std::string edge_label;
    std::vector<AArch64PhiCopyOp> copies;
};

class AArch64PhiCopyLoweringContext {
  public:
    virtual ~AArch64PhiCopyLoweringContext() = default;

    virtual void report_error(const std::string &message) = 0;
    virtual bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;
    virtual bool try_get_value_vreg(const CoreIrValue *value,
                                    AArch64VirtualReg &out) const = 0;
    virtual bool materialize_value(AArch64MachineBlock &machine_block,
                                   const CoreIrValue *value,
                                   const AArch64VirtualReg &target_reg) = 0;
};

bool emit_parallel_phi_copies(AArch64MachineBlock &machine_block,
                              const AArch64PhiEdgePlan &plan,
                              AArch64MachineFunction &function,
                              AArch64PhiCopyLoweringContext &context);

} // namespace sysycc
