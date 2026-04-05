# CLI Module

## Scope

The CLI module converts `argv` into a compiler configuration object.

## Main Files

- [cli.hpp](/Users/caojunze424/code/SysyCC/src/cli/cli.hpp)
- [cli.cpp](/Users/caojunze424/code/SysyCC/src/cli/cli.cpp)

## Responsibilities

- parse command line flags
- normalize public GCC-like driver actions such as `-E`, `-fsyntax-only`,
  `-S`, and `-S -emit-llvm`
- record input and output file paths
- collect include search directories from `-I`
- collect system include search directories from `-isystem`
- collect command-line macro and forced-include inputs from `-D`, `-U`, and
  `-include`
- control default system header lookup via `-nostdinc`
- map dialect-selection flags into compiler configuration
- map `-std=...` and `-f...` extension toggles into effective dialect settings
- parse warning-policy switches such as `-Wall`, `-Wextra`, `-Werror`, and
  `-Wno-...`
- enable dump switches such as `--dump-tokens` and `--dump-parse`
- support `--stop-after=<stage>` so tests and tooling can stop after
  `preprocess`, `lex`, `parse`, `ast`, `semantic`, `core-ir`, `ir`, or `asm`
- select backend-specific emission modes such as native Linux AArch64 asm
- print help, version, and verbose driver configuration information
- fill [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)

## Input and Output

Input:

- `argc`
- `argv`

Output:

- `ClI::Cli` internal state
- [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp)

## Notes

- The namespace is currently `ClI`.
- The CLI does not run compilation logic itself.
- It only prepares configuration for the compiler core.
- The public user-facing actions are now GCC-like:
  - `-E` preprocesses to stdout or `-o`
  - `-fsyntax-only` stops after semantic analysis
  - `-S` emits AArch64 assembly
  - `-S -emit-llvm` emits LLVM IR
- Bare `sysycc input.sy` and `-c` are parsed but currently fail with explicit
  driver errors because linking and object emission are not implemented yet.
- `-I<dir>` and `-I <dir>` are both accepted and stored in [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp).
- `-isystem <dir>` is accepted and merged ahead of the default system include directories unless `-nostdinc` disables the default search roots.
- The parsed include directories are forwarded through the compiler context and consumed by the preprocess stage during local include resolution.
- The parsed system include directories are also forwarded through the compiler context and consumed by the preprocess stage during angle-include and `#include_next` resolution.
- `-D`, `-U`, and `-include` are stored as preprocess-session inputs and are
  applied before the main source file is preprocessed.
- `-std=c99`, `-std=gnu99`, and `-std=sysy` select the base language mode, and
  `-fgnu-extensions`, `-fclang-extensions`, and `-fbuiltin-types` can override
  parts of that base mode for one invocation.
- `--strict-c99` remains accepted as a compatibility alias for disabling GNU,
  Clang, and builtin-type extension packs in one invocation.
- `-Wall`, `-Wextra`, `-Werror`, `-Wno-...`, and `-Werror=...` are parsed into
  shared warning-policy state that the diagnostic engine applies later.
- `--stop-after=<stage>` and `--stop-after <stage>` are both accepted.
- `--stop-after` does not disable dump switches; it only prevents later passes
  from running after the requested stage succeeds.
- Internal pipeline mode is now entered explicitly by internal controls rather
  than by a final heuristic fallback.
- The preferred private internal controls are:
  - `--sysy-dump-tokens`
  - `--sysy-dump-parse`
  - `--sysy-dump-ast`
  - `--sysy-dump-ir`
  - `--sysy-dump-core-ir`
  - `--sysy-stop-after`
  - `--sysy-backend`
  - `--sysy-target`
- The public `-S` path defaults to the native AArch64 backend and fills in the
  default target triple `aarch64-unknown-linux-gnu` when the user does not
  specify one.
- `--backend=llvm-ir|aarch64-native` and `--target=...` remain accepted as
  compatibility / developer controls but are no longer the primary user-facing
  surface.
- The older `--dump-*`, `--stop-after`, `--backend`, and `--target` spellings
  remain as compatibility aliases for existing tests and developer workflows.
- `--dump-ir` remains specific to internal LLVM IR dumps in
  `build/intermediate_results`, while `-S -emit-llvm` is the public primary
  output path for LLVM IR emission.
- `--enable-gnu-dialect` / `--disable-gnu-dialect`,
  `--enable-clang-dialect` / `--disable-clang-dialect`, and
  `--enable-builtin-types` / `--disable-builtin-types` explicitly reconfigure
  the optional dialect packs for one invocation.
- `-v` now prints version plus the effective driver/language/include
  configuration and then continues compiling, while `--version` exits
  immediately.
