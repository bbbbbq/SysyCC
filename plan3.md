# CoreIrCanonicalizePass CFG 第二阶段增强计划

## 1. 背景与结论

假设 `plan1` 的分支条件第二阶段增强与 `plan2` 的地址表达式 / GEP 第二阶段增强都已完成，
下一步最适合做的是：

**把 `CoreIrCanonicalizePass` 做到“CFG 第二阶段增强”**。

当前 `Canonicalize` 在控制流层面只做了：

- 非入口 trampoline block 删除

这说明基础 CFG 归一已经有了第一步，但距离“更稳定、更利于后续优化/降级”的标准形态还差一层：

- 空块清理
- 单前驱 / 单后继线性块合并
- 无意义双分支目标归并

这一步优先于更激进的数据流优化，因为：

- 现有 IR 没有 PHI，很多 CFG 归一规则在当前模型下风险较低
- 规范化后的 CFG 会直接帮助 `ConstFold`、`DCE` 和 LLVM lowering
- 与 `plan1` / `plan2` 一样，容易形成“局部规则 + 专项测试 + 全量回归”的闭环

## 2. 目标

在 [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)
中扩展第二阶段 CFG canonicalization，只做**明确安全**的局部控制流归一。

目标效果：

- 减少无意义 basic block 数量
- 让块间跳转关系更直接
- 为后续 `DCE` 提供更容易清理的 CFG
- 不引入新的 IR 类型和全局分析框架

## 3. 范围与边界

### 3.1 In Scope

允许的 CFG 规范化：

1. **空跳转块删除**
   - 非入口块
   - 无副作用指令
   - 只剩无条件 jump
   - 在现有 trampoline 删除基础上，允许更稳定地收敛

2. **单前驱 / 单后继线性块合并**
   - 前块结尾是无条件 jump
   - 后块无其他前驱
   - 合并后不会引入语义歧义

3. **条件分支双目标相同收敛**
   - `condbr x, B, B` → `jump B`

4. **空终点块清理**
   - 对只做中转的 end/block 进一步压平

### 3.2 Out of Scope

本阶段明确不做：

- loop rotation
- switch CFG 重排
- critical edge splitting
- 需要支配分析的块重写
- 跨函数 CFG 优化
- 新的数据流分析
- 修改 branch condition canonicalization
- 修改地址 / GEP canonicalization

## 4. 推荐实现方式

### 4.1 总体策略

在 `CoreIrCanonicalizePass::Run()` 中，把 CFG 规范化做成一个独立 pass 段，
在 branch canonicalization 和 GEP canonicalization 之后运行。

推荐 helper：

- `collect_predecessor_counts(...)`
- `try_simplify_redundant_cond_jump(...)`
- `try_merge_linear_blocks(...)`
- `try_remove_empty_jump_block(...)`

### 4.2 关键规则

1. **`condbr` 双目标相同**
   - 直接替换成 `JumpInst`

2. **线性块合并**
   - 仅在后继块只有一个前驱时允许
   - 把后继块的非终结指令与终结指令移动到前驱块
   - 更新 parent
   - 删除原后继块

3. **块删除时机**
   - 只有在所有前驱都已重定向后才删除块
   - 删除前必须 `detach_operands()`

## 5. 代码落点

主要修改：

- [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)

重点验证：

- [tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp)
- [tests/ir/ir_break_continue/run.sh](/Users/caojunze424/code/SysyCC/tests/ir/ir_break_continue/run.sh)
- [tests/ir/ir_short_circuit/run.sh](/Users/caojunze424/code/SysyCC/tests/ir/ir_short_circuit/run.sh)
- [tests/ir/ir_while/run.sh](/Users/caojunze424/code/SysyCC/tests/ir/ir_while/run.sh)
- [tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp)

## 6. 测试计划

新增或强化以下回归：

1. `condbr` 真/假目标相同应降成 `jump`
2. trampoline block 继续正确删除
3. 单前驱 / 单后继块能够安全合并
4. `break/continue`、`short_circuit`、`while` 等现有 IR 形态继续成立
5. LLVM lowering 集成输出仍正确

最终验证：

- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_break_continue/run.sh`
- `tests/ir/ir_short_circuit/run.sh`
- `tests/ir/ir_while/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 7. 完成标准

完成标准：

- CFG 第二阶段 canonicalization 落地
- 新专项测试补齐
- 现有关键 CFG 集成测试通过
- `./tests/run_all.sh` 继续为 `PASS`

## 8. 风险与防线

### 风险

- 线性块合并时错误处理块 parent 或终结指令
- 前驱计数不准确导致删错块
- 过度压平 CFG，破坏现有 IR 形态断言

### 防线

- 只做单前驱 / 单后继的保守合并
- 所有删除前先重定向、再 `detach_operands`
- 关键 loop / short-circuit / break/continue 回归必须保留
