# AGENTS.md

本文件定义本仓库的人类开发者与代码代理协作规范。适用范围为整个 `SysyCC` 仓库。

这是一个正在持续演进的 `SysY22 / C-like` 编译器项目，当前代码已经不是空骨架，而是一个具备真实前端、语义分析、Core IR/LLVM IR 后端、测试体系和文档体系的工程。任何改动都应以当前代码与当前目录结构为准，不要把仓库当成“待规划模板”。

## 1. 项目总览

当前主路径大致是：

```text
main
  -> cli::Cli
  -> compiler::Complier
  -> compiler::PassManager
      -> PreprocessPass
      -> LexerPass
      -> ParserPass
      -> AstPass
      -> SemanticPass
      -> IRGenPass
```

当前仓库的核心目标：

- 完成 C-like / SysY22 风格源程序的预处理、词法、语法、AST、语义分析与 IR 生成。
- 以 `CompilerContext` 作为跨阶段共享状态中心。
- 以前端方言能力、语义模型、Core IR 管线和后端 Lowering 为主线持续演进。
- 保持文档、测试和实现同步。

### 1.1 当前阶段主线目标

当前阶段目标不是继续铺宽功能面，而是把 SysyCC 从“能产出汇编”推进到“能编译真实工程”。

- 让 SysyCC 能被小型/中型 C 工程通过 `make` / `ninja` / `cmake` 调用。
- 主战平台只做 AArch64；RISC-V 在本阶段只保不回退，不做主线开发。
- 该目标优先级高于 IR 新优化、高于新语法特性、高于第二后端扩张。

### 1.2 当前阶段全局协作规则

- 只修改自己被授权的模块范围，不得跨边界“顺手修”。
- 任何行为变化必须补测试。
- 任何接口变化必须写清楚对接需求。
- 小步提交，优先做可合并的小闭环。
- 如果发现问题落在别人的模块，不要直接改，先记录为“接口请求”或“阻塞项”。

### 1.3 AArch64 对象文件 / 链接闭环负责人

如果任务被明确分配为 SysyCC 的 AArch64 对象文件 / 链接闭环负责人，则该角色的唯一目标是：把 AArch64 路径从“能产出 `.s`”推进到“能稳定产出 `.o`，并能被外部工具链链接成可执行文件”。

主职责：

- 提升 AArch64 object emission 稳定性。
- 补齐多文件链接所需的 ABI / relocation / PIC / external symbol 兼容。
- 提升 AArch64 多目标文件链接后的可运行性。
- 不负责 driver 参数解析，不负责系统头兼容。

允许修改的模块范围：

- `src/backend/asm_gen/aarch64/**`
- `src/backend/asm_gen/asm_result.hpp`
- `src/backend/asm_gen/object_result.hpp`
- `src/backend/asm_gen/backend_options.hpp`
- `src/backend/asm_gen/backend_kind.hpp`
- `tests/aarch64_backend_single_source/**`
- `tests/compiler2025/run_arm_*.sh`
- `tests/run/**` 中与 AArch64 object/link smoke 直接相关的新增用例
- `doc/modules/ir.md`
- `doc/modules/tests.md`
- `doc/modules/aarch64-llvm-backend-plan.md`
- `doc/README.md`

禁止修改的模块范围：

- `src/cli/**`
- `src/compiler/complier.cpp`
- `src/frontend/**`
- `src/backend/ir/**`，除非是极窄的 backend bridge 必要改动
- `Makefile` 中与 driver / build-system 相关的逻辑

当前背景：

- AArch64 backend pipeline 已经分为 machine lowering / regalloc / spill rewrite / frame finalize / emission。
- 当前目标不是继续扩第二后端，而是先把 AArch64 真正做成可链接、可运行的主线。
- 该负责人不负责让真实工程“能被构建系统调用”，只负责让 AArch64 产物质量足够支撑这件事。

第一阶段任务：

- 稳定 `.o` 产物输出。
- 跑通多 `.o` 文件外部链接 smoke。
- 优先补 external symbol、call ABI、PIC、relocation、frame/unwind 高频问题。
- 把 AArch64 imported single-source smoke 维持为主门禁。
- 新增至少一个“多源文件 -> 多 `.o` -> 外部链接 -> qemu/运行验证”的 regression。

验收标准：

- SysyCC 产出的多个 AArch64 `.o` 能被外部 `clang` / `ld` 正常链接。
- 多文件函数调用、外部符号引用、静态数据访问不出现系统性 ABI 问题。
- 现有 AArch64 single-source smoke 不回退。
- object/link 相关失败有稳定复现和回归用例。

输出要求：

- 不要改 driver，不要改前端。
- 若需要 driver 提供额外输入参数，只提交“接口请求清单”。
- 优先修复 link blockers，而不是继续扩展非关键指令能力。

## 2. 文档目录结构

本项目文档目录实际使用 `doc/`，不是 `docs/`。编写和更新文档时应遵循当前结构：

```text
doc/
├── README.md
└── modules/
    ├── aarch64-llvm-backend-plan.md
    ├── ast.md
    ├── attribute.md
    ├── class-relationships.md
    ├── cli.md
    ├── common.md
    ├── compiler.md
    ├── diagnostic.md
    ├── dialect-refactor-plan.md
    ├── dialects.md
    ├── ir.md
    ├── legacy-pass.md
    ├── lexer.md
    ├── manual.md
    ├── parser.md
    ├── preprocess.md
    ├── scripts.md
    ├── semantic.md
    └── tests.md
```

文档职责约定：

- `doc/README.md`
  - 作为文档入口、总览、模块地图和推荐阅读顺序。
- `doc/modules/*.md`
  - 每个模块一份文档，描述目录布局、关键类、主路径和当前状态。
- `roadmap.md`
  - 记录阶段性里程碑和当前主线目标。
- `manual/`
  - 放手册、规范摘录或外部参考资料时，优先作为原始资料区，不要把架构说明堆进去。

文档更新规则：

- 改 `src/frontend/preprocess` 时，同时检查 `doc/modules/preprocess.md`。
- 改 `src/frontend/lexer` 时，同时检查 `doc/modules/lexer.md`。
- 改 `src/frontend/parser` 时，同时检查 `doc/modules/parser.md`。
- 改 `src/frontend/ast` 时，同时检查 `doc/modules/ast.md`。
- 改 `src/frontend/semantic` 时，同时检查 `doc/modules/semantic.md`。
- 改 `src/backend/ir` 时，同时检查 `doc/modules/ir.md`。
- 改主调度关系、所有权关系或跨模块协作方式时，同时检查：
  - `doc/modules/compiler.md`
  - `doc/modules/class-relationships.md`
  - `doc/README.md`

## 3. 代码目录结构

当前代码结构以 `src/` 为核心，建议按下面的职责理解：

```text
src/
├── main.cpp
├── cli/
├── common/
├── compiler/
├── frontend/
│   ├── ast/
│   ├── attribute/
│   ├── dialects/
│   ├── lexer/
│   ├── parser/
│   ├── preprocess/
│   └── semantic/
├── backend/
│   ├── asm_gen/
│   ├── ir/
│   └── ir_passes/
├── pass/
└── ...
```

### 3.1 `src/main.cpp`

- 程序入口。
- 负责串起 CLI、编译器对象、共享诊断输出。
- 不应塞入具体词法/语义/IR 细节。

### 3.2 `src/cli/`

- 命令行参数解析。
- 将用户输入映射为 `ComplierOption`。
- 这里负责“怎么启动编译”，不负责“怎么做编译”。

### 3.3 `src/common/`

- 放跨阶段复用的基础设施。
- 当前重点包括：
  - `diagnostic/`
  - `source_manager`
  - `source_line_map`
  - `source_location_service`
  - `source_mapping_view`
  - 字面量辅助工具
- 这层应尽量保持稳定和低耦合，避免依赖上层具体语法/IR 细节。

### 3.4 `src/compiler/`

- 编译器主控层。
- 核心对象包括：
  - `Complier`
  - `ComplierOption`
  - `CompilerContext`
  - `compiler/pass/*`
- 负责：
  - 管理一次编译任务的选项
  - 初始化和同步共享上下文
  - 组装并执行 pass pipeline
  - 汇总阶段结果与诊断

注意：

- 当前仓库真实命名是 `Complier`，不是 `Compiler`。
- 除非用户明确要求做一次全局命名修正，否则不要顺手改这个拼写。

### 3.5 `src/frontend/`

按编译前端阶段拆分：

- `preprocess/`
  - 预处理流程、宏展开、条件编译、include 解析、预定义宏、源码映射。
- `lexer/`
  - token 扫描与 token 序列输出。
- `parser/`
  - bison 语法、解析运行时、解析错误信息、语法特性校验。
- `ast/`
  - AST 节点、AST dump、AST lowering、AST 特性校验。
- `attribute/`
  - GNU/扩展属性的解析和分析。
- `dialects/`
  - 方言管理、dialect pack、各阶段 feature/handler 注册表。
- `semantic/`
  - 语义模型、作用域、类型系统、常量求值、声明/表达式/语句分析。

### 3.6 `src/backend/`

当前重点是 `ir/`：

- `ir/core/`
  - Core IR 的对象模型和构建基础。
- `ir/pipeline/`
  - `Build -> Optimize -> Lower` 主管线。
- `ir/pass/`
  - Core IR pass 管理与优化插槽。
- `ir/lowering/`
  - 面向目标后端的 lowering 边界。
- `ir/lowering/llvm/`
  - Core IR -> LLVM IR lowering。
- `ir/lowering/aarch64/`
  - 当前是显式占位后端，不要把它当成已完成实现。
- `ir/printer/`
  - Core IR 原始文本输出。
- `ir/detail/`
  - IR 内部辅助数据结构和共享 lowering 细节。

此外：

- 旧的 `IRBuilder -> IRBackend -> LlvmIrBackend` 路径仍在树中，属于迁移中的参考实现。
- 新改动要优先判断自己应该接入 Core IR 新路径，还是补 legacy 兼容层。

### 3.7 `src/pass/`

- 这是遗留兼容层，不是当前主设计中心。
- 相关信息应参考 `doc/modules/legacy-pass.md`。

## 4. 代码设计结构

### 4.1 设计分层

建议按下面的层次理解当前工程：

```text
CLI Layer
  -> Compiler Orchestration Layer
      -> Frontend Stages
          -> Semantic Model
              -> Core IR Pipeline
                  -> Target Lowering
                      -> IR Text / Backend Output
```

### 4.2 核心设计原则

- `CompilerContext` 是共享状态中心。
- 每个阶段通过 pass 顺序推进，不应跨阶段随意写状态。
- 诊断统一走 `DiagnosticEngine`，不要各处自造错误输出风格。
- 方言能力通过 `DialectManager` 及其 registry 统一暴露，不要在 lexer/parser/semantic/ir 中重复硬编码开关。
- 语义分析负责“是否合法、类型是什么、常量值是什么”。
- IR 负责“如何表达可执行语义”，不要把高层语义规则散落到后端字符串拼接里。

### 4.3 关键共享对象

- `ComplierOption`
  - 一次编译任务的外部输入配置。
- `CompilerContext`
  - 一次编译过程中的共享内部状态。
- `DiagnosticEngine`
  - 跨阶段统一诊断容器。
- `DialectManager`
  - 方言包、feature registry、handler registry 聚合器。
- `SemanticModel`
  - AST 绑定后的语义信息中心。
- `IRResult`
  - IR 输出结果容器。

### 4.4 依赖方向

默认依赖方向应保持为：

```text
cli -> compiler
compiler -> common/frontend/backend
frontend -> common
semantic -> ast/common/dialects
backend/ir -> semantic/ast/common
common -> 尽量不反向依赖上层
```

如果新增代码打破这个方向，应先确认是否真的需要，否则优先抽公共层。

### 4.5 生成文件规则

以下文件属于生成产物，修改语法或词法规则时优先改源文件，不要直接手改生成文件：

- `src/frontend/parser/parser.y`
- `src/frontend/lexer/lexer.l`

对应生成文件包括：

- `build/generated/frontend/parser/parser_generated.cpp`
- `build/generated/frontend/parser/parser.tab.h`
- `build/generated/frontend/lexer/lexer_scanner.cpp`
- `build-ninja/generated/frontend/parser/parser_generated.cpp`
- `build-ninja/generated/frontend/parser/parser.tab.h`
- `build-ninja/generated/frontend/lexer/lexer_scanner.cpp`

如果生成文件与源规则不一致，应通过构建系统重新生成。

## 5. 按模块改动时的落点规则

### 5.1 改预处理

优先检查：

- `src/frontend/preprocess/`
- `src/common/source_*`
- `src/common/diagnostic/*`
- `doc/modules/preprocess.md`

不要只改宏展开而忽略：

- include 路径解析
- 条件编译栈
- 源码位置映射
- 诊断链路

### 5.2 改词法

优先检查：

- `src/frontend/lexer/lexer.l`
- `src/frontend/lexer/lexer.cpp`
- `src/frontend/dialects/registries/lexer_keyword_registry.cpp`
- `doc/modules/lexer.md`

如果增加关键字，要同步考虑：

- dialect registry
- token 输出
- parser 接受路径

### 5.3 改语法

优先检查：

- `src/frontend/parser/parser.y`
- `src/frontend/parser/parser_runtime.*`
- `src/frontend/parser/parser_feature_validator.*`
- `src/frontend/attribute/*`
- `doc/modules/parser.md`

不要只让 grammar 过编译，还要确认：

- AST 是否能承接
- feature validator 是否需要更新
- 错误信息是否仍然可读

### 5.4 改 AST

优先检查：

- `src/frontend/ast/*`
- `src/frontend/ast/detail/*`
- `doc/modules/ast.md`

新增 AST 节点后要同步考虑：

- dump 输出
- lowering builder
- completeness / validator 逻辑
- semantic 是否可消费

### 5.5 改语义

优先检查：

- `src/frontend/semantic/analysis/*`
- `src/frontend/semantic/model/*`
- `src/frontend/semantic/type_system/*`
- `src/frontend/semantic/support/*`
- `doc/modules/semantic.md`

语义改动必须明确：

- 这是语言规则修正、bug 修复，还是新特性扩展
- 会影响哪些已有测试
- 是否需要新增诊断或 warning

### 5.6 改 IR / 后端

优先检查：

- `src/backend/ir/core/*`
- `src/backend/ir/pipeline/*`
- `src/backend/ir/pass/*`
- `src/backend/ir/lowering/*`
- `src/backend/ir/printer/*`
- `doc/modules/ir.md`

新增 lowering 或优化前要明确：

- 输入 IR 不变量是什么
- 输出 IR 不变量是什么
- 属于 Core IR 层、Lowering 层，还是 legacy LLVM 层

## 6. 测试和验证结构

当前测试目录不是单一 `unit/` 风格，而是按阶段组织：

```text
tests/
├── dialects/
├── fuzz/
├── parse/
├── run/
├── semantic/
├── test_helpers.sh
└── run_all.sh
```

默认测试原则：

- 每次新增功能时，必须新增至少一个对应测试用例。
- 新测试应优先放到最贴近该功能所在阶段的目录下，例如：
  - 预处理功能放到 `tests/preprocess/`
  - 词法功能放到 `tests/lexer/`
  - 语法功能放到 `tests/parser/` 或 `tests/ast/`
  - 语义功能放到 `tests/semantic/`
  - IR / 运行期功能放到 `tests/ir/` 或 `tests/run/`
  - 方言功能放到 `tests/dialects/`
- 如果一个新功能跨越多个阶段，至少补一条最直接覆盖主行为的阶段测试；如果行为最终可执行，再补一条 `run` 级回归测试。
- 每次新增功能、修复功能或调整行为后，默认都要运行除 `fuzz` 以外的回归测试，不要只跑单个新增用例就结束。
- 默认非 `fuzz` 回归入口是：

```bash
./tests/run_all.sh
```

- `fuzz` 不属于日常每次功能改动后的必跑项；只有在用户明确要求、修改了 `tests/fuzz/`、修改了 fuzz 脚本，或怀疑存在需要扩大覆盖面的随机回归风险时再单独运行。
- 改 parser，至少看 `tests/parse/`。
- 改 semantic，至少看 `tests/semantic/`。
- 改运行期 IR/代码生成，至少看 `tests/run/`。
- 改方言注册、扩展包或 feature ownership，至少看 `tests/dialects/`。
- 改 fuzz 相关脚本或回归策略，再检查 `tests/fuzz/`。

## 7. 构建与日常命令

优先遵循仓库已有入口：

```bash
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
```

也可以用顶层 `Makefile`：

```bash
make build
make run
make format
make check
```

说明：

- 顶层 `make build` / `make run` 现在走 `build-ninja/` 这套 Ninja 开发构建目录。
- `build/` 仍然会被测试脚本和中间产物使用，不要把它当源码目录改。
- `clang-format` 应只用于目标文件，不要无差别扫全仓库。

## 8. 代理工作规则

- 先读相关模块文档，再改代码。
- 先确认自己是在改当前主路径，还是在改迁移中的旧路径。
- 不要把一个跨阶段问题只修一半。
- 不要顺手大规模重命名或格式化无关文件。
- 每次添加新功能时，必须同步添加对应测试，不接受“功能先落地、测试后补”的默认做法。
- 每次完成功能改动后，默认执行一次非 `fuzz` 回归；如果没有运行，要明确说明原因。
- 对于涉及架构变化的改动，要同步更新 `doc/README.md` 或对应模块文档。
- 如果需求涉及“增加语言特性”，最少要检查：
  - preprocess
  - lexer
  - parser
  - ast
  - semantic
  - ir
  - tests

## 9. 推荐阅读顺序

首次接手本仓库时，推荐按下面顺序建立上下文：

1. `doc/README.md`
2. `doc/modules/compiler.md`
3. `doc/modules/class-relationships.md`
4. `doc/modules/preprocess.md`
5. `doc/modules/lexer.md`
6. `doc/modules/parser.md`
7. `doc/modules/ast.md`
8. `doc/modules/semantic.md`
9. `doc/modules/ir.md`
10. `doc/modules/tests.md`

## 10. 本文件的用途

这份 `AGENTS.md` 不是为了替代现有模块文档，而是为了给进入仓库执行任务的人或代理一个“从目录结构到设计结构”的统一入口。

如果未来目录结构、主路径、Core IR 管线或文档组织发生变化，应优先同步更新本文件。
