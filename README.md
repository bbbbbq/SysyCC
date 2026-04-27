# SysyCC

SysyCC 是一个正在持续演进的 SysY22 / C-like 编译器项目。它已经具备较完整的前端、语义分析、Core IR 优化管线、LLVM IR lowering、AArch64 / RISC-V64 原生后端雏形、分层测试体系，以及真实 C 工程验证脚手架。

当前阶段的主线目标不是继续铺宽语法功能，而是把 SysyCC 从“能产出 IR / 汇编”推进到“能被 Make / Ninja / CMake 等构建系统当作 C 编译器调用，并逐步编译真实 C 工程”。

## 当前能力

- 支持预处理、词法、语法、AST、语义分析、Core IR 构建、Core IR 优化、LLVM IR lowering、原生汇编 / 对象输出等主流程。
- 支持常见 GCC-like driver 形态，包括 `-E`、`-fsyntax-only`、`-S`、`-S -emit-llvm`、`-c`、`-o`、`-I`、`-D`、`-U`、`-include`、`-MD/-MMD/-MF/-MT/-MQ/-MP`、`@response-file`、`--sysroot`、`-isystem`、`-iquote`、`-idirafter`、`-pthread`、`-L`、`-l`、`-Wl,...` 等。
- 支持单源和多源 full compile / link smoke，例如 `build-ninja/compiler main.c helper.c -Iinclude -DVALUE=1 -o app`。
- 支持多源 compile-only 的基础形态，例如 `build-ninja/compiler -c a.c b.c` 会生成 `a.o` 和 `b.o`。
- 系统头兼容正在扩面，当前重点覆盖 `stdlib.h`、`string.h`、`math.h`、`stddef.h`、`assert.h`、`time.h` 等真实工程高频头。
- AArch64 是当前主战后端，重点推进 `.s -> .o -> link -> run` 的对象文件和链接闭环；RISC-V64 当前主要保不回退。
- 已建立 Lua / MuJS 真实工程 probe，用于验证 SysyCC 是否能作为 `CC` 编译更接近真实世界的 C 项目。

## 快速开始

推荐使用 Ninja 构建：

```bash
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
```

也可以使用顶层 Makefile：

```bash
make build
```

常用产物：

- `build-ninja/compiler`: 主要 public driver。
- `build-ninja/SysyCC`: 兼容旧脚本的本地入口。
- `build-ninja/sysycc-aarch64c`: AArch64 原生后端开发入口。
- `build-ninja/sysycc-riscv64c`: RISC-V64 原生后端开发入口。

## 使用示例

编译并运行一个简单 C 程序：

```bash
cat > /tmp/hello.c <<'EOF'
#include <stdio.h>

int main(void) {
    puts("hello from SysyCC");
    return 0;
}
EOF

build-ninja/compiler /tmp/hello.c -o /tmp/hello
/tmp/hello
```

生成对象文件：

```bash
build-ninja/compiler -c main.c -o main.o
```

多源 full compile：

```bash
build-ninja/compiler main.c helper.c -Iinclude -DPROJECT_OFFSET=2 -o app
```

生成 LLVM IR：

```bash
build-ninja/compiler -S -emit-llvm main.c -o main.ll
```

生成汇编：

```bash
build-ninja/compiler -S main.c -o main.s
```

调试阶段输出：

```bash
build-ninja/compiler --stop-after=parse --dump-parse main.c
build-ninja/compiler --stop-after=core-ir --dump-core-ir main.c
```

## 测试

日常开发优先跑 tier1：

```bash
make test-tier1
```

更完整的分层回归：

```bash
make test-tier2
make test-full
```

AArch64 后端相关入口：

```bash
make test-aarch64-ll
make test-aarch64-single-source-smoke
make test-aarch64-single-source-full
```

真实工程和性能调优辅助入口：

```bash
make lua-smoke
make lua-incremental
make lua-incremental TEST_ARGS="lvm.c"
make real-project-compile-times
make pass-report-diff TEST_ARGS="before.md after.md"
```

说明：

- `test-tier1` 是快速主线门禁，覆盖 run / cli / dialects 等高价值 smoke。
- `test-tier2` 覆盖更细的前端、语义、IR、后端测试。
- `test-full` 用于更完整回归。
- Lua / MuJS 真实工程 probe 是手动验证入口，不默认放入 tier1/tier2，因为它依赖 Docker、网络、外部仓库和更长运行时间。

## 真实工程 Probe

真实工程验证脚本位于：

```text
tests/manual/external_real_project_probe/
```

完整验证：

```bash
tests/manual/external_real_project_probe/verify.sh
```

默认脚本会优先复用 `qemu_dev` Docker 容器，在容器内构建 SysyCC、拉取 Lua / MuJS，并尝试将 SysyCC 作为 `CC` 编译这些项目。

快速迭代常用：

```bash
make lua-smoke
make lua-incremental TEST_ARGS="lvm.c"
make real-project-compile-times TEST_ARGS=lua
```

更多说明见 [external real-project probe 文档](tests/manual/external_real_project_probe/README.md)。

## 项目结构

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
└── pass/
```

主流程大致是：

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
      -> IRGen / Core IR pipeline
      -> LLVM IR lowering / native backend
```

注意：仓库内部历史误拼写已完成迁移，新增代码请统一使用 `Compiler` / `CompilerOption`。

## 文档导航

文档入口：

- [doc/README.md](doc/README.md): 模块文档索引和当前状态总览。
- [roadmap.md](roadmap.md): 阶段性路线图。
- [AGENTS.md](AGENTS.md): 人类开发者和代码代理协作规范。

核心模块文档：

- [CLI](doc/modules/cli.md)
- [Compiler Core](doc/modules/compiler.md)
- [Preprocess](doc/modules/preprocess.md)
- [Lexer](doc/modules/lexer.md)
- [Parser](doc/modules/parser.md)
- [AST](doc/modules/ast.md)
- [Semantic](doc/modules/semantic.md)
- [Core IR / Backend](doc/modules/ir.md)
- [Tests](doc/modules/tests.md)
- [AArch64 Backend Plan](doc/modules/aarch64-llvm-backend-plan.md)

## 当前重点

1. 让 SysyCC 更稳定地作为真实 C 工程的 `CC` 使用。
2. 持续补齐系统头、builtin、GNU / Clang 常见 C 扩展兼容。
3. 推进 AArch64 对象文件、ABI、relocation、PIC、外部链接和运行闭环。
4. 用 Lua / MuJS 等真实工程反向驱动 driver、前端、IR 和后端修复。
5. 在不降低优化正确性的前提下，优化 SysyCC 编译真实工程时的编译耗时。

## 开发约定

- 改行为必须补测试。
- 改模块接口必须同步文档。
- 小步提交，尽量让每个 commit 都可验证、可回滚。
- 不要把生成文件当源码手改；lexer / parser 应优先改 `.l` / `.y` 源规则并通过构建再生成。
- 当前主线优先级：真实工程可编译性 > AArch64 正确性闭环 > 编译耗时性能 > 新语言特性铺宽。

更多协作细节见 [AGENTS.md](AGENTS.md)。

## License

当前仓库未在根目录声明明确开源许可证。若要对外发布或接受外部贡献，建议先补充 `LICENSE` 文件并在本节同步说明。
