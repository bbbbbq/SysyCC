# Core IR 目录提升计划

## 摘要

当前 `src/backend/ir` 目录里只剩 `passes/` 一个顶层目录，但用户已经明确要求把
`passes/` 下的内容整体上提一级，恢复成直接在 `ir/` 下组织阶段目录。

目标结构如下：

```text
src/backend/ir/
├── build/
├── canonicalize/
├── const_fold/
├── dce/
├── lower/
└── shared/
```

并删除 `src/backend/ir/passes/` 本身。

## 关键改动

### 1. 最终目录布局

目标布局建议固定为：

```text
src/backend/ir/
├── build/
│   ├── build_core_ir_pass.hpp
│   └── build_core_ir_pass.cpp
├── canonicalize/
│   ├── core_ir_canonicalize_pass.hpp
│   └── core_ir_canonicalize_pass.cpp
├── const_fold/
│   ├── core_ir_const_fold_pass.hpp
│   └── core_ir_const_fold_pass.cpp
├── dce/
│   ├── core_ir_dce_pass.hpp
│   └── core_ir_dce_pass.cpp
├── lower/
│   ├── lower_ir_pass.hpp
│   ├── lower_ir_pass.cpp
│   ├── lowering/
│   │   ├── core_ir_target_backend.hpp
│   │   ├── core_ir_target_backend_factory.hpp
│   │   ├── core_ir_target_backend_factory.cpp
│   │   ├── llvm/
│   │   └── aarch64/
│   └── legacy/
│       ├── ir_backend.hpp
│       ├── ir_backend_factory.hpp
│       ├── ir_backend_factory.cpp
│       ├── ir_builder.hpp
│       ├── ir_builder.cpp
│       ├── gnu_function_attribute_lowering_handler.hpp
│       ├── gnu_function_attribute_lowering_handler.cpp
│       └── llvm/
└── shared/
    ├── core/
    ├── detail/
    ├── printer/
    ├── ir_kind.hpp
    └── ir_result.hpp
```

### 2. 文件归属规则

- `build/`
  - 只放 `BuildCoreIrPass` 这个阶段封装
- `canonicalize/`
  - 只放 `CoreIrCanonicalizePass`
- `const_fold/`
  - 只放 `CoreIrConstFoldPass`
- `dce/`
  - 只放 `CoreIrDcePass`
- `lower/`
  - 放 `LowerIrPass`
  - 放当前 target backend 工厂与具体 target lowering
  - 放仍然属于“旧 lowering 路径”的 legacy IR backend 封装
- `shared/`
  - 放所有多 pass 共用的 Core IR 模型和工具
  - 包括：
    - `core/`
    - `detail/`
    - `printer/`
    - `ir_kind.hpp`
    - `ir_result.hpp`

### 3. 明确不恢复旧的平铺顶层布局

这次只做“上提一层”，不回退到更早的 `src/backend/ir/core`、
`src/backend/ir/detail`、`src/backend/ir/printer` 这种平铺式顶层布局。

也就是说：

- 保留 `shared/core`
- 保留 `shared/detail`
- 保留 `shared/printer`
- 保留 `lower/legacy`
- 保留 `lower/lowering`
- 仅删除中间层 `passes/`

### 4. `ir.hpp` 的处理

`ir.hpp` 当前仍引用已删除的旧 `ir_pass.hpp`，已经属于过时入口。

这次直接做决策：
- 如果代码中没有任何真实引用，删除 `src/backend/ir/ir.hpp`
- 不再保留一个错误或过时的 umbrella header
- 如果后续确实需要统一入口，再在 `shared/` 下重新定义新的聚合头

默认按“删除旧 `ir.hpp`”执行。

### 5. include 路径与构建系统

所有源码、测试、文档中的路径统一切到新结构，例如：

- `backend/ir/shared/core/...`
- `backend/ir/shared/detail/...`
- `backend/ir/shared/printer/...`
- `backend/ir/shared/ir_kind.hpp`
- `backend/ir/shared/ir_result.hpp`
- `backend/ir/lower/lowering/...`
- `backend/ir/lower/legacy/...`
- `backend/ir/build/...`
- `backend/ir/canonicalize/...`
- `backend/ir/const_fold/...`
- `backend/ir/dce/...`

`CMakeLists.txt` 同步更新为新路径。  
不新增新的 target，不拆库；继续保持单一主可执行目标。

## 关键实现决策

### A. 为什么保留 `shared/`

- `core/` 不是 `build` 私有
- `detail/aggregate_layout` 不是 `lower` 私有
- `printer/` 也不是某一个 pass 私有
- 这些都被多个后端阶段共享，所以放到 `shared/` 是最清晰的所有权模型

### B. 为什么把 `ir_builder/ir_backend/llvm legacy` 放到 `lower/legacy/`

- 这套东西本质上是“旧 lowering 路径”
- 它不属于 `build/canonicalize/const_fold/dce`
- 放到 `lower/legacy/` 可以明确表示：
  - 它属于 lower 语义域
  - 但不是当前主路径的 Core IR target lowering 实现

### C. 不做的事

- 不改变 pass 执行顺序
- 不修改 `PassKind`
- 不改 `--stop-after=ir`
- 不改变 `BuildCoreIrPass/CoreIrConstFoldPass/CoreIrDcePass/LowerIrPass` 的行为
- 不把共享层硬塞进具体 pass 目录

## 测试与文档同步

### 测试

- 所有 include 路径切到新位置
- 所有直接编译源文件路径的 `run.sh` 同步改到新位置
- 现有已整理好的测试目录名保持不再改：
  - `ir_core_canonicalize_pass`
  - `ir_top_level_pass_pipeline_llvm`
  - `ir_lower_function_declaration`
  - `ir_lower_function_pointer_call`
  - `ir_lower_struct_index`
  - `ir_lower_aarch64_placeholder`

### 文档

同步更新：

- `doc/README.md`
- `doc/modules/compiler.md`
- `doc/modules/ir.md`
- `doc/modules/tests.md`
- `doc/modules/class-relationships.md`

要求：
- `IR` 模块目录树显示 `build/ canonicalize/ const_fold/ dce/ lower/ shared/`
- `shared/`、`lowering/`、`legacy/` 的归属关系写清楚
- 不再出现 `src/backend/ir/passes` 作为真实路径
- 历史说明可以保留，但只能明确标注为历史

## 验收标准

必须全部满足：

- `find src/backend/ir -maxdepth 1 -mindepth 1` 不再出现 `src/backend/ir/passes`
- `rg -n 'backend/ir/passes|src/backend/ir/passes' src tests doc CMakeLists.txt` 返回 0
- `cmake -S . -B build`
- `cmake --build build --parallel`
- `./tests/run_all.sh`
- `build/test_result.md` 为 `Overall: PASS`

## 假设

- 允许 `shared/` 作为共享层。
- 允许 `lower/legacy/` 作为旧 lowering 路径收纳层。
- `ir.hpp` 默认删除而不是迁移。
