# CLI Module

## Scope

The CLI module converts `argv` into a compiler configuration object.

## Main Files

- [cli.hpp](/Users/caojunze424/code/SysyCC/src/cli/cli.hpp)
- [cli.cpp](/Users/caojunze424/code/SysyCC/src/cli/cli.cpp)

## Responsibilities

- parse command line flags
- normalize public GCC-like driver actions such as `-E`, `-fsyntax-only`,
  `-S`, `-c`, full-compile linking, `-g`, and `-S -emit-llvm`
- parse public optimization switches such as `-O0` and `-O1`
- record input and output file paths
- collect include search directories from `-I`
- collect system include search directories from `-isystem`
- collect command-line macro and forced-include inputs from `-D`, `-U`, and
  `-include`
- collect depfile controls from `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`, and `-MP`
- collect external-link passthrough state from `-pthread`, `-L`, `-l`, and
  `-Wl,...`
- classify common GCC-like build flags into supported, safe-ignore,
  pass-through, or explicit-error behavior
- control default system header lookup via `-nostdinc`
- map dialect-selection flags into compiler configuration
- map `-std=...` and `-f...` extension toggles into effective dialect settings
- parse warning-policy switches such as `-Wall`, `-Wextra`, `-Werror`, and
  `-Wno-...`
- enable dump switches such as `--dump-tokens` and `--dump-parse`
- support `--stop-after=<stage>` so tests and tooling can stop after
  `preprocess`, `lex`, `parse`, `ast`, `semantic`, `core-ir`, `ir`, or `asm`
- select backend-specific emission modes such as native Linux AArch64 or
  RISC-V64 asm
- select native object / PIC / debug emission modes such as `-c`, `-fPIC`,
  and `-g`
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
- The default public executable produced by the build is `compiler`, while
  `build/SysyCC` remains as a local compatibility alias for older scripts.
- The public user-facing actions are now GCC-like:
  - `-E` preprocesses to stdout or `-o`
  - `-fsyntax-only` stops after semantic analysis
  - `-S` emits native assembly
  - `-c` emits a native ELF object file
  - bare full-compile invocations lower source inputs to LLVM IR and then call
    a host `clang`/`cc` driver to produce an executable
  - link-only invocations with positional `.o`/`.a`/`.so`/`.dylib` inputs call
    that same host link driver directly
  - `-g` forwards a native debug-info request
  - `-S -emit-llvm` emits LLVM IR
- `-MD` now requests a depfile for the primary output including system headers,
  while `-MMD` excludes system headers.
- `-MF` overrides the depfile path, `-MT` and `-MQ` override depfile target
  names, and `-MP` adds phony header targets for deleted-header-safe
  incremental rebuilds.
- `-MT` target strings are forwarded to the dependency scanner without
  additional make escaping, while `-MQ` requests make-escaped spelling. Multiple
  `-MT` and `-MQ` options are preserved in command-line order so build-system
  generated depfiles can name several outputs deterministically.
- `-MF` accepts joined or separate arguments and can name nested relative paths
  or absolute paths. The compiler creates missing depfile parent directories
  after the SysyCC compilation step succeeds.
- When no explicit `-MT` or `-MQ` is provided, the driver passes the primary
  output path as a make-quoted default depfile target, so object paths with
  spaces remain usable in Make/Ninja depfiles.
- `-MF`, `-MT`, `-MQ`, and `-MP` currently require `-MD` or `-MMD`, and
  dependency generation is only accepted on output-producing public driver
  actions such as `-c`, `-S`, `-S -emit-llvm`, and single-source full compile.
- Depfile generation is handled in the driver by invoking a host dependency
  scanner after SysyCC compilation succeeds. The driver tries `clang`, then
  `cc`, and `SYSYCC_HOST_CC` can override that probe order when integration
  needs a fixed host compiler path.
- Bare public driver invocations no longer stop at an explicit
  linking-not-supported diagnostic. The current full-compile path lowers one
  or more source inputs to temporary LLVM IR files and then shells out to a
  host `clang`/`cc` driver for the final compile+link step, with `a.out` as the
  default executable path when `-o` is omitted.
- That full-compile path can collect multiple source inputs, extra positional
  linker inputs, `-pthread`, `-L`, `-l`, and `-Wl,...` for the external host
  link driver. The native AArch64/RISC-V object-emission paths still remain the
  public `-c`/`-S` workflows.
- Multi-source `-c`, `-S`, `-E`, and `-fsyntax-only` invocations are not yet
  supported. Multi-source `-MD`/`-MMD` depfile generation is also rejected
  until the driver has a defined depfile merge strategy.
- Multi-source final linking of SysyCC-native AArch64 objects is still not the
  stabilized mainline path; build systems should currently drive SysyCC one
  source at a time through `-c` and then use the external-link skeleton only
  when the resulting objects are host-linkable for the active environment.
- `-O0` keeps the minimum Core IR pipeline required by later lowering, while
  `-O1` additionally enables the current Core IR optimization batch:
  canonicalization, constant folding, and dead-code elimination.
- Unsupported higher optimization levels such as `-O2`, `-O3`, and `-Os`
  currently fail with explicit driver diagnostics instead of being accepted
  silently.
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
- The public `-S` path still defaults to the native AArch64 backend and fills
  in `aarch64-unknown-linux-gnu` when the user does not specify a target.
- The public `-c` path still defaults to the native AArch64 backend and the
  same default target triple.
- `--backend=riscv64-native` now switches the native path to a decoupled
  RISC-V64 codegen library and fills in `riscv64-unknown-linux-gnu` when the
  user omits `--target`.
- `-fPIC` and `-g` are now forwarded to both native backends.
- `--backend=llvm-ir|aarch64-native|riscv64-native` and `--target=...` remain
  accepted as compatibility / developer controls but are no longer the primary
  user-facing surface.
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
  configuration and optimization level, then continues compiling, while
  `--version` exits immediately.

## Driver Input Support Matrix

| Invocation shape | Status | Current behavior |
| --- | --- | --- |
| `source.c -o app` | supported | Lowers one source to LLVM IR and links through the host C driver. |
| `main.c helper.c -o app` | supported | Lowers each source with an isolated compiler context, then links all temporary LLVM IR files together. |
| `main.c helper.o -o app` | supported | Lowers the source to LLVM IR and forwards the object to the host link step after resetting `-x none`. |
| `main.c libhelper.a -o app` | supported | Lowers the source to LLVM IR and forwards the archive to the host link step. |
| `helper.o libhelper.a -o app` | supported | Runs link-only mode through the host C driver. |
| `-c a.c` | supported | Emits one native object through the selected native backend. |
| `-c a.c b.c` | unsupported | Rejected with a stable diagnostic; compile sources separately for now. |
| `-S a.c b.c`, `-E a.c b.c`, `-fsyntax-only a.c b.c` | unsupported | Rejected with a multiple-input diagnostic. |
| `-MD/-MMD main.c helper.c -o app` | unsupported | Rejected because multi-source depfile merging is not defined yet. |

## GCC-like Build Compatibility Matrix

| Flag(s) | Status | Current behavior |
| --- | --- | --- |
| `-x c` | supported | Forces C parsing for following inputs, including non-`.c` file names. |
| `-MD` | supported | Emits a depfile that includes system headers. |
| `-MMD` | supported | Emits a depfile that excludes system headers. |
| `-MF`, `-MT`, `-MQ`, `-MP` | supported with `-MD`/`-MMD` | Override depfile path or target spelling and optionally add phony header targets. |
| `-fPIC`, `-g` | supported | Forwarded into backend/driver state for native output or full-compile linking. |
| `-pipe`, `-ffunction-sections`, `-fdata-sections`, `-fno-common`, `-fvisibility=hidden`, `-Winvalid-pch`, `-arch arm64`, `-arch aarch64` | safe ignore | Accepted for build-system compatibility without changing the current output mode. |
| `-pthread`, `-L`, `-l`, `-Wl,...` | pass through | Stored on [ComplierOption](/Users/caojunze424/code/SysyCC/src/compiler/complier_option.hpp) and forwarded to the external host link driver when a link stage runs. |
| unsupported `-x <lang>` | explicit error | Only `-x c` is currently accepted. |
| unsupported `-arch <arch>` | explicit error | Only `arm64` and `aarch64` are accepted as compatibility spellings. |
| unsupported `-fvisibility=<mode>` | explicit error | Only `hidden` is currently accepted as a safe-ignore compatibility flag. |
| unknown flags | explicit error | The driver does not silently swallow unclassified options. |
