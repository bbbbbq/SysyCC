# CLI Module

## Scope

The CLI module converts `argv` into a compiler configuration object.

## Main Files

- [cli.hpp](/Users/caojunze424/code/SysyCC/src/cli/cli.hpp)
- [cli.cpp](/Users/caojunze424/code/SysyCC/src/cli/cli.cpp)

## Responsibilities

- parse command line flags
- record input and output file paths
- enable dump switches such as `--dump-tokens` and `--dump-parse`
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

