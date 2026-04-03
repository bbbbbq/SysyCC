# CoreIrCanonicalizePass 地址表达式第二阶段增强计划

## 1. 背景与结论

在 `plan1` 完成后，我认为下一步最应该做的是：

**把 `CoreIrCanonicalizePass` 做到“地址表达式 / GEP 第二阶段增强”**。

也就是继续收敛 `GetElementPtr`、数组索引、结构体成员访问和地址包装链的形态，
让更多“语义等价但结构不同”的地址计算统一成更稳定的 canonical form。

我选择它而不是先做更激进 CFG 或更强 cast 归并，原因是：

- 当前 builder 和 lowering 已经大量依赖 `GEP` 形态
- 仓库里已有很多 index/member 相关 IR 回归，适合短周期闭环
- 这一步能直接降低 LLVM lowering 的复杂度
- 它也会给后续更强的 `ConstFold` / `DCE` / load-store 优化留出更干净的输入

## 2. 为什么这一步优先级最高

相对 `plan1` 的“分支条件第二阶段增强”，这一步更像下一条自然延长线：

- 分支条件已经有了第一阶段规范化能力
- 当前 `Canonicalize` 对地址表达式还很浅，只处理了零索引 no-op `GEP`
- 但项目里已经明显存在大量地址相关路径：
  - 数组索引
  - 结构体成员访问
  - 数组 decay
  - 全局地址初始化
  - 嵌套 `GEP`

因此下一步继续做地址 canonicalization，收益会更稳定，也更贴合当前代码基础。

## 3. 目标

在 [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp) 中新增一轮更强但仍保守的地址表达式规范化，只处理**明确安全**的已有 IR 形态。

目标效果：

- 让更多地址计算统一成单一 `GEP` 形态
- 减少多层无意义 `GEP` 包装
- 保持语义不变
- 不引入新的 IR 类型或全局分析

## 4. 范围与边界

### 4.1 In Scope

允许的规范化：

- `gep(base, 0)` 且结果类型不变
  - 继续保持删除
- 简单嵌套 `GEP`
  - `gep(gep(base, c1...), c2...)`
  - 在“索引拼接后语义明确不变”的条件下扁平化成一个 `gep`
- 数组 decay 形成的 `addr -> gep(0)` / `gep(0, idx)` 链
  - 统一成稳定的单层地址表达式
- 结构体成员访问导致的浅层地址包装
  - 在当前已有类型信息足够时统一到单个 `GEP`

### 4.2 Out of Scope

本阶段明确不做：

- 动态复杂索引重排
- 跨越 pointer arithmetic 语义边界的 GEP 合并
- 修改 builder 输出策略
- 新增 alias analysis
- 新增 load/store forwarding
- 修改 CFG 规则
- 修改 branch canonicalization

## 5. 推荐实现方式

### 5.1 总体策略

在当前 `canonicalize_nonterminator_instructions()` 里继续扩展 `GEP` 相关规则，
但只允许“局部、单块、显然等价”的合并。

建议拆出两个 helper，而不是继续把逻辑堆在 `canonicalize_gep()` 里：

- `is_noop_gep(...)`
- `try_flatten_gep_chain(...)`

### 5.2 可接受的扁平化规则

第一批建议只支持下面两种：

1. **零索引包装删除**
   - 保持当前实现

2. **简单常量前缀拼接**
   - 内层 `GEP` 的 base 不是 null
   - 内外两层索引都存在
   - 外层不是通过改变 pointee 语义去“跨对象偏移”
   - 至少前缀索引是常量且可安全直接拼接
   - 如果拿不准，就放弃规范化

### 5.3 代码落点

主要修改：

- [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)

重点验证的现有集成路径：

- [tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp)
- [tests/ir/ir_lower_struct_index/ir_lower_struct_index.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_lower_struct_index/ir_lower_struct_index.cpp)
- [tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp)

## 6. 测试计划

新增或强化以下回归：

1. 零索引 no-op `GEP` 继续能删除
2. `gep(gep(base,...),...)` 的简单常量链可扁平化
3. 结构体成员访问形成的浅层地址包装可收敛
4. 动态复杂 index 的嵌套 `GEP` 不应被错误合并
5. 集成 lowering 结果仍和当前 LLVM 预期一致

最终验证：

- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_lower_struct_index/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 7. 完成标准

1 小时到 1.5 小时内的完成标准：

- 地址表达式第二阶段 canonicalization 落地
- 专项回归补齐
- 关键集成测试通过
- 全量非 fuzz 回归保持 `PASS`
- 不引入新的 IR 类型或全局分析框架

## 8. 风险与防线

### 风险

- 扁平化 `GEP` 时跨越了不安全的类型语义边界
- 动态索引链被错误拼接
- 结构体成员访问地址被错误收敛，影响 lowering

### 防线

- 只处理局部、明确安全的嵌套 `GEP`
- 先支持简单常量前缀拼接，不一步做激进版本
- 每新增一种地址 rewrite，都补对应专项回归
- 最终必须跑完整非 fuzz 回归
