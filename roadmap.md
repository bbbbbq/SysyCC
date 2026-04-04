# SysyCC Roadmap

## 当前里程碑

### Milestone 1

- 对比 `SysyCC` 生成的 LLVM IR 链接后程序与 Clang 编译程序的 benchmark 性能。
- 以 benchmark 结果反向推动当前 IR 优化实现，目标达到 Clang 对应程序约 `80%` 的性能。

### Milestone 2

- 完成 LLVM IR 后端，把 LLVM IR 继续转换为汇编代码。
- 后续接入汇编器和链接器，生成最终可执行文件。

### Milestone 3

- 使用计算机系统能力大赛的正确性测试用例，系统验证编译器输出程序的正确性。
