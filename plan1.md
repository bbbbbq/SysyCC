# CoreIrCanonicalizePass 分支条件第二阶段增强计划

## 1. 背景与目标

当前 `CoreIrCanonicalizePass` 已经完成第一阶段规范化，覆盖了：

- `CondJump` 条件规范化到 compare 驱动的 `i1`
- 安全的整数 `SignExtend/ZeroExtend/Truncate` 链清理
- 非入口 trampoline block 删除
- 零索引 no-op `GEP` 清理

下一步最应该做的是继续增强 **分支条件规范化**，把更多“包了一层 cast / compare-of-compare / 布尔包装”的 `CondJump` 条件进一步收敛成**直接 `i1 compare`**。

本阶段目标：

- 继续增强 `CondJump` 的 branch canonicalization
- 保持实现保守、安全、局部化
- 不扩展到 CFG / GEP / cast-chain 的其他子系统
- 在约 1 小时实现范围内形成完整闭环：实现 + 回归 + 全量验证

## 2. 为什么优先做这件事

- 这是当前 Core IR 管线里收益最高、风险最低的一步
- 它会直接放大 `CoreIrConstFoldPass` 和 `CoreIrDcePass` 的效果
- 相比数组 lowering、完整聚合对象 lowering，这项增强更容易在短时间内做出高质量、可验证的结果

## 3. 范围与边界

### 3.1 In Scope

只增强 `CoreIrCanonicalizePass` 的**分支条件规范化**，支持把以下条件统一成直接 `i1 compare`：

- `condbr zext(compare_i1)`
- `condbr sext(compare_i1)`
- `condbr icmp ne (zext/sext compare), 0`
- `condbr icmp eq (zext/sext compare), 0`
- `condbr zext(!compare_i1)`
- `condbr sext(!compare_i1)`

### 3.2 Out of Scope

本阶段明确不做：

- CFG 规则增强
- GEP / 地址表达式增强
- cast-chain 规则增强
- 新的 IR 类型或新 opcode
- 浮点条件规范化
- 指针条件规范化
- `truncate` 透传优化
- builder 生成策略调整
- 全局分析、支配分析、数据流分析

## 4. 实现设计

### 4.1 总体策略

在 [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp) 中新增一轮**更强但仍保守**的 branch canonicalization。

设计原则：

- 只处理“明确安全”的布尔包装
- 只做局部 rewrite，不引入全局推理
- 保持 `truncate` 为禁止透传对象
- 保持当前 `ConstFold` / `DCE` / `Lower` 职责边界不变

### 4.2 推荐实现步骤

1. 在现有分支条件规范化逻辑中补一个“布尔包装识别”层
   - 用来识别 compare 被 `zext/sext`、`icmp eq/ne ..., 0`、`logical-not` 等包裹的情况
2. 把识别出的包装重新归一成统一的 `i1 compare`
3. 只允许 `SignExtend` / `ZeroExtend` 作为真值保持 cast 透传
4. 显式拒绝 `Truncate`、浮点、指针条件透传
5. 复用现有 compare 反转逻辑，避免新引入一套平行规则

### 4.3 代码落点

主要修改：

- [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)

少量测试修改：

- [tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp)

如有必要，再补一条集成断言：

- [tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp)

## 5. 测试计划

新增或强化以下回归：

1. `zext(compare_i1)` 分支
   - 期望被收敛为直接 compare 条件
2. `sext(compare_i1)` 分支
   - 期望被收敛为直接 compare 条件
3. `icmp ne (zext compare), 0`
   - 期望被还原成原 compare
4. `icmp eq (zext compare), 0`
   - 期望被还原成反向 compare
5. `zext(!compare_i1)` / `sext(!compare_i1)`
   - 期望走 compare 反转逻辑
6. `truncate(...)` 条件
   - 明确不能被错误透传

最终验证命令：

- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 6. 完成标准

1 小时内的完成标准：

- 分支条件第二阶段增强落地
- 新专项回归补齐
- 集成回归仍然通过
- 全量 `./tests/run_all.sh` 仍为 `PASS`
- 不引入新的 IR 类型或复杂分析

## 7. 风险与防线

### 风险

- 布尔包装透传判断过于激进，导致语义改变
- compare-of-compare 收敛错误，破坏后续 lowering 预期
- `truncate` 被错误当作真值保持 cast

### 防线

- 只放开 `SignExtend` / `ZeroExtend`
- `Truncate` 保持显式禁止
- 每新增一种 rewrite，都要补对应专项测试
- 最终必须跑完整非 fuzz 回归
