# Core IR Canonicalize 总路线图

## 当前状态

本路线图中的 5 个阶段现已全部在 `main` 落地：

- Plan 1：分支条件第二阶段增强
- Plan 2：地址表达式 / GEP 第二阶段增强
- Plan 3：CFG 第二阶段增强
- Plan 4：整数与布尔表达式第二阶段增强
- Plan 5：Stack Slot 内存访问规范化

当前这份文件保留为“已实施路线图”，用于说明 Canonicalize 是按什么顺序演进到当前形态的。

## 摘要

本路线图把 `CoreIrCanonicalizePass` 的后续演进拆成 5 个连续阶段，目标是逐步把
Core IR 归一到更稳定、更利于 `ConstFold`、`DCE` 和 `Lower` 处理的标准形态。

整体原则：

- 每一步都保持保守、局部、可验证
- 不一开始引入全局数据流分析或激进重写
- 每个阶段都形成“实现 + 专项测试 + 集成测试 + 全量回归”的闭环
- 优先做对现有 IR 管线收益高、风险低、且已有测试基础的规范化

## 总体路线

### Plan 1：分支条件第二阶段增强（已实现）

核心目标：

- 继续增强 `CondJump` 条件规范化
- 把更多“包了一层 cast / compare-of-compare / 布尔包装”的条件收敛成直接 `i1 compare`

重点能力：

- `condbr zext(compare_i1)`
- `condbr sext(compare_i1)`
- `condbr icmp ne (zext/sext compare), 0`
- `condbr icmp eq (zext/sext compare), 0`
- `condbr zext(!compare_i1)`
- `condbr sext(!compare_i1)`

关键约束：

- 只允许 `SignExtend` / `ZeroExtend` 真值透传
- 明确禁止 `truncate`、浮点条件、指针条件透传

价值：

- 直接放大 `CoreIrConstFoldPass` 和 `CoreIrDcePass` 的效果
- 风险最低，最适合作为分支规范化的第二层闭环

### Plan 2：地址表达式 / GEP 第二阶段增强（已实现）

核心目标：

- 继续收敛 `GetElementPtr`、数组索引、结构体成员访问和地址包装链
- 让更多地址计算统一成更稳定的 canonical form

重点能力：

- 继续删除零索引 no-op `GEP`
- 扁平化简单嵌套 `GEP`
- 统一数组 decay 形成的浅层地址链
- 收敛结构体成员访问的浅层地址包装

关键约束：

- 只处理局部、明确安全的嵌套 `GEP`
- 不做动态复杂索引重排
- 不跨越 pointer arithmetic 的语义边界

价值：

- 直接降低 LLVM lowering 的复杂度
- 为后续更强的 load/store 规范化留出更干净输入

### Plan 3：CFG 第二阶段增强（已实现）

核心目标：

- 把当前 CFG 规范化从“trampoline block 删除”推进到更完整的局部 CFG 收敛

重点能力：

- 空跳转块清理
- 单前驱 / 单后继线性块合并
- `condbr x, B, B -> jump B`
- 空终点块进一步压平

关键约束：

- 只做局部 CFG 归一
- 不引入支配分析
- 不做 loop rotation / switch 重排 / critical edge splitting

价值：

- 减少 basic block 噪音
- 让 `DCE`、`Lower` 和后续 IR 断言更加稳定

### Plan 4：整数与布尔表达式第二阶段增强（已实现）

核心目标：

- 统一普通非终结整数/布尔表达式形态
- 进一步提高 `ConstFold` 命中率

重点能力：

- 安全代数单位元：
  - `x + 0`
  - `x - 0`
  - `x * 1`
  - `x / 1`
  - `x | 0`
  - `x ^ 0`
- compare 常量换边和方向统一
- compare-of-compare / 布尔包装进一步归一

关键约束：

- 不做溢出敏感或激进代数变换
- 不碰浮点、指针表达式
- 不做跨块 value propagation

价值：

- 让 `ConstFold` 面对更标准的表达式树
- 为将来的更强 canonicalize 或 peephole 优化打基础

### Plan 5：Stack Slot 内存访问规范化（已实现）

核心目标：

- 把简单 stack slot 内存访问统一成更直接的 load/store 形态

重点能力：

- `load(addr_of_stackslot(slot)) -> direct stack-slot load`
- `store(value, addr_of_stackslot(slot)) -> direct stack-slot store`
- 删除因此变成死代码的 `addr_of_stackslot`

关键约束：

- 只处理 plain `addr_of_stackslot`
- 不处理 `GEP(addr_of_stackslot(...))`
- 不做 global 地址折叠
- 不做 store-to-load forwarding / alias analysis / memory SSA

价值：

- 减少同一语义的双重表示
- 为后续更强局部内存优化打基础

## 推荐执行顺序

推荐严格按以下顺序推进：

1. `plan1` 分支条件第二阶段增强
2. `plan2` 地址表达式 / GEP 第二阶段增强
3. `plan3` CFG 第二阶段增强
4. `plan4` 整数与布尔表达式第二阶段增强
5. `plan5` stack slot 内存访问规范化

这个顺序的理由是：

- 先统一分支条件
- 再统一地址表达式
- 再统一 CFG 结构
- 然后统一普通表达式
- 最后统一内存访问表示

这样每一步都在给后一步清理输入形态，而不是反过来放大复杂度。

## 每阶段统一验收标准

每个阶段都必须满足：

- 只扩展当前阶段目标，不顺手扩大到无关子系统
- 补齐对应专项回归
- 保留已有关键集成测试
- 至少跑：
  - `cmake -S . -B build`
  - `cmake --build build --parallel`
  - 该阶段对应的专项测试
  - `./tests/run_all.sh`
- 最终 `build/test_result.md` 为 `Overall: PASS`

## 总风险与控制策略

### 总风险

- 规范化规则过于激进，改变语义
- 不同阶段的 canonical form 互相冲突
- 测试只覆盖“跑通”，没有覆盖“形态正确”

### 控制策略

- 每一步只做一个子系统增强
- 每新增一种 rewrite，都必须补专项测试
- 优先写“禁止错误透传”的负例测试
- 如果某种重写是否安全有一点不确定，默认不做
- 以保守 canonicalization 为主，不把 canonicalize 变成真正的高收益优化器
