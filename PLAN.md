# Core IR Canonicalize 下一阶段测试计划

## 背景

旧的 `PLAN.md` 已经完成了上一轮 `CoreIrCanonicalizePass` 的 5 个实施阶段，
不再适合作为当前工作入口。

现在最值得做的不是继续扩规则，而是先把“等价地址形态输出一致”和
“CFG fixed-point 收敛稳定”用一组线性测试计划锁死。当前测试重点应围绕：

- 更强的 `global` / `stackslot` + structural `GEP` 地址标准形
- 更深层的 array/struct 混合地址链收敛
- 防止 unsafe / non-structural 地址链被错误扁平化
- 地址规范化与 CFG simplify 的阶段联动
- 最终 LLVM lowering 与运行期回归闭环

## 总目标

在不扩大本轮 rewrite 范围的前提下，优先把当前 canonicalize 规则变成
“可证明、可回归、可长期稳定演进”的测试闭环。

## 总验收标准

- 等价的结构化地址链在 raw Core IR 输出中收敛成同一种 canonical form
- 非结构化或语义敏感的地址链不会被错误合并
- 地址 canonicalize 暴露出来的 CFG 简化机会能稳定收敛
- LLVM lowering 与运行期行为不因 canonical form 收敛而回退
- 最终至少通过：
  - `tests/ir/ir_core_canonicalize_pass/run.sh`
  - `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
  - 相关新增 `tests/ir/*/run.sh`
  - 相关新增 `tests/run/*/run.sh`
  - `./tests/run_all.sh`

## 五步线性测试计划

### Step 1：锁定“等价结构化地址链输出一致”的 raw Core IR golden

这是下一步最值得先做的事情，因为它直接保护当前最核心的目标：
同一 `base object + canonical indices` 只保留一种输出形态。

- 主要落点：
  - `tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp`
- 测试重点：
  - `global` 基址上的零索引包装链
  - `stackslot` 基址上的零索引包装链
  - 同一结构化路径从不同入口形态进入后，最终打印结果一致
  - `load/store` 使用的地址在 canonicalize 后不再保留多余 wrapper
- 验收标准：
  - 同语义地址链的 raw printer 输出完全一致
  - 相关断言不只检查“能 flatten”，还检查“flatten 成同一种形态”

### Step 2：扩展更深层的 array/struct 混合地址链矩阵测试

只有先锁定 Step 1 的“输出一致性”，再扩到更深层混合链，才能知道新增规则
是在扩大覆盖面，而不是引入第二种等价形态。

- 主要落点：
  - `tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp`
  - `tests/ir/ir_multidim_index_decay_call/run.sh`
  - `tests/ir/ir_indexed_struct_member_access/run.sh`
- 测试重点：
  - `array -> struct -> array`
  - `struct -> array -> struct`
  - 多维数组后再取成员
  - `global` / `stackslot` 两类 base object 上的相同结构化路径
- 验收标准：
  - 更深层结构化链都收敛成单一 `base + indices`
  - 不同构造方式不会再产生分散的等价 GEP 形态

### Step 3：补齐 unsafe / non-structural 地址路径的负例保护

地址标准形越强，越需要尽早补齐“哪些不能动”的保护测试，否则后续扩展很容易
把 pointer arithmetic、union 风格访问或 reinterpret-like 路径错误折叠。

- 主要落点：
  - `tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp`
- 测试重点：
  - 缺少 leading zero member wrapper 的 `struct*` 指针算术
  - array decay 之后继续做非结构化偏移
  - union / alias-sensitive 地址路径
  - 动态索引参与的非结构化 follow-on `GEP`
- 验收标准：
  - 这些路径在 canonicalize 后仍保持原有语义边界
  - 新测试明确证明“保持不变”是预期，而不是未覆盖

### Step 4：补地址 canonicalize 与 CFG simplify 的 fixed-point 联动测试

前 3 步锁定地址形态后，下一步最值得验证的是阶段联动，因为地址/真值规范化会暴露
新的常量分支、冗余跳转和不可达块清理机会。

- 主要落点：
  - `tests/ir/ir_core_canonicalize_pass/ir_core_canonicalize_pass.cpp`
  - `tests/ir/ir_top_level_pass_pipeline_llvm/ir_top_level_pass_pipeline_llvm.cpp`
- 测试重点：
  - constant-condition branch collapse
  - `condbr x, B, B -> jump B`
  - trampoline/jump-threading 后的不可达块清理
  - rewrite 后 terminator / parent 关系保持正确
- 验收标准：
  - canonicalize 一次运行后就达到稳定形态
  - block 数量、终结指令种类和跳转目标都与预期一致

### Step 5：把新增 canonical form 提升到 top-level pipeline 与运行期回归

前 4 步验证的是 raw Core IR 与局部 pass 行为，最后一步才值得做端到端锁定，
把这些规范化结果变成长期稳定的用户可见保障。

- 主要落点：
  - `tests/ir/ir_top_level_pass_pipeline_llvm/*`
  - `tests/ir/ir_floating_comparison_and_condition/run.sh`
  - `tests/run/run_indexed_struct_member_access/*`
  - `tests/run/run_multidim_index_decay_call/*`
  - `tests/run/run_floating_comparison_and_condition/*`
- 测试重点：
  - canonical address form 在 LLVM lowering 后仍保持正确寻址语义
  - pointer / float truthiness 路径与 CFG 收敛后的 lowering 一致
  - 把已最小化的 fuzz 复现输入提升为确定性 IR / run 回归
- 验收标准：
  - top-level pipeline、专项 IR、专项 run 与 `./tests/run_all.sh` 全部通过
  - 新增 regression 能覆盖本轮 canonicalize 最关键的等价地址与 CFG 联动风险

## 执行顺序要求

这 5 步必须严格线性推进：

1. 先锁定“等价输出一致”
2. 再扩大深层结构化链覆盖
3. 再补负例边界，防止过度规范化
4. 再验证 CFG fixed-point 联动
5. 最后做 top-level pipeline 与运行期闭环

如果某一步发现新的 rewrite 空间，先把该步测试补完整，再决定是否进入实现，
不要在同一步里一边扩规则、一边扩验证面，避免混淆回归来源。
