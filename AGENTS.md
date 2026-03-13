# AGENTS.md

本项目是一个 `SysY 2022` 编译器实验/实现项目。

## 目标

- 使用 `C++` 实现一个清晰、可扩展的 SysY22 编译器。
- 优先保证代码可读性、模块边界清楚、便于逐步补全前端和后端能力。

## 当前技术栈

- 构建系统：`CMake`
- 便捷命令：`Makefile`
- 语言标准：`C++17`

## 常用命令

```bash
make
make build
make clean
```

等价的 CMake 命令：

```bash
cmake -S . -B build
cmake --build build
./build/SysyCC lex tests/arithmetic.sy
```

## 开发约定

- 默认在现有结构上做最小修改，不随意重命名或大规模搬动文件。
- 新增代码优先保持简单直接，避免过早抽象。
- 如果要扩展编译器模块，优先按职责拆分为词法、语法、语义、IR、代码生成等部分。
- 提交前至少保证可以本地编译通过。

## 当前目录

- `src/`：源代码
- `CMakeLists.txt`：CMake 入口
- `Makefile`：一键构建与运行
