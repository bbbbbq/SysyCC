# Core IR Canonicalize V1 计划

## 摘要

为 `CoreIrCanonicalizePass` 落地第一版真实规范化逻辑，目标不是做激进优化，
而是把当前已经生成出的 Core IR 归一到更稳定、更利于后续 `ConstFold`、`DCE`
和 `Lower` 处理的标准形态。

第一版只做四类能力：

- 条件分支规范化
- 冗余整数 cast 合并
- 跳板块清理
- 已有地址表达式的保守规范化

## 关键改动

### 1. 条件分支规范化

- 让 `CoreIrCondJumpInst` 的条件尽量直接挂在 compare 结果上
- 处理当前最有价值的局部形态：
  - `condbr <non-i1 compare>` → 生成 `i1` compare 直接作为分支条件
  - `condbr !compare` → 反转 compare 谓词并直接分支
  - `condbr <integer value>` → 物化成 `icmp ne value, 0`
  - `condbr !<integer value>` → 物化成 `icmp eq value, 0`
- 第一版不做浮点条件和指针条件的重写

### 2. 冗余整数 cast 合并

- 仅处理：
  - `SignExtend`
  - `ZeroExtend`
  - `Truncate`
- 第一版支持：
  - 恒等 cast 删除
  - 同方向 extend 链合并
  - 连续 truncate 链合并
  - `trunc(sext(x))` / `trunc(zext(x))` 回到原位宽时折叠成 `x`
- 不处理浮点 cast、指针 cast 和复杂混合链

### 3. 跳板块清理

- 删除非入口、仅包含一条无条件 `JumpInst` 的 trampoline block
- 将所有前驱 terminator 直接改写到最终目标块
- 第一版不做激进的连续块合并和大范围 CFG 重排

### 4. 地址表达式规范化

- 只规范已经生成出来的地址表达式
- 第一版只做保守整理：
  - 删除零索引且结果类型不变的 `GetElementPtr`
  - 通过 use-rewrite 去掉作为外层地址基底的无意义零索引 GEP 包装
- 不做更深的语义反推，也不重建复杂嵌套 GEP

### 5. 支撑接口

- 为 `JumpInst` 和 `CondJumpInst` 增加最小必要的目标块 setter
- 供 canonicalize 在 CFG 清理时安全重定向分支目标

## 测试与验证

新增或强化的覆盖点：

- 已经 canonical 的 IR 不应被改坏
- `CondJump` 条件从 compare / logical-not(compare) / integer value 规范到 `i1 compare`
- 整数 cast 链合并与 identity cast 删除
- trampoline block 删除与分支重定向
- 零索引 GEP 规范化
- 顶层 pipeline 经过 canonicalize 后仍能正确 lower 到 LLVM IR

验证命令：

- `cmake -S . -B build`
- `cmake --build build --parallel`
- `tests/ir/ir_core_canonicalize_pass/run.sh`
- `tests/ir/ir_top_level_pass_pipeline_llvm/run.sh`
- `./tests/run_all.sh`

## 约束与默认值

- 第一版是“规范化”而不是“高收益优化”
- 不引入新的数据流分析、支配分析或 alias 分析
- 不修改 `CoreIrBuilder` 的总体生成策略
- 不改变 `ConstFold`、`DCE`、`Lower` 的职责边界
- 对拿不准是否完全语义等价的重写，默认不做
