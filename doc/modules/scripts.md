# Scripts Module

## Scope

The scripts module contains developer helper tools outside the compiler binary.

## Main Files

- [show_parse_tree.py](/Users/caojunze424/code/SysyCC/scripts/show_parse_tree.py)
- [run_static_checks.sh](/Users/caojunze424/code/SysyCC/scripts/run_static_checks.sh)
- [run_iwyu.py](/Users/caojunze424/code/SysyCC/scripts/run_iwyu.py)

## Responsibilities

- read parse dump files
- render parse trees in terminal form
- generate local HTML graph pages
- open the generated page in the default browser
- drive repository-wide static analysis checks
- run `clang-tidy` over handwritten C++ translation units
- exclude generated parser headers from `clang-tidy`
- run `cppcheck` from `build/compile_commands.json`
- keep `cppcheck` focused on warning/performance/portability findings
- drive `include-what-you-use` from `build/compile_commands.json`

## Example

```bash
python3 scripts/show_parse_tree.py build/intermediate_results/minimal.parse.txt --open
```

```bash
make check
```

## Notes

- `make check` requires these tools to be installed and visible in `PATH`:
  - `clang-tidy`
  - `cppcheck`
  - `include-what-you-use`
  - `python3`
- the static-check pipeline configures and builds the project first so
  `build/compile_commands.json` is available to all three tools
- generated parser files are intentionally excluded from the blocking
  `clang-tidy` header set
- `cppcheck` style-only suggestions are left non-blocking to keep `make check`
  focused on correctness and performance issues
