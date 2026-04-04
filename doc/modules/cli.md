# CLI Module

## Scope

The CLI module converts `argv` into a compiler configuration object.

## Main Files

- [cli.hpp](/Users/caojunze424/code/SysyCC/src/cli/cli.hpp)
- [cli.cpp](/Users/caojunze424/code/SysyCC/src/cli/cli.cpp)

## Responsibilities

- parse command line flags
- record input and output file paths
- collect include search directories from `-I`
- collect system include search directories from `-isystem`
- map dialect-selection flags into compiler configuration
- enable dump switches such as `--dump-tokens` and `--dump-parse`
- support `--stop-after=<stage>` so tests and tooling can stop after
  `preprocess`, `lex`, `parse`, `ast`, `semantic`, `core-ir`, `ir`, or `asm`
- select backend-specific emission modes such as native Linux AArch64 asm
- print help and version information
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
- `-I<dir>` and `-I <dir>` are both accepted and stored in [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp).
- `-isystem <dir>` is accepted and merged ahead of the default system include directories.
- The parsed include directories are forwarded through the compiler context and consumed by the preprocess stage during local include resolution.
- The parsed system include directories are also forwarded through the compiler context and consumed by the preprocess stage during angle-include and `#include_next` resolution.
- `--strict-c99` disables the GNU, Clang, and builtin-type extension packs for
  one invocation.
- `--stop-after=<stage>` and `--stop-after <stage>` are both accepted.
- `--stop-after` does not disable dump switches; it only prevents later passes
  from running after the requested stage succeeds.
- `-S` currently requests final assembly output and is only supported with
  `--backend=aarch64-native`.
- `--backend=llvm-ir|aarch64-native` selects between the LLVM IR path and the
  first native Linux AArch64 asm backend.
- `--target=aarch64-unknown-linux-gnu` is the only accepted native-backend
  target triple in the current implementation.
- `--dump-ir` remains specific to LLVM IR dumps, while `--dump-core-ir` writes
  the optimized Core IR module before backend-specific emission.
- `--enable-gnu-dialect` / `--disable-gnu-dialect`,
  `--enable-clang-dialect` / `--disable-clang-dialect`, and
  `--enable-builtin-types` / `--disable-builtin-types` explicitly reconfigure
  the optional dialect packs for one invocation.
