# Compiler2026 Functional Suite

`tests/compiler2026/` contains the recovered 2026 initial-round ARM functional
cases from `2026初赛ARM赛道功能用例.zip`.

The 2026 case format is compatible with the 2025 functional harness, so this
directory intentionally reuses the `tests/compiler2025/` runners instead of
forking another copy of the test logic. The wrapper scripts set the 2026 data
root and report/build directories, then delegate to the 2025-compatible runner.

Useful entry points:

```bash
tests/compiler2026/run_functional.sh --suite functional 00_main
tests/compiler2026/run_functional.sh --suite all
tests/compiler2026/run_functional_in_docker.sh --suite all
tests/compiler2026/run_arm_functional.sh 00_main
tests/compiler2026/run_arm_functional_in_docker.sh 00_main
```

The extracted data lives under:

```text
tests/compiler2026/extracted/functional/functional_recover/
├── functional/
└── h_functional/
```
