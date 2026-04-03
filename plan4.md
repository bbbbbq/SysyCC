# CoreIrCanonicalizePass 整数与布尔表达式第二阶段增强计划

## 1. 背景与结论

在 `plan3` 的 CFG 第二阶段增强完成后，下一步最适合做的是：

**把 `CoreIrCanonicalizePass` 做到“整数与布尔表达式第二阶段增强”**。

原因是：

- 到这一步，分支、GEP、CFG 形态都已经更稳定
- 当前还缺少一层对普通非终结表达式的统一
- 这会直接放大 `CoreIrConstFoldPass` 的命中率

相比更早去做内存数据流或更强 lowering，这一步更适合继续保持 canonicalize 的“局部、安全、可回归”风格。

## 2. 目标

在不引入新分析的前提下，统一更多整数/布尔表达式形态，使相同语义尽量收敛成更利于 `ConstFold` / `DCE` 的标准表达。

目标效果：

- 减少代数单位元噪音
- 统一布尔包装
- 统一 compare 常量位置和方向
- 为常量折叠创造更直接输入

## 3. 范围与边界

### 3.1 In Scope

允许的表达式规范化：

1. **安全代数单位元**
   - `x + 0 -> x`
   - `x - 0 -> x`
   - `x * 1 -> x`
   - `x / 1 -> x`
   - `x | 0 -> x`
   - `x ^ 0 -> x`

2. **安全布尔包装清理**
   - `!!x -> x`（仅当 `x` 已经是 `i1 compare`）
   - compare 的显式整数包装进一步收敛

3. **compare 方向统一**
   - 常量尽量放右侧
   - 交换比较方向时同步翻转谓词
   - 例如 `5 < x` 统一成 `x > 5`

4. **compare-of-compare 清理**
   - 仅处理明确安全的 `icmp eq/ne <i1-compare-as-int>, 0/1` 子集

### 3.2 Out of Scope

本阶段不做：

- 乘 0、减自身等更激进代数重写
- 溢出敏感变换
- 浮点表达式归一
- 指针算术归一
- 跨块 value propagation
- 新的数据流分析

## 4. 推荐实现方式

### 4.1 总体策略

在 `canonicalize_nonterminator_instructions()` 里扩展：

- `CoreIrBinaryInst`
- `CoreIrUnaryInst`
- `CoreIrCompareInst`

建议新增 helper：

- `try_canonicalize_binary_identity(...)`
- `try_canonicalize_compare_orientation(...)`
- `try_canonicalize_boolean_wrapper(...)`

### 4.2 关键规则

1. **先做 identity cleanup**
   - 删除最安全的代数单位元

2. **再做 compare 方向统一**
   - 把 compare 常量尽量移到 RHS
   - 同步翻转 `slt/sle/sgt/sge/ult/ule/ugt/uge`

3. **最后处理 compare-of-compare**
   - 只对当前 IR 类型清晰的 `i1` / `zext(sext(compare))` 变体放开

## 5. 代码落点

主要修改：

- [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)

重点验证：

- [tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp)
- [tests/ir/ir_core_const_fold_pass/ir_core_const_fold_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_const_fold_pass/ir_core_const_fold_pass.cpp)
- [tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp)

## 6. 测试计划

新增或强化以下回归：

1. `x + 0`, `x - 0`, `x * 1`, `x / 1`, `x | 0`, `x ^ 0`
2. compare 常量换边与谓词翻转
3. `!!compare` 清理
4. `icmp eq/ne (compare-as-int), 0/1` 的安全子集归一
5. 与 `ConstFold` 组合后仍保持更简洁 IR

最终验证：

- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_core_const_fold_pass/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 7. 完成标准

- 整数/布尔表达式第二阶段 canonicalization 落地
- 不引入溢出/符号语义错误
- `ConstFold` 命中率得到可观提升
- 全量非 fuzz 回归继续为 `PASS`

## 8. 风险与防线

### 风险

- 比较方向翻转错误
- 把非 `i1` 布尔包装清理得过于激进
- 单位元化简碰到溢出/有符号语义坑

### 防线

- 只做最保守 identity 规则
- compare 换边必须和谓词翻转绑定
- 所有 compare-of-compare 规则都补专项回归
