# CoreIrCanonicalizePass Stack Slot 内存访问规范化计划

## 1. 背景与结论

在 `plan4` 完成后，下一步最适合做的是：

**把 `CoreIrCanonicalizePass` 扩展到 stack slot 内存访问规范化**。

原因是：

- 到这一步，分支、GEP、CFG、整数表达式都已经更稳定
- 当前 IR 里 `LoadInst` / `StoreInst` 同时支持：
  - 直接 `stack_slot`
  - 间接 `address`
- 这说明已经存在“同一语义的两种表示”
- 如果能把简单的 `addr_of_stackslot -> load/store` 统一收敛，就能为未来更强的局部内存优化打好基础

## 2. 目标

在 [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)
中新增一轮只面向 stack slot 的内存访问 canonicalization。

目标效果：

- 把简单 stack slot 访问统一成更直接的 load/store 形态
- 删除因此变成死代码的 `AddressOfStackSlotInst`
- 为未来的 store-to-load forwarding 或 scalar replacement 做准备

## 3. 范围与边界

### 3.1 In Scope

只处理下面两种模式：

1. `load(addr_of_stackslot(slot))`
   - 规范成直接 `load stack_slot`

2. `store(value, addr_of_stackslot(slot))`
   - 规范成直接 `store -> stack_slot`

并在 rewrite 后：

- 删除变成 dead 的 `AddressOfStackSlotInst`

### 3.2 Out of Scope

本阶段明确不做：

- global 地址 load/store 归一
- `GEP(addr_of_stackslot(...), ...)` 的内存折叠
- store-to-load forwarding
- alias analysis
- memory SSA
- scalar replacement

## 4. 推荐实现方式

### 4.1 总体策略

扩展 `canonicalize_nonterminator_instructions()`：

- 识别 `CoreIrLoadInst`
- 识别 `CoreIrStoreInst`
- 如果其地址是单纯 `CoreIrAddressOfStackSlotInst`，则改写为 stack-slot 直达形式

建议新增 helper：

- `try_canonicalize_stackslot_load(...)`
- `try_canonicalize_stackslot_store(...)`

### 4.2 关键规则

1. **只认 plain `addr_of_stackslot`**
   - 一旦地址中间还包着 `GEP` 或其他表达式，就不处理

2. **rewrite 后立即尝试删死的 `addr_of_stackslot`**
   - 复用已有 `erase_instruction_if_dead(...)`

3. **不改变非 stack slot 访问**
   - `addr_of_global`
   - `addr_of_function`
   - `GEP`
   - pointer deref
   全部保持不动

## 5. 代码落点

主要修改：

- [src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp)

重点验证：

- [tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp)
- [tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp](/Users/caojunze424/code/SysyCC/tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp)
- 现有涉及 stack slot 的 builder / runtime IR 用例

## 6. 测试计划

新增或强化以下回归：

1. `load(addr_of_stackslot(slot))` 能转成 direct stack-slot load
2. `store(v, addr_of_stackslot(slot))` 能转成 direct stack-slot store
3. rewrite 后死掉的 `addr_of_stackslot` 会被删掉
4. `GEP(addr_of_stackslot(...), ...)` 不会被误折叠
5. 顶层 LLVM pipeline 仍保持正确

最终验证：

- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 7. 完成标准

- stack slot 内存访问规范化落地
- 简单 stack slot load/store 统一成直达形式
- `addr_of_stackslot` 死代码能被清掉
- 全量非 fuzz 回归保持 `PASS`

## 8. 风险与防线

### 风险

- 把仍然有语义用途的 `addr_of_stackslot` 错删
- 错误处理 `GEP(addr_of_stackslot(...))` 为普通直达访问
- 影响 lowering 中对 stack slot 与 address 两种形式的兼容

### 防线

- 只处理“地址操作数正好是 `AddressOfStackSlotInst`”的情况
- rewrite 后只在无 use 时删死 `addr_of_stackslot`
- 保留 `GEP`、pointer deref 路径不动
- 通过 top-level pipeline 测试确保 lowering 未被破坏
