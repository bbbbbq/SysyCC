# AArch64 LLVM-IR Dynamic Library 改造计划

## Summary

目标是把当前内嵌在 `SysyCC` 编译器里的 AArch64 native backend，逐步改造成一个：

- 可独立构建的动态库
- 对外以 `LLVM IR` 为输入契约
- 同时接受 `.ll` 与 `.bc`
- 输出 `.s` 与 `.o`
- 能通过持续测试逐步补齐 LLVM IR 兼容性的独立 codegen 产品

本计划明确采用**激进路线**：

- 不再把 AArch64 backend 的长期核心输入绑定在 `CoreIR`
- 不再把它仅视为编译器内部的一个后端 pass
- 最终让编译器反过来成为它的一个上游，而不是它的宿主

## Execution Status

截至 `2026-04-11`，计划已经开始落地：

- `Phase 1` 的第一阶段已完成：
  - 新增共享库 target `libsysycc_aarch64_codegen`
  - 当前编译器继续保留 `aarch64_asm_gen_pass.cpp` 作为 thin adapter
  - 现有 `compiler -S/-c --backend=aarch64-native` 仍可通过共享库路径工作
- 当前共享库边界已经包含：
  - `src/backend/asm_gen/aarch64/model/`
  - `src/backend/asm_gen/aarch64/passes/`
  - `src/backend/asm_gen/aarch64/support/`
  - 以及动态库运行所需的最小诊断公共层
- 已验证的基线证据包括：
  - 工程可完整构建
  - `compiler` 真实链接 `libsysycc_aarch64_codegen`
  - AArch64 `asm` / `object` / `CLI -c` 目标回归继续通过
- `Phase 2` 已完成：
  - 新增共享库 public front door：
    - `src/backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp`
    - `src/backend/asm_gen/aarch64/api/aarch64_codegen_api.cpp`
  - `compile_ll_file_to_asm/object(...)`
  - `compile_bc_file_to_asm/object(...)`
  - `get_aarch64_codegen_capabilities()`
  - 已新增直接链接 `libsysycc_aarch64_codegen` 的 smoke test，验证外部程序
    可以不经过 `CompilerContext` 直接调用共享库 API
- `Phase 3` 已完成：
  - `compile_ll_file_to_asm(...)` 和 `compile_ll_file_to_object(...)`
    已不再统一返回 `UnsupportedInputFormat`
  - 新增 restricted textual LLVM IR importer：
    - `src/backend/asm_gen/aarch64/api/aarch64_restricted_llvm_ir_importer.hpp`
    - `src/backend/asm_gen/aarch64/api/aarch64_restricted_llvm_ir_importer.cpp`
  - 当前已验证可工作的 `.ll` 子集包括：
    - integer / void / ptr / array / literal struct type parsing
    - global `global` / `constant` + integer/float/null/zeroinitializer/basic aggregate initializers
    - direct function define/declare
    - integer binary ops
    - `icmp`
    - integer/float/pointer cast 家族中的常见 `sext/zext/trunc/sitofp/uitofp/fptosi/fptoui/fpext/fptrunc/inttoptr/ptrtoint`
    - `alloca`
    - `load` / `store`
    - direct / indirect `call`
    - `br` / `ret`
    - `phi`
    - basic `getelementptr inbounds`
    - `named types`
    - benign `module asm`
    - alias symbol rewriting
    - attribute / metadata-rich textual syntax tolerance
    - importer-level scalarization of a basic LLVM vector slice:
      - `insertelement`
      - `extractelement`
      - `shufflevector`
      - `llvm.vector.reduce.add.*`
  - 当前 Phase 3 smoke 已验证：
    - `foo.ll -> foo.s`
    - `foo.ll -> foo.o`
    - `readelf` 可检查 object
    - 若本机有 cross toolchain + qemu/sysroot，则可把 API 产出的 `.o` 链接并执行最小 smoke
  - `.bc` 支持不再是 Phase 3 blocker；这已移到 Phase 4 完成

- `Phase 4` 已完成：
  - `compile_bc_file_to_asm(...)` 和 `compile_bc_file_to_object(...)`
    已接通
  - `.bc` 读取层使用进程内 LLVM bitcode reader，不再依赖
    `llvm-dis` / `clang` 这类 host tool
  - `.bc` 读取层会先在进程内还原 textual LLVM IR，再复用已经完成的
    Phase 3 textual importer / native backend 路径
  - public capability surface 现在固定暴露 `.bc` 支持：
    - `get_aarch64_codegen_capabilities().supports_bc_files == true`
  - 当前 Phase 4 smoke 已验证：
    - `foo.bc -> foo.s`
    - `foo.bc -> foo.o`
    - `.ll/.bc` 共享同一 native AArch64 codegen 主路径
  - 当前 Phase 4 的现实约束：
    - 仍然依赖进程内 LLVM bitcode reader 以及对应 LLVM 库
    - 但不再依赖外部 `llvm-dis` / `clang`

- `Phase 5` 已完成：
  - `LowerIrPass` 现在会为 native AArch64 路径稳定落盘 LLVM IR 工件：
    - `build/intermediate_results/<stem>.ll`
    - `build/intermediate_results/<stem>.bc`
  - 编译器侧 `AArch64AsmGenPass` 不再直接消费 `CoreIrModule`
  - 当前 native compiler path 已改成：
    - `CoreIR -> LLVM IR text`
    - `LLVM IR text -> bitcode artifact`
    - `AArch64AsmGenPass -> compile_bc_file_to_asm/object(...)`
    - 若 bitcode artifact 缺失则退回 `.ll` artifact
  - 这意味着编译器与独立 AArch64 codegen 库的交互，已经切换到
    LLVM IR artifact 边界
  - 当前 Phase 5 smoke 已验证：
    - native compiler path 会稳定产出 `.ll/.bc` artifacts
    - native compiler path 在 `PATH=""` 下仍可完成 codegen
    - decoupled library path 与 compiler path 共用同一 `.ll/.bc` 主路径

- `Phase 6` 已完成：
  - 旧的 public `CoreIrModule -> AArch64` façade `AArch64AsmBackend` 已删除
  - 历史占位 `CoreIrAArch64TargetBackend` 已删除
  - `IrKind::AArch64` 旧占位枚举值已删除
  - 编译器中的 native AArch64 path 已不再直连 `CoreIR`
  - 当前 compiler/native/library 三者的真实边界已经统一成：
    - `CoreIR -> LLVM IR artifacts`
    - `LLVM IR artifacts -> libsysycc_aarch64_codegen`
  - Phase 6 后的剩余 CoreIR 依赖仅存在于
    restricted LLVM IR importer 的桥接实现内部，不再作为对外入口或
    compiler/native integration contract 暴露

- `Post-Phase 6` 独立交付面已落地：
  - 新增 standalone CLI target `sysycc-aarch64c`
  - 新增 install/export/package config：
    - `SysyCCAArch64CodegenConfig.cmake`
    - `SysyCCAArch64CodegenTargets.cmake`
  - 已验证安装后的 package 可被外部 `find_package(...)` 消费
  - 已验证 `sysycc-aarch64c` 可直接编译 `.ll` 与 `.bc`，不经过主编译器
  - 库内与 `CoreIR` / `common::diagnostic` 的桥接现已集中到单独内部桥接层，
    不再散落在 public API 编排路径中
  - `restricted importer` 已进一步拆成：
    - `LLVM text -> backend-local import model/parser`
    - `backend-local import model -> CoreIR bridge`
    这让 public API 与文本 parser 不再直接 include `CoreIR` 头文件
  - 当前目录职责也进一步清晰：
    - `api/` 只保留 public API、bitcode loader、text parser/import model
    - `support/` 承载仍然依赖 `CoreIR` 的内部 bridge
  - internal bridge 现在也已经从“重新解析 LLVM 文本”进一步收口成
    “直接消费 backend-local import model 再 lower 到 CoreIR”
  - `AArch64BackendPipeline` 的输入也开始从硬绑定 `CoreIrModule`
    收口成内部 codegen input 抽象，CoreIR 现在只是其中一种 adapter
- 截至 `2026-04-24`，AArch64 object/link 闭环又新增了一条稳定基线：
  - native direct object writer 现已支持 logical-immediate 形式的
    `and` / `orr` / `eor` 编码，修复了 object-only 路径在 backend
    生成 `and #1` 这类窄整数 mask 指令时无法直接产出 `.o` 的问题
  - 已新增 `tests/run/run_aarch64_multi_object_link_smoke`，覆盖
    `clang -> .ll -> sysycc-aarch64c -c -fPIC -> 多个 .o -> readelf ->
    外部链接 -> AArch64 运行验证`
  - 已用 `medce-1.c + medce-1-lib.c` 的双 `.o` 手工探测确认相同路径可复现并已通过
  - `tests/aarch64_backend_single_source` 现已把 `globalrefs.c` 纳入
    direct-object smoke，覆盖单源 `.o -> 外部链接 -> 运行`
  - `tests/compiler2025/run_arm_functional.sh` 与
    `tests/compiler2025/run_arm_performance.sh` 现已在主循环前增加
    一条最小 object/link 预检 smoke

后续按本计划继续推进时，应从 `Phase 2` 已落地的 public API 继续补 `Phase 3`
的 LLVM IR reader，不要把新的长期入口重新做回 `CompilerContext` 专属接口。

## 当前 Object 路径边界（`2026-04-25`）

已覆盖能力：

- `sysycc-aarch64c` 与 `compiler -c --backend=aarch64-native` 都可稳定产出
  AArch64 ELF relocatable object。
- `.text`、常见数据段、符号表、`.eh_frame`、`.debug_line` 的最小产物链路已可用。
- 已有回归覆盖直接函数调用的 `R_AARCH64_CALL26`、页级地址物化、
  `:lo12:` 补充、以及 PIC 下的 GOT 型全局引用。
- 已有三条门禁分别覆盖：
  - imported single-source direct-object smoke
  - multi-object PIC link/run smoke
  - compiler2025 ARM runner preflight smoke

仍未系统覆盖的场景：

- TLS / thread-local 相关 relocation
- weak/common/linkonce 等更复杂链接属性的 object 行为
- shared-library / PLT 细粒度场景，以及更完整的 GOT/PLT 组合覆盖
- 更复杂的 aggregate ABI、异常展开、以及 debug/unwind 的深度一致性验证
- driver 直接 orchestrate 多 `.o` 输入、再统一链接的完整工程化工作流

接口请求清单：

- 需要 driver 提供稳定的 public 开关，把前端输入直接驱动到
  native AArch64 object-only smoke，而不是手工先生成 `.ll`
- 需要 driver 端把 `-fPIC` / debug-info 等 native object 相关选项
  稳定传递到 AArch64 codegen library
- 如果后续目标变成“让 SysyCC 直接参与外部链接流程”，则还需要
  driver 层设计明确的 linker 参数透传与多对象编排接口

## Final Goal

最终形态拆成 3 层：

1. `libsysycc_aarch64_codegen`
   - AArch64 codegen 核心动态库
   - 负责从 LLVM IR module 生成 `.s` / `.o`

2. `sysycc-aarch64c`
   - 独立 CLI 工具
   - 接受 `.ll` / `.bc`
   - 调用 `libsysycc_aarch64_codegen`

3. `compiler`
   - 现有编译器主程序
   - 不再直接把 `CoreIR` 喂给 AArch64 backend
   - 改成在 IR 优化阶段落盘 `.ll` / `.bc`
   - 再调用独立 AArch64 codegen 库或工具

## Core Direction

### 1. 对外输入只认 LLVM IR

对外接口和独立工具都只接受：

- `.ll`
- `.bc`

不再把 `CoreIR` 作为 AArch64 backend 的公开输入契约。

### 2. 后端核心不直接暴露 C++ 内部对象 ABI

即使短期内假定 LLVM 版本一致，也不应把以下对象直接作为长期公共 ABI：

- `std::string`
- `std::vector`
- `DiagnosticEngine`
- `AArch64MachineModule`
- `AArch64ObjectModule`
- `CoreIrModule`
- `llvm::Module`

最终对外公共边界应尽量收敛成：

- 文件路径输入
- 内存 buffer 输入
- plain result bytes / plain text
- 明确的 diagnostics 输出

### 3. 用测试驱动 LLVM IR 兼容面扩张

这个项目不会一开始就支持“任意 LLVM IR”。

支持面必须通过：

- 最小 `.ll` regression
- `.bc` parity regression
- `clang/llc` 差分对拍
- AArch64 link/run smoke

逐步扩大。

## Phase Plan

## Phase 0：固定当前 AArch64 基线

目标：

- 先把当前 AArch64 backend 的行为基线锁死
- 为后续剥离和输入契约切换提供回归对照

动作：

- 选一组高价值 AArch64 regressions 作为常驻门禁
- 固定 4 类证据：
  - `.s` 结构
  - `.o` 结构
  - `readelf` / relocation / frame info
  - 可执行 smoke
- 新建面向后端独立演进的测试分组

重点测试面：

- spill-heavy
- direct/indirect call
- aggregate ABI
- float / float16
- object reloc/debug/unwind

验收：

- 当前仓库内 AArch64 regressions 形成稳定清单
- 后续所有 phase 都可复用这组基线测试

## Phase 1：把 AArch64 backend 物理剥离成独立动态库 target

目标：

- 先完成“构建层面的独立”
- 暂时允许内部仍然通过现有路径工作

动作：

- 新增动态库 target：
  - `libsysycc_aarch64_codegen`
- 将以下目录纳入独立库：
  - `src/backend/asm_gen/aarch64/model/`
  - `src/backend/asm_gen/aarch64/passes/`
  - `src/backend/asm_gen/aarch64/support/`
- 将当前 `compiler` 中对 AArch64 backend 的调用改成链接库后的 thin adapter

本 phase 不要求：

- 立即脱离 `CoreIR`
- 立即支持 `.ll/.bc`

验收：

- AArch64 backend 单独构建为 shared library
- 现有 `compiler -S/-c --backend=aarch64-native` 继续工作

## Phase 2：定义独立 AArch64 codegen API

目标：

- 不再通过编译器内部 wiring 才能调用后端
- 为独立工具和未来对外嵌入提供稳定入口

建议先做薄 C++ API：

- `compile_ll_file_to_asm(...)`
- `compile_ll_file_to_object(...)`
- `compile_bc_file_to_asm(...)`
- `compile_bc_file_to_object(...)`

输入：

- input file path
- target triple
- backend options

输出：

- asm text
- object bytes
- diagnostics

要求：

- 暂时可以假定 LLVM 版本一致
- 但 API 仍尽量不要暴露大量内部对象

验收：

- 独立的小测试程序可以直接链接库并调用 API

## Phase 3：先支持 `.ll`

目标：

- 正式把 AArch64 backend 的输入边界切换到 LLVM IR
- 优先做可读、可调试的文本 IR

核心策略：

- 直接新增 `LLVM IR -> AArch64 lowering` 边界
- 不建议长期保留 `LLVM IR -> CoreIR -> AArch64`
- AArch64 backend 的长期核心不再依赖 `CoreIR`

说明：

这是本计划里最重的一步，但也是最关键的一步。

需要完成：

- `.ll` 读取
- LLVM module 级遍历
- LLVM IR type/value/instruction 到 AArch64 lowering 的新入口
- globals/functions/constants/calls/GEP/control flow 的最小闭环

优先兼容子集：

- integer arithmetic
- branch / jump / return
- load / store
- direct / indirect call
- global variable
- basic `getelementptr`
- basic `phi`

验收：

- `foo.ll -> foo.s`
- `foo.ll -> foo.o`
- 最小样例 link/run smoke 可通过

## Phase 4：补 `.bc`

目标：

- 在 `.ll` 稳定后补齐 bitcode 输入

策略：

- 复用 `.ll` 路径之后的全部 lowering / codegen 逻辑
- 只在输入层新增 bitcode reader

原因：

- `.bc` 难点不在读取本身，而在调试体验和定位复杂度
- 所以不应先于 `.ll`

验收：

- `.bc -> .s`
- `.bc -> .o`
- 同一 IR 的 `.ll` / `.bc` 输出结果行为一致

## Phase 5：把 IR 优化阶段改成输出 `.ll` 和 `.bc`

目标：

- 编译器与独立 AArch64 backend 彻底解耦

动作：

- IR 优化阶段支持稳定输出：
  - `.ll`
  - `.bc`
- 将编译器中的 AArch64 native path 改成：
  - 优化后输出 LLVM IR 工件
  - 调用独立 AArch64 codegen 库或工具

收益：

- 中间产物可保存、可对拍、可复现
- 后端调试不再依赖整个编译器过程

验收：

- 编译器仍可通过新的 LLVM IR 工件路径产出 `.s` / `.o`
- 现有 AArch64 用户语义不回退

## Phase 6：删除旧的 CoreIR-AArch64 耦合入口

目标：

- 不保留长期双轨

删除对象：

- 旧的 `CoreIrModule -> AArch64` backend 主入口
- 只服务旧路径的 adapter/wiring
- 编译器里对旧 native backend 的直连逻辑

验收：

- AArch64 backend 核心不再 include `CoreIR` 相关头文件
- 编译器只通过 LLVM IR 工件与 AArch64 backend 交互

## Dynamic Library Risk Notes

即使当前阶段允许假定 LLVM 版本一致，动态库化仍有以下风险：

### 1. C++ ABI 风险

如果公共 API 暴露 STL、异常、内部类对象，未来很容易因为编译器、标准库或构建选项变化导致：

- 崩溃
- 析构错误
- 内存释放错误

结论：

- 公共 API 要薄
- 长期更推荐 C API 或 opaque handle

### 2. 内存所有权风险

若库分配对象、外部释放对象，跨动态库边界时容易出现 allocator 不一致问题。

结论：

- 统一 create/destroy
- 或直接返回 plain text / plain bytes

### 3. LLVM 版本绑定风险

如果公共接口直接暴露 `llvm::Module*` 之类对象，对接方必须与库共享完全一致的 LLVM 版本和 ABI。

本计划当前假设：

- 短期内允许此约束存在

但长期仍建议：

- 公共输入改成 `.ll` / `.bc` 文件或内存 buffer
- 库内部自行 parse

### 4. 测试矩阵膨胀风险

后续将出现多条测试链：

- source -> ll
- source -> bc
- ll -> s
- ll -> o
- bc -> s
- bc -> o
- link/run
- 与 clang/llc 对拍

结论：

- 必须尽早建立稳定的测试分层

## LLVM IR Compatibility Strategy

目标不是一次性“支持所有 LLVM IR”，而是持续补齐。

推荐推进方式：

1. 先定义当前支持子集
2. 新遇到一个兼容性缺口，就补一个最小 `.ll` regression
3. 为每个 `.ll` regression 增加：
   - `.s` / `.o` 生成验证
   - `clang/llc` 差分验证
   - 能运行时再补 run smoke

优先兼容顺序：

1. arithmetic / compare / cast
2. control flow / phi
3. memory / load / store
4. globals / constants
5. GEP / aggregate ABI
6. float / float16 / fp128
7. PIC / GOT / PLT / TLS
8. debug / unwind

## Recommended Commit Sequence

1. Create a standalone shared-library target for the AArch64 backend
2. Introduce an independent AArch64 codegen API
3. Add a standalone ll-to-aarch64 asm/object smoke tool
4. Lower textual LLVM IR modules into the AArch64 backend
5. Add bitcode input support on top of the LLVM module reader
6. Route compiler AArch64 codegen through emitted LLVM IR artifacts
7. Remove the legacy CoreIR-coupled AArch64 path

## Recommendation

本计划建议采用如下强策略：

- 先共享库化
- 先 `.ll` 后 `.bc`
- 先让 AArch64 backend 成为独立 codegen 产品
- 再让编译器去适配它

这是一个激进但方向清晰的改造路线。

如果阶段执行得当，最终收获会是：

- 一个独立的 AArch64 LLVM-IR codegen library
- 一个可单独调用的 `ll/bc -> s/o` 工具
- 一个与编译器主流程解耦、可持续用测试补齐兼容面的后端
