# Core IR 后端 Pass 重构计划

## 摘要

将当前后端从单一 `IRGenPass -> CoreIrPipeline -> CoreIrPassManager -> Lower` 的黑盒流程，重构为由顶层 `PassManager` 统一管理的显式后端阶段：

`SemanticPass -> BuildCoreIrPass -> CoreIrCanonicalizePass -> CoreIrConstFoldPass -> CoreIrDcePass -> LowerIrPass`

本次重构的核心目标是删除 `CoreIrPassManager`，将 `Build`、各个 Core IR 优化、`Lower` 全部提升为顶层编译阶段，并让 `CompilerContext` 显式保存 Core IR 中间产物。

## 关键改动

### 1. 顶层 Pass 管线重构

- 删除 `IRGenPass` 作为单体后端入口。
- 删除 `CoreIrPassManager`。
- 删除 `CoreIrPipeline`，不再保留内部 `Build -> Optimize -> Lower` 调度。
- 在顶层 `PassManager` 中直接注册后端阶段：
  - `BuildCoreIrPass`
  - `CoreIrCanonicalizePass`
  - `CoreIrConstFoldPass`
  - `CoreIrDcePass`
  - `LowerIrPass`
- `PassKind` 扩展为更细粒度的后端阶段，至少包含：
  - `BuildCoreIr`
  - `CoreIrCanonicalize`
  - `CoreIrConstFold`
  - `CoreIrDce`
  - `LowerIr`

### 2. CompilerContext 中间产物建模

- 在 `CompilerContext` 中新增 Core IR 阶段产物存储：
  - `std::unique_ptr<CoreIrBuildResult> core_ir_build_result_;`
- 不只保存 `CoreIrModule*`，必须保存整个 `CoreIrBuildResult`，因为它同时持有：
  - `CoreIrContext`
  - `CoreIrModule`
- 各后端 pass 都通过 `CompilerContext` 读写这份 Core IR 中间结果。
- 继续保留 `std::unique_ptr<IRResult> ir_result_;` 作为最终 lowering 文本产物。

### 3. 各后端 Pass 的职责边界

- `BuildCoreIrPass`
  - 输入：AST + `SemanticModel`
  - 输出：`CoreIrBuildResult`
  - 不负责优化，不负责 lowering，不负责最终文本落盘
- `CoreIrCanonicalizePass`
  - 输入：`CoreIrBuildResult`
  - 负责轻量标准化整理，确保后续优化面对更稳定的 IR 形态
- `CoreIrConstFoldPass`
  - 输入：`CoreIrBuildResult`
  - 负责局部常量折叠和常量传播的最小闭环
- `CoreIrDcePass`
  - 输入：`CoreIrBuildResult`
  - 负责简单死代码删除和不可达基本块删除
- `LowerIrPass`
  - 输入：优化后的 `CoreIrBuildResult`
  - 调用 `CoreIrTargetBackend`
  - 输出：`IRResult`
  - 负责 `--dump-ir` 对应的最终 IR 文本落盘

### 4. CLI / stop-after / dump 语义

- 第一阶段保持用户可见语义尽量稳定：
  - `--stop-after=ir` 继续表示“停在后端最终 lowering 完成之后”
- 内部不立即暴露新的细粒度 `stop-after` 名称，避免一次性扩大 CLI 变化面。
- `--dump-ir` 仍由 `LowerIrPass` 负责，因为只有该阶段之后才有稳定的最终文本 `IRResult`。
- 如果后续需要调试 Core IR，再单独新增 Core IR dump 开关，不在本次重构中混入。

### 5. 文档落点

- 计划文档默认落在：
  - `.omx/plans/core-ir-pass-refactor.md`
- 实现完成后同步更新：
  - `doc/modules/ir.md`
  - `doc/modules/compiler.md`
  - `doc/modules/class-relationships.md`
  - `doc/README.md`

## 测试计划

- 新增或改造 `tests/ir/` 回归，至少覆盖：
  - `BuildCoreIrPass` 能单独构建 Core IR
  - 单个 Core IR 优化 pass 可独立运行
  - `LowerIrPass` 能在已有 Core IR 上单独执行
- 新增一条“多后端 pass 串联”回归，验证：
  - `BuildCoreIrPass -> CoreIrCanonicalizePass -> CoreIrConstFoldPass -> CoreIrDcePass -> LowerIrPass`
  - 最终输出与当前 LLVM IR 语义一致
- 保留一条失败路径测试，验证：
  - Core IR 缺失时优化 pass / lower pass 会明确失败
  - 诊断通过统一 `DiagnosticEngine` 输出
- 修改完成后运行非 `fuzz` 回归：
  - `./tests/run_all.sh`

## 重要接口与类型变更

- `PassKind` 从粗粒度后端阶段扩展为细粒度 Core IR 阶段
- 删除：
  - `IRGenPass`
  - `CoreIrPassManager`
  - `CoreIrPipeline`
- 新增：
  - `BuildCoreIrPass`
  - `CoreIrCanonicalizePass`
  - `CoreIrConstFoldPass`
  - `CoreIrDcePass`
  - `LowerIrPass`
- `CompilerContext` 新增：
  - `core_ir_build_result_`
  - 对应 getter / setter / clear 接口

## 假设与默认值

- 默认保留现有前端阶段顺序不变，只重构后端阶段。
- 默认不在本次重构里引入新的用户可见 CLI 选项。
- 默认 `--stop-after=ir` 仍表示“完成最终 IR lowering”。
- 默认第一批优化 pass 只做低风险 Core IR 变换，不在本次引入复杂全局数据流优化。
- 默认这次重构的目标是“阶段显式化与统一调度”，不是同时扩展新的后端能力。
