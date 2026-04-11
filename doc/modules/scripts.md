# Scripts Module

## Scope

The scripts module contains developer helper tools outside the compiler binary.

## Main Files

- [show_parse_tree.py](/Users/caojunze424/code/SysyCC/scripts/show_parse_tree.py)
- [run_static_checks.sh](/Users/caojunze424/code/SysyCC/scripts/run_static_checks.sh)
- [run_iwyu.py](/Users/caojunze424/code/SysyCC/scripts/run_iwyu.py)
- [codex_cli_watchdog.sh](/Users/caojunze424/.codex/worktrees/f141/SysyCC/scripts/codex_cli_watchdog.sh)

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
- monitor local `codex` CLI sessions and relaunch one with a fixed prompt if
  the interactive session disappears, with an optional stale-session heuristic
  based on recent Codex local log/history updates

## Example

```bash
python3 scripts/show_parse_tree.py build/intermediate_results/minimal.parse.txt --open
```

```bash
make check
```

```bash
scripts/codex_cli_watchdog.sh --dry-run --once
```

```bash
scripts/codex_cli_watchdog.sh --restart-stale --stale-after 1800
```

```bash
make test-tier1
```

```bash
make test-tier2 TEST_ARGS="--stage ir"
```

## Notes

- `make check` requires these tools to be installed and visible in `PATH`:
  - `clang-tidy`
  - `cppcheck`
  - `include-what-you-use`
  - `python3`
- `make test-tier1` runs `./tests/run_tier1.sh`
- `make test-tier2` runs `./tests/run_tier2.sh`
- `make test-full` runs `./tests/run_full.sh`
- `make test` is an alias for `make test-tier1`
- `TEST_ARGS="..."` forwards extra arguments such as `--list` or `--stage ir`
- the static-check pipeline configures and builds the project first so
  `build/compile_commands.json` is available to all three tools
- generated parser files are intentionally excluded from the blocking
  `clang-tidy` header set
- `cppcheck` style-only suggestions are left non-blocking to keep `make check`
  focused on correctness and performance issues
- `codex_cli_watchdog.sh` currently targets macOS Terminal or iTerm and uses
  one fixed prompt file under `scripts/prompts/`
- stale-session detection is heuristic rather than a true Codex task-status API;
  it currently watches recent updates to `~/.codex/log/codex-tui.log`,
  `~/.codex/history.jsonl`, and `~/.codex/session_index.jsonl`
