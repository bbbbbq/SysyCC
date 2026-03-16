# Legacy Pass Module

## Scope

The `src/pass/` directory is a legacy or compatibility area. It is separate
from the active pass implementation under `src/compiler/pass/`.

## Main Files

- [pass_manager.cpp](/Users/caojunze424/code/SysyCC/src/pass/pass_manager.cpp)
- [pass_manager.hpp](/Users/caojunze424/code/SysyCC/src/pass/pass_manager.hpp)

## Current Status

- This directory is not the main pass implementation path.
- The active pass types used by the compiler live under
  [src/compiler/pass](/Users/caojunze424/code/SysyCC/src/compiler/pass).
- These files mainly exist for compatibility or for future cleanup.

## Recommendation

If the project continues evolving, it is better to either:

- merge `src/pass/` into `src/compiler/pass/`
- or remove the legacy copy after all references are cleaned up

