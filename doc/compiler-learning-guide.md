# 从 0 开始用 SysyCC 学习编译原理

## 目标读者

这份文档面向刚开始学习编译原理、希望以 SysyCC 作为参考工程的同学。它不是只列几本书或几个概念，而是把“理论知识”“工程代码”“测试用例”“真实项目验证”放在同一条学习路径里。

读完并按路线实践后，你应该能够：

- 理解一个 C-like 编译器从命令行输入到生成目标代码的大体路径
- 看懂 SysyCC 中 CLI、Compiler、Frontend、Semantic、Core IR、Backend、Tests 的基本职责
- 知道每个编译阶段需要掌握哪些理论知识
- 能通过小实验验证自己的理解，而不是只停留在读代码
- 能逐步从 toy compiler 视角过渡到真实工程编译器视角

## 项目学习地图

SysyCC 当前主路径可以按下面的层次理解：

```text
main
  -> cli::Cli
  -> compiler::Compiler
  -> compiler::PassManager
      -> PreprocessPass
      -> LexerPass
      -> ParserPass
      -> AstPass
      -> SemanticPass
      -> BuildCoreIrPass
      -> Core IR optimization passes
      -> LowerIrPass
      -> AArch64 / RISC-V backend paths
```

对应源码入口：

- `src/main.cpp`: 程序入口
- `src/cli/`: 命令行参数解析和 driver 行为
- `src/compiler/`: 编译任务调度、选项、上下文、pass 管理
- `src/frontend/preprocess/`: 预处理器
- `src/frontend/lexer/`: 词法分析
- `src/frontend/parser/`: 语法分析
- `src/frontend/ast/`: AST 结构和 AST pass
- `src/frontend/semantic/`: 语义分析、类型系统、作用域、常量表达式
- `src/backend/ir/`: Core IR、优化、lowering
- `src/backend/asm_gen/`: 目标后端、汇编和对象文件路径
- `tests/`: 分阶段测试、真实工程测试、回归测试基础设施
- `doc/modules/`: 各模块文档

## 学习前置知识

### C/C++ 基础

SysyCC 使用 C++ 实现，并编译 C-like / SysY / 部分真实 C 工程。因此学习者需要先补齐：

- C 语言基本语法：表达式、语句、函数、数组、指针、结构体、联合体、枚举
- C 语言声明系统：声明符、指针声明、数组声明、函数声明、typedef
- C 预处理：宏、头文件、条件编译、`#include` 搜索路径
- C ABI 基本概念：函数调用、参数传递、返回值、对象文件、链接
- C++ 基础：类、继承、多态、智能指针、容器、RAII、`std::optional`
- C++ 工程阅读能力：头文件/源文件分离、命名空间、CMake/Ninja 构建

建议练习：

- 手写 20 个复杂 C 声明并解释含义
- 用 `clang -E` 观察宏展开后的源文件
- 用 `clang -S` 观察简单 C 程序生成的汇编
- 用 `nm` / `objdump` / `readelf` 查看对象文件符号

### 工具链基础

学习编译器不能只看算法，还要理解工具链。需要掌握：

- `cc` / `clang` / `gcc` 的常用参数：`-c`、`-S`、`-E`、`-o`、`-I`、`-D`、`-L`、`-l`
- 静态库和动态库：`.a`、`.so`、`.dylib`
- Make / Ninja / CMake 如何调用编译器
- depfile：`-MD`、`-MMD`、`-MF`、`-MT`、`-MQ`
- 交叉编译基本概念
- Docker / qemu 在编译器测试中的作用

对应 SysyCC 代码：

- `src/cli/`
- `src/compiler/compiler.cpp`
- `tests/compiler/`
- `tests/manual/external_real_project_probe/`
- `doc/modules/cli.md`
- `doc/modules/tests.md`

## 第一阶段：建立编译器全局视角

### 必学概念

- 编译器 pipeline
- pass 和 pass manager
- 上下文对象
- 诊断系统
- 中间产物
- driver 和 compiler core 的区别

### 在 SysyCC 中看哪里

- `src/main.cpp`
- `src/compiler/compiler.cpp`
- `src/compiler/compiler_option.hpp`
- `src/compiler/compiler_context/`
- `src/compiler/pass/`
- `src/common/diagnostic/`
- `doc/modules/compiler.md`
- `doc/modules/class-relationships.md`
- `doc/modules/diagnostic.md`

### 你要能回答的问题

- `main.cpp` 做了什么，不做什么？
- `Cli` 和 `Compiler` 的边界在哪里？
- `CompilerContext` 里保存了哪些跨阶段状态？
- 一个 pass 失败后，诊断如何向用户展示？
- 为什么编译器要分阶段，而不是一个函数从头干到尾？

### 推荐实验

1. 构建项目：

```bash
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
```

2. 找一个最小输入，观察中间产物：

```bash
build-ninja/compiler tests/run/run_factorial_for/run_factorial_for.sy --dump-tokens --dump-parse --dump-ast --dump-ir
```

3. 故意写一个语法错误，观察诊断格式。

## 第二阶段：预处理器

### 必学概念

- translation unit
- preprocessing token
- object-like macro
- function-like macro
- variadic macro
- macro expansion
- stringification：`#`
- token pasting：`##`
- include search path
- system header 和 user header
- include guard
- 条件编译：`#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif`
- builtin probe：`__has_include`、`__has_attribute`、`__has_feature`
- 预定义宏
- `#line` 和源码位置映射

### 在 SysyCC 中看哪里

- `src/frontend/preprocess/`
- `src/frontend/preprocess/detail/macro_expander.cpp`
- `src/frontend/preprocess/detail/macro_table.cpp`
- `src/frontend/preprocess/detail/include_resolver.cpp`
- `src/frontend/preprocess/detail/conditional_stack.cpp`
- `src/frontend/preprocess/detail/constant_expression_evaluator.cpp`
- `src/common/source_manager.*`
- `src/common/source_line_map.*`
- `doc/modules/preprocess.md`
- `tests/preprocess/`
- `tests/semantic/semantic_standard_headers_matrix/`

### 你要能回答的问题

- 宏展开为什么容易递归？
- `#include "x.h"` 和 `#include <x.h>` 搜索路径有什么区别？
- 系统头为什么会暴露大量非标准扩展？
- 为什么预处理器需要维护源码位置映射？
- `#if 0` 中的非法代码是否应该进入 lexer/parser？

### 推荐实验

- 写一个包含嵌套宏、字符串化、token paste 的用例，对比 `clang -E` 和 SysyCC 预处理结果
- 写一个本地头文件和系统头文件混合 include 的用例
- 尝试添加一个新的 predefined macro，并补最小测试

## 第三阶段：词法分析

### 必学概念

- 字符流到 token 流
- token kind
- keyword 和 identifier 的区别
- integer literal
- floating literal
- character literal
- string literal
- escape sequence
- 注释处理
- longest match
- lexical state
- flex 生成 scanner 的基本方式

### 在 SysyCC 中看哪里

- `src/frontend/lexer/lexer.l`
- `src/frontend/lexer/lexer.cpp`
- `src/frontend/dialects/registries/lexer_keyword_registry.cpp`
- `doc/modules/lexer.md`
- `tests/lexer/`

### 你要能回答的问题

- 为什么 `typedef` 名不是 lexer 直接识别的 keyword？
- `>>=` 应该被拆成几个 token？
- 字符串字面量何时拼接？
- 关键字为什么和 dialect manager 有关？
- lexer 如何把 token 的位置传给后续阶段？

### 推荐实验

- 给 lexer 添加一种新关键字，并观察 parser 是否马上能接受
- 添加一个新的字面量边界测试
- 打开 `--dump-tokens` 对比 token 输出

## 第四阶段：语法分析

### 必学概念

- context-free grammar
- terminal / non-terminal
- parse tree
- AST
- shift / reduce conflict
- precedence 和 associativity
- declaration vs expression ambiguity
- C 声明语法
- statement grammar
- error recovery
- bison 基础

### 在 SysyCC 中看哪里

- `src/frontend/parser/parser.y`
- `src/frontend/parser/parser.cpp`
- `src/frontend/parser/parser_runtime.*`
- `src/frontend/parser/parser_feature_validator.*`
- `src/frontend/attribute/`
- `doc/modules/parser.md`
- `tests/parser/`
- `tests/ast/`

### 你要能回答的问题

- C 语言为什么语法分析比四则表达式复杂很多？
- `typedef` 为什么会影响语法解析？
- `int *f(void);` 和 `int (*f)(void);` 有什么区别？
- attribute 应该在 parser 处理，还是 semantic 处理？
- parser 为什么不能负责所有类型检查？

### 推荐实验

- 给 parser 增加一个小语法特性，并补 parser/AST 测试
- 故意制造一个 shift/reduce conflict，观察 bison 报告
- 从 `parser.y` 找到 `if/else` 悬挂问题的处理方式

## 第五阶段：AST 设计

### 必学概念

- AST 和 parse tree 的区别
- expression node
- statement node
- declaration node
- type node / declarator node
- source span
- AST dump
- AST visitor / lowering
- ownership model

### 在 SysyCC 中看哪里

- `src/frontend/ast/`
- `src/frontend/ast/detail/`
- `doc/modules/ast.md`
- `tests/ast/`

### 你要能回答的问题

- 为什么 parser 产物不直接拿去生成 IR？
- AST 节点为什么需要 source span？
- AST 中是否应该保存语义类型？
- AST 节点所有权如何管理？
- dump AST 对调试有什么帮助？

### 推荐实验

- 找一个表达式，画出它的 AST
- 增加一个 AST dump 字段，观察测试如何变化
- 从 parser 规则追踪一个函数定义如何变成 AST

## 第六阶段：语义分析

### 必学概念

- scope
- symbol table
- name lookup
- declaration binding
- type checking
- type compatibility
- lvalue / rvalue
- implicit conversion
- usual arithmetic conversion
- pointer conversion
- array-to-pointer decay
- function designator decay
- constant expression
- enum constant
- struct / union layout
- bit-field
- function prototype
- redeclaration rule
- incomplete type
- builtin type
- builtin function

### 在 SysyCC 中看哪里

- `src/frontend/semantic/analysis/`
- `src/frontend/semantic/model/`
- `src/frontend/semantic/type_system/`
- `src/frontend/semantic/support/`
- `src/frontend/dialects/packs/builtin_types/`
- `doc/modules/semantic.md`
- `tests/semantic/`

### 你要能回答的问题

- `int *p; *p = 1;` 中每个表达式的类型是什么？
- `arr` 在表达式里为什么经常变成 pointer？
- `struct S;` 和 `struct S { int x; };` 的关系是什么？
- `typedef struct S S;` 为什么会同时涉及 tag namespace 和 ordinary identifier namespace？
- `void f(void);` 和 `void f();` 在 C 里有什么差别？
- 常量表达式为什么不能简单地等到运行时再算？
- `union` 初始化为什么会影响 IR lowering？

### 推荐实验

- 给一个错误程序补更精确的 semantic diagnostic
- 新增一个标准头兼容 smoke
- 写一个 typedef / struct tag 同名测试
- 写一个函数原型和函数定义合并测试

## 第七阶段：Core IR

### 必学概念

- intermediate representation
- SSA
- basic block
- control-flow graph
- instruction
- value
- use-def chain
- phi node
- terminator
- load / store
- alloca / stack slot
- global variable
- constant
- aggregate initializer
- lowering boundary
- verifier
- IR printer

### 在 SysyCC 中看哪里

- `src/backend/ir/shared/core/`
- `src/backend/ir/build/`
- `src/backend/ir/shared/printer/`
- `src/backend/ir/lower/`
- `src/backend/ir/pipeline/`
- `doc/modules/ir.md`
- `tests/ir/ir_core_raw_printer/`
- `tests/ir/ir_core_builder_*/`
- `tests/ir/ir_global_union_pointer_address_initializer/`

### 你要能回答的问题

- 为什么优化通常不直接在 AST 上做？
- 基本块必须以 terminator 结尾吗？
- phi 节点解决了什么问题？
- use-def 链为什么对优化很重要？
- `load` 和 `store` 为什么是优化中的难点？
- 全局聚合初始化为什么需要精确布局？

### 推荐实验

- 选择一个简单 C 函数，手动画 CFG
- 对比 `--dump-ir` 输出和源代码
- 找一个 `if/else` 程序，观察 phi 或控制流如何表达
- 修改一个 IR printer 输出，理解 IR 对象结构

## 第八阶段：IR 分析

### 必学概念

- CFG analysis
- dominator tree
- dominance frontier
- loop info
- induction variable
- scalar evolution
- alias analysis
- escape analysis
- memory location
- MemorySSA
- call graph
- function attributes
- preserved analyses
- analysis invalidation

### 在 SysyCC 中看哪里

- `src/backend/ir/analysis/`
- `src/backend/ir/effect/`
- `src/backend/ir/shared/detail/core_ir_rewrite_utils.*`
- `doc/modules/ir.md`
- `tests/ir/ir_core_cfg_analysis/`
- `tests/ir/ir_core_dominator_tree_analysis/`
- `tests/ir/ir_core_dominance_frontier_analysis/`
- `tests/ir/ir_core_memory_ssa/`
- `tests/ir/ir_core_alias_analysis/`
- `tests/ir/ir_core_escape_analysis/`

### 你要能回答的问题

- 什么是支配关系？
- 为什么 mem2reg 需要 dominance frontier？
- alias analysis 为什么通常只能保守判断？
- MemorySSA 为什么能加速 load/store 相关优化？
- pass 改了 CFG 后，哪些分析必须失效？
- preserved analyses 设计不好会导致什么 bug？

### 推荐实验

- 手写一个 diamond CFG，计算 dominator tree 和 dominance frontier
- 找一个 loop，标出 header、latch、preheader、exit
- 给 MemorySSA 增加一个小测试，验证 clobber 查询
- 故意不 invalidation 一个分析，观察是否产生错误优化

## 第九阶段：IR 优化

### 必学概念

- constant folding
- copy propagation
- dead code elimination
- dead store elimination
- common subexpression elimination
- global value numbering
- sparse conditional constant propagation
- mem2reg
- instcombine
- simplify cfg
- loop simplify
- loop invariant code motion
- loop rotation
- loop unroll
- loop vectorization
- inlining
- interprocedural optimization
- fixed-point iteration
- profitability heuristic
- correctness first

### 在 SysyCC 中看哪里

- `src/backend/ir/const_fold/`
- `src/backend/ir/copy_propagation/`
- `src/backend/ir/dce/`
- `src/backend/ir/dead_store_elimination/`
- `src/backend/ir/local_cse/`
- `src/backend/ir/gvn/`
- `src/backend/ir/sccp/`
- `src/backend/ir/mem2reg/`
- `src/backend/ir/licm/`
- `src/backend/ir/loop_*`
- `src/backend/ir/inliner/`
- `src/backend/ir/pipeline/core_ir_pass_pipeline.cpp`
- `tests/ir/ir_core_*`

### 你要能回答的问题

- 为什么优化 pass 必须先证明安全？
- GVN 和 local CSE 的区别是什么？
- DCE 如何判断一条指令没有副作用？
- LICM 为什么需要 loop preheader？
- fixed-point group 为什么可能 hang？
- 为什么真实工程会暴露 toy case 看不到的性能问题？
- 为什么优化器性能本身也是编译器质量的一部分？

### 推荐实验

- 写一个重复表达式程序，观察 local CSE/GVN 是否消除
- 写一个常量条件分支，观察 simplify cfg
- 写一个 loop invariant 表达式，观察 LICM
- 打开 pass trace，找出最慢的 pass

相关命令：

```bash
SYSYCC_TRACE_PASSES=1 build-ninja/compiler input.c -c -o input.o
SYSYCC_PASS_REPORT_DIR=build/pass-reports build-ninja/compiler input.c -c -o input.o
```

## 第十阶段：Lowering 和 LLVM IR

### 必学概念

- target-independent IR
- target-specific lowering
- LLVM IR
- type lowering
- aggregate lowering
- calling convention
- function declaration lowering
- global variable lowering
- string literal lowering
- pointer representation

### 在 SysyCC 中看哪里

- `src/backend/ir/lower/`
- `src/backend/ir/lower/lowering/llvm/`
- `src/backend/ir/lower/legacy/`
- `doc/modules/ir.md`
- `tests/ir/`
- `tests/run/`

### 你要能回答的问题

- Core IR 和 LLVM IR 的职责边界在哪里？
- 为什么不能把前端语义规则散落到 LLVM 字符串拼接里？
- aggregate 类型 lowering 最容易错在哪里？
- 函数声明和函数定义在 IR 层有什么区别？
- 为什么真实项目里的系统头会生成大量函数原型？

### 推荐实验

- 选择一个结构体返回函数，观察 LLVM IR
- 选择一个字符串字面量程序，观察全局常量
- 手动用 `clang` 编译 SysyCC 输出的 LLVM IR，验证行为

## 第十一阶段：后端、汇编和对象文件

### 必学概念

- instruction selection
- register allocation
- stack frame
- spill
- prologue / epilogue
- ABI
- calling convention
- relocation
- symbol visibility
- PIC
- object file
- assembler
- linker
- qemu-user

### 在 SysyCC 中看哪里

- `src/backend/asm_gen/aarch64/`
- `src/backend/asm_gen/riscv64/`
- `tests/aarch64_backend_single_source/`
- `tests/run/run_aarch64_multi_object_*`
- `tests/compiler2025/run_arm_*.sh`
- `doc/modules/aarch64-llvm-backend-plan.md`

### 你要能回答的问题

- 汇编和对象文件有什么区别？
- 为什么多 `.o` 链接比单文件汇编更难？
- AArch64 函数参数如何传递？
- 结构体返回如何通过 ABI 表达？
- 外部符号引用为什么需要 relocation？
- PIC 对全局地址访问有什么影响？

### 推荐实验

- 编译一个两文件函数调用程序，观察符号表和 relocation
- 用 qemu 跑 AArch64 产物
- 对比 Clang 和 SysyCC 后端输出的对象文件

## 第十二阶段：Driver 和真实工程构建

### 必学概念

- compiler driver
- compile-only
- full compile and link
- link-only
- multi-source compile
- response file
- depfile
- build-system compatibility
- host linker passthrough
- system include compatibility
- real project smoke

### 在 SysyCC 中看哪里

- `src/cli/`
- `src/compiler/compiler.cpp`
- `tests/cli/`
- `tests/compiler/`
- `tests/run/run_make_multifile_smoke/`
- `tests/run/run_cmake_ninja_multifile_smoke/`
- `tests/run/run_north_star_real_project_smoke/`
- `tests/manual/external_real_project_probe/`
- `doc/modules/cli.md`
- `doc/modules/tests.md`

### 你要能回答的问题

- `compiler -c a.c -o a.o` 和 `compiler a.c -o a` 的差别是什么？
- 多源 `compiler main.c helper.c -o app` 如何实现？
- 为什么 `-MD/-MMD` 对 Make/Ninja 很关键？
- 为什么真实工程通常先卡在 driver，而不是优化器？
- 为什么要用 Lua、zlib、SQLite、Git、libpng、OpenSSL 做验证？

### 推荐实验

- 写一个 3 文件 C 项目，用 `CC=$(pwd)/build-ninja/compiler make` 构建
- 写一个 CMake+Ninja 小项目，指定 `CMAKE_C_COMPILER=$(pwd)/build-ninja/compiler`
- 跑真实工程矩阵：

```bash
SYSYCC_REAL_PROJECT_DOCKER_CONTAINER=qemu_dev \
tests/manual/external_real_project_probe/validate_c_projects.sh
```

## 第十三阶段：测试体系

### 必学概念

- unit test
- regression test
- smoke test
- tiered test
- differential testing
- fuzz testing
- oracle
- golden file
- build-system test
- real-project validation
- timeout
- fail-fast

### 在 SysyCC 中看哪里

- `tests/test_helpers.sh`
- `tests/run_all.sh`
- `tests/run_tier1.sh`
- `tests/run_tier2.sh`
- `tests/run_full.sh`
- `tests/compiler2025/`
- `tests/fuzz/`
- `tests/manual/external_real_project_probe/`
- `doc/modules/tests.md`

### 你要能回答的问题

- 一个 parser bug 应该放到哪个测试目录？
- 一个 runtime 行为 bug 应该如何验证？
- 为什么真实工程通过不等于所有语义都正确？
- 为什么全量回归要分 tier？
- 为什么新增功能必须补最小复现？

### 推荐实验

- 给一个已有 bug 写最小测试，再修复 bug
- 跑 `make test-tier1`
- 跑一个单独测试目录的 `run.sh`
- 尝试用 csmith 生成 fuzz case

## 第十四阶段：性能和工程化

### 必学概念

- compiler self build time
- generated-code performance
- compiler compile-time performance
- pass profiling
- hot translation unit
- profiling report
- incremental build
- ccache / sccache
- algorithmic complexity
- pathological case

### 在 SysyCC 中看哪里

- `src/compiler/pass/pass.cpp`
- `tests/compiler/compiler_pass_trace_report/`
- `tests/manual/external_real_project_probe/`
- `Makefile`
- `CMakeLists.txt`
- `doc/modules/tests.md`

### 你要能回答的问题

- 编译器性能有哪三种？
- 为什么优化 pass 可能让编译器本身变慢？
- 如何定位一个真实工程中最慢的 `.c` 文件？
- 如何判断一个性能优化是否影响正确性？
- 为什么不能为了提速直接跳过优化 pass？

### 推荐实验

- 对同一个 TU 优化前后生成 pass report diff
- 找一个最慢 pass，用采样工具确认热点
- 只优化当前瓶颈，然后重新跑真实工程 smoke

## 核心知识清单

### 语言前端

- 字符集和源文件读取
- 注释处理
- 预处理 token
- 宏定义、宏展开、宏递归保护
- include 搜索
- 条件编译表达式
- token 分类
- 关键字识别
- 字面量解析
- grammar 设计
- precedence 和 associativity
- AST 构建
- source location 和 diagnostic
- attribute 解析
- dialect feature gating

### 语义系统

- 符号表
- 作用域栈
- tag namespace
- typedef namespace
- 类型表示
- 类型兼容
- incomplete type
- lvalue/rvalue
- array/function decay
- 整数提升
- 算术转换
- 指针转换
- 结构体/联合体布局
- bit-field
- enum
- 函数原型
- redeclaration
- constant expression
- builtin symbol
- system header compatibility

### IR 和优化

- IR value/instruction/block/function/module
- CFG
- use-def
- SSA
- phi
- mem2reg
- dominance
- dominance frontier
- loop analysis
- alias analysis
- escape analysis
- MemorySSA
- CSE/GVN
- SCCP
- DCE/DSE
- LICM
- loop simplify/rotate/unroll/vectorize
- inlining
- analysis invalidation
- fixed-point convergence
- verifier

### 后端和工具链

- LLVM IR lowering
- target ABI
- register allocation
- stack frame
- spill
- instruction emission
- object file
- relocation
- symbol table
- static library
- dynamic library
- linker
- qemu
- cross compilation

### 工程化

- CMake/Ninja
- Makefile
- ccache
- Docker
- test tiering
- fuzzing
- real project validation
- pass trace
- profiling
- CI mindset
- small commits
- regression-first workflow

## 推荐资料

### 书籍

- 《编译原理》龙书：适合建立传统编译理论框架
- Engineering a Compiler：更工程化，适合配合本项目阅读
- Modern Compiler Implementation in ML/C/Java：适合理解 IR、寄存器分配、运行时
- Compilers: Principles, Techniques, and Tools：经典理论参考
- The C Programming Language：理解 C 语言基础
- C: A Reference Manual：查 C 语言细节

### 在线资料

- LLVM Language Reference Manual
- System V ABI 文档
- AArch64 Procedure Call Standard
- GNU Make Manual
- CMake Documentation
- cppreference 的 C 语言部分

### 建议阅读方式

不要按书从第一页读到最后一页再看项目。更高效的方式是：

1. 先跑项目
2. 遇到概念就查书
3. 回到源码找对应实现
4. 写测试验证
5. 再读更深入的章节

## 典型学习任务清单

### 前端任务

- 添加一个新的关键字识别测试
- 添加一个复杂宏展开测试
- 修复一个 `#if` 常量表达式边界
- 添加一个 parser 错误诊断
- 支持一个小的 GNU attribute 形式

### 语义任务

- 添加一个 typedef/tag 冲突测试
- 修复一个函数原型兼容问题
- 增加一个 constant expression case
- 补一个 standard header smoke
- 改进一个类型错误诊断

### IR 任务

- 添加一个新的 Core IR printer 测试
- 修复一个 aggregate initializer lowering
- 给某个 pass 添加 preserved analyses
- 增加一个 DCE/GVN/LICM regression
- 给 MemorySSA 增加一个 clobber 查询测试

### 后端任务

- 添加一个 AArch64 多对象链接 smoke
- 修复一个外部符号 relocation
- 验证一个 struct return ABI case
- 增加一个 varargs ABI case
- 对比 SysyCC 和 Clang 产物

### 工程任务

- 给真实工程矩阵增加一个小型 C 项目
- 给 pass report 增加一个统计字段
- 优化一个最慢真实 TU
- 改进 Make/Ninja smoke
- 补 README 或模块文档

## 常见误区

- 误区：编译器就是 parser。
  现实：parser 只是前端的一部分，真实工程常常卡在预处理、语义、driver、链接、优化性能。

- 误区：能编译 toy case 就能编译真实工程。
  现实：真实工程会大量使用系统头、宏、typedef、build flags、对象链接和库。

- 误区：优化 pass 只要让 IR 更短就是正确。
  现实：优化必须保持可观察行为不变，尤其要小心内存、别名、调用、副作用、溢出和未定义行为。

- 误区：遇到慢 pass 可以直接跳过。
  现实：这会掩盖问题。更好的方式是 trace、profile、定位热点、优化数据结构或算法。

- 误区：测试越多越好，不需要分层。
  现实：没有 tier 的测试会拖慢迭代，最终大家都不愿意跑。

## 最后建议

学习编译器最好的方式不是“看完所有理论再动手”，而是沿着一个真实工程不断闭环：

```text
读一小段理论
  -> 看 SysyCC 对应模块
  -> 跑一个测试
  -> 改一个小点
  -> 补一个 regression
  -> 再跑回归
```

SysyCC 适合作为学习材料的地方在于：它已经不只是教学骨架，而是有真实 driver、系统头兼容、Core IR 优化、AArch64 后端、分层测试和真实工程矩阵。你可以从最小 token 测试一路走到 Lua、SQLite、Git、OpenSSL 这种真实工程验证。这条路会比只写一个表达式解释器难很多，但也更接近真正的编译器工程。
